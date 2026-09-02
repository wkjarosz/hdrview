#include "app.h"

#include "colormap.h"
#include "image.h"
#include "texture.h"

using namespace std;

void HDRViewApp::setup_rendering()
{
    try
    {
        // Query (and cache) the backend's max texture size while a graphics context is guaranteed to be current on
        // this (the main) thread. Image::finalize() relies on the cached value from background loader threads.
        spdlog::info("Maximum supported texture size: {}", Texture::max_size());

        m_render_pass = make_unique<RenderPass>(false, true);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);
        m_render_pass->set_depth_test(RenderPass::DepthTest::Always, false);
        m_render_pass->set_clear_color(float4(0.15f, 0.15f, 0.15f, 1.f));

        m_shader = make_unique<Shader>(m_render_pass.get(),
                                       /* An identifying name */
                                       "ImageView", Shader::from_asset("shaders/image-shader_vert"),
                                       Shader::from_asset("shaders/image-shader_frag"), Shader::BlendMode::AlphaBlend);

        const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

        m_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);

        Image::make_default_textures();
        Colormap::initialize();

        m_shader->set_texture("dither_texture", Image::dither_texture());
        set_image_textures();
        spdlog::info("Successfully initialized graphics API!");
    }
    catch (const exception &e)
    {
        // m_shader would be left null; every draw call downstream dereferences it unconditionally
        // with no null check, so continuing here just trades this error for a later segfault.
        spdlog::critical("Shader initialization failed!:\n\t{}.", e.what());
        exit(EXIT_FAILURE);
    }
}

bool HDRViewApp::supports_hdr() const
{
    // --sdr means "behave as if this were an SDR display", so it wins over anything the hardware reports
    if (m_force_sdr)
        return false;

#if defined(__APPLE__)
    // Metal's EDR path: hasEdrSupport() inspects the attached screens' EDR headroom, and the colorpass
    // never runs here
    return HelloImGui::hasEdrSupport();
#else
    // Mirrors tev's rule: we can exceed SDR either through a float framebuffer or through a transfer
    // function that is itself HDR, and the display must not have said its ceiling is SDR. A max of 0 means
    // "unknown", not "no headroom", and some systems never report it, so assume HDR there.
    const bool extended_range = m_float_buffer || m_display_cs.tf.type == TransferFunction::BT2100_PQ ||
                                m_display_cs.tf.type == TransferFunction::BT2100_HLG;
    return extended_range && (m_display_cs.max_nits > 80.f || m_display_cs.max_nits == 0.f);
#endif
}

float HDRViewApp::display_headroom() const
{
    // an SDR display's ceiling is its white; reported, not left unknown, so the histogram still marks it
    if (m_force_sdr)
        return 1.f;

#if defined(__APPLE__)
    // Without the float buffer we draw into a plain 8-bit sRGB layer, so white is the ceiling whatever the
    // panel could reach. That framebuffer is decided once at startup from all the screens attached then
    // (hasEdrSupport()), while the headroom below is the current screen's alone.
    if (!m_float_buffer)
        return 1.f;

    return cocoa_display_headroom(m_params.backendPointers.glfwWindow);
#else
    // Wayland (and Windows, once its ceiling is real) fills these in from the compositor.
    if (m_display_cs.max_nits <= 0.f || m_display_cs.sdr_white_nits <= 0.f)
        return 0.f;

    // a ceiling below SDR white is a display describing itself incoherently, so clamp it
    return std::max(1.f, m_display_cs.max_nits / m_display_cs.sdr_white_nits);
#endif
}

//
// The colorpass. When m_color_managed is true, everything HDRView draws (the image content, and Dear ImGui's
// UI, which has no notion of display color space) is redirected into an offscreen texture, and a full-screen
// pass converts that to whatever the real framebuffer needs just before the frame is presented.
// Modeled on nanogui-1's Screen::m_wants_color_management / ColorPass. Every function below no-ops when
// m_color_managed is false, including on macOS EDR, which consumes extended sRGB directly.
//
// The two halves run at different points in the frame (begin_colorpass_frame() from CustomBackground,
// end_colorpass_frame() from BeforeSwap), so update_colorpass() decides once per frame ahead of both.
//

void HDRViewApp::update_colorpass()
{
#if defined(HELLOIMGUI_HAS_OPENGL)
    if (m_colorpass_failed)
        return;

    auto cs = query_display_colorspace(m_params.backendPointers.glfwWindow);
    if (cs != m_display_cs)
    {
        m_display_cs = cs;
        color_conversion_matrix(m_gamut_matrix, gamut_chromaticities(ColorGamut_sRGB_BT709), cs.chroma);
        spdlog::info("Display color space is now {} ({}).", cs.name(),
                     cs.needs_color_management() ? "colorpass active" : "no color management needed");
    }

    m_color_managed = cs.needs_color_management();
    if (!m_color_managed)
        return;

    float2 fbscale = ImGui::GetIO().DisplayFramebufferScale;
    int2   fb_size = int2(float2{ImGui::GetIO().DisplaySize} * fbscale);
    if (fb_size.x <= 0 || fb_size.y <= 0)
    {
        m_color_managed = false;
        return;
    }

    try
    {
        if (!m_colorpass_shader)
        {
            m_color_texture =
                make_unique<Texture>(Texture::PixelFormat::RGBA, Texture::ComponentFormat::Float16, fb_size,
                                     Texture::InterpolationMode::Nearest, Texture::InterpolationMode::Nearest,
                                     Texture::WrapMode::ClampToEdge, 1,
                                     Texture::TextureFlags::ShaderRead | Texture::TextureFlags::RenderTarget);

            // Everything the frame draws lands in m_colorpass_pass; m_resolve_pass then converts it to the
            // window's framebuffer. The resolve pass overwrites every pixel, so it must not clear.
            m_colorpass_pass = make_unique<RenderPass>(false, true, m_color_texture.get());
            m_resolve_pass   = make_unique<RenderPass>(false, false);
            m_colorpass_pass->set_cull_mode(RenderPass::CullMode::Disabled);
            m_resolve_pass->set_cull_mode(RenderPass::CullMode::Disabled);
            // Opaque black, not RenderPass's transparent-black default: the offscreen target's alpha passes
            // straight through, and a zero alpha can make the window translucent on some compositors.
            m_colorpass_pass->set_clear_color(float4{0.f, 0.f, 0.f, 1.f});

            m_colorpass_shader =
                make_unique<Shader>(m_resolve_pass.get(), "ColorPass", Shader::from_asset("shaders/colorpass_vert"),
                                    Shader::from_asset("shaders/colorpass_frag"), Shader::BlendMode::None);

            const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};
            m_colorpass_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);

            spdlog::info("Initialized the HDR colorpass (offscreen target + final display conversion).");
        }

        // resizes the attached texture and revalidates the FBO; a no-op when the size hasn't changed
        m_colorpass_pass->resize(fb_size);
        m_resolve_pass->resize(fb_size);
    }
    catch (const exception &e)
    {
        spdlog::error("HDR colorpass initialization failed, falling back to direct (non-color-managed) "
                      "rendering:\n\t{}.",
                      e.what());
        cleanup_colorpass();
        m_color_managed    = false;
        m_colorpass_failed = true;
    }
#endif
}

void HDRViewApp::begin_colorpass_frame()
{
#if defined(HELLOIMGUI_HAS_OPENGL)
    if (!m_color_managed)
        return;

    // stays active across the rest of the frame, Dear ImGui's rendering included, until
    // end_colorpass_frame() closes it just before the swap
    m_colorpass_pass->begin();
#endif
}

void HDRViewApp::end_colorpass_frame()
{
#if defined(HELLOIMGUI_HAS_OPENGL)
    if (!m_color_managed)
        return;

    m_colorpass_pass->end();

    m_resolve_pass->begin();
    m_colorpass_shader->set_texture("color_texture", m_color_texture.get());
    m_colorpass_shader->set_uniform_block("cpp", {{"tf_type", (int)m_display_cs.tf.type},
                                                  {"tf_gamma", m_display_cs.tf.gamma},
                                                  {"tf_white_nits", m_display_cs.transfer_white_nits()},
                                                  {"sdr_white_nits", m_display_cs.sdr_white_nits},
                                                  {"min_nits", m_display_cs.min_nits},
                                                  {"max_nits", m_display_cs.max_nits},
                                                  {"gamut_matrix", m_gamut_matrix}});
    m_colorpass_shader->begin();
    m_colorpass_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
    m_colorpass_shader->end();
    m_resolve_pass->end();
#endif
}

void HDRViewApp::cleanup_colorpass()
{
    // Order matters: the shader and the passes reference the texture, and the target pass holds an FBO
    // attachment to it. All of these must be released while the GL context is still alive, hence BeforeExit.
    m_colorpass_shader.reset();
    m_resolve_pass.reset();
    m_colorpass_pass.reset();
    m_color_texture.reset();
    m_color_managed = false;
}
