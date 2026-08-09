#include "app.h"

#include "colormap.h"
#include "common.h"
#include "fonts.h"
#include "image.h"
#include "texture.h"

#include <random>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

using namespace std;

void HDRViewApp::draw_pixel_grid() const
{
    if (!current_image())
        return;

    static const int s_grid_threshold = 10;

    if (!m_draw_grid || (s_grid_threshold == -1) || (m_zoom <= s_grid_threshold))
        return;

    float factor = clamp((m_zoom - s_grid_threshold) / (2 * s_grid_threshold), 0.f, 1.f);
    float alpha  = lerp(0.0f, 1.0f, smoothstep(0.0f, 1.0f, factor));

    if (alpha <= 0.0f)
        return;

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImColor col_fg(1.0f, 1.0f, 1.0f, alpha);
    ImColor col_bg(0.2f, 0.2f, 0.2f, alpha);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    // draw vertical lines
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_bg, 4.f);

    // draw horizontal lines
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_bg, 4.f);

    // and now again with the foreground color
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_fg, 2.f);
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_fg, 2.f);
}

void HDRViewApp::draw_pixel_info() const
{
    auto img = current_image();
    if (!img || !m_draw_pixel_info)
        return;

    auto ref = reference_image();

    static constexpr float2 align = {0.5f, 0.5f};

    auto  &group = img->groups[img->selected_group];
    string names[4];
    string longest_name;
    for (int c = 0; c < group.num_channels; ++c)
    {
        auto &channel = img->channels[group.channels[c]];
        names[c]      = Channel::tail(channel.name);
        if (names[c].length() > longest_name.length())
            longest_name = names[c];
    }

    ImGui::PushFont(m_mono_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    static float line_height = ImGui::CalcTextSize("").y;
    const float2 channel_threshold2 =
        float2{ImGui::CalcTextSize((longest_name + ": 31.00000").c_str()).x, group.num_channels * line_height};
    const float2 coord_threshold2  = channel_threshold2 + float2{0.f, 2.f * line_height};
    const float  channel_threshold = maxelem(channel_threshold2);
    const float  coord_threshold   = maxelem(coord_threshold2);
    ImGui::PopFont();

    if (m_zoom <= channel_threshold)
        return;

    // fade value for the channel values shown at sufficient zoom
    float factor = clamp((m_zoom - channel_threshold) / (1.25f * channel_threshold), 0.f, 1.f);
    float alpha  = smoothstep(0.0f, 1.0f, factor);

    if (alpha <= 0.0f)
        return;

    // fade value for the (x,y) coordinates shown at further zoom
    float factor2 = clamp((m_zoom - coord_threshold) / (1.25f * coord_threshold), 0.f, 1.f);
    float alpha2  = smoothstep(0.0f, 1.0f, factor2);

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImGui::PushFont(m_mono_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    for (int y = bounds.min.y; y < bounds.max.y; ++y)
    {
        for (int x = bounds.min.x; x < bounds.max.x; ++x)
        {
            auto   pos        = app_pos_at_pixel(float2(x + 0.5f, y + 0.5f));
            float4 r_pixel    = pixel_value({x, y}, true, 2);
            float4 t_pixel    = linear_to_sRGB(pixel_value({x, y}, false, 2));
            float4 pixel      = m_status_color_mode == 0 ? r_pixel : t_pixel;
            float3 text_color = contrasting_color(t_pixel.xyz());
            float3 shadow     = contrasting_color(text_color);
            if (alpha2 > 0.f)
            {
                float2 c_pos = pos + float2{0.f, (-1 - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("({},{})", x, y);
                ImGui::AddTextAligned(draw_list, c_pos + 1.f, ImColor(float4{shadow, alpha2}), text, align);
                ImGui::AddTextAligned(draw_list, c_pos, ImColor(float4{text_color, alpha2}), text, align);
            }

            for (int c = 0; c < group.num_channels; ++c)
            {
                float2 c_pos = pos + float2{0.f, (c - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("{:>2s}:{: > 9.5f}", names[c], pixel[c]);
                ImGui::AddTextAligned(draw_list, c_pos + 1.f, ImColor(float4{shadow, alpha2}), text, align);
                ImGui::AddTextAligned(draw_list, c_pos, ImColor{float4{text_color, alpha2}}, text, align);
            }
        }
    }
    ImGui::PopFont();
}

void HDRViewApp::draw_image_border() const
{
    auto draw_list = ImGui::GetBackgroundDrawList();

    auto cimg = current_image();
    auto rimg = reference_image();

    if (!cimg && !rimg)
        return;

    if (cimg && cimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{cimg->data_window.min}), app_pos_at_pixel(float2{cimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{cimg->display_window.min}),
                                    app_pos_at_pixel(float2{cimg->display_window.max})}
                                  .make_valid();
        bool non_trivial = cimg->data_window != cimg->display_window || cimg->data_window.min != int2{0, 0};
        ImGui::PushRowColors(true, false);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive), "Data window",
                                   {0.f, 0.f}, non_trivial);
        if (m_draw_display_window && non_trivial)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header), "Display window",
                                   {1.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (rimg && rimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{rimg->data_window.min}), app_pos_at_pixel(float2{rimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{rimg->display_window.min}),
                                    app_pos_at_pixel(float2{rimg->display_window.max})}
                                  .make_valid();
        ImGui::PushRowColors(false, true, true);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive),
                                   "Reference data window", {1.f, 0.f}, true);
        if (m_draw_display_window)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header),
                                   "Reference display window", {0.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (m_roi_live.has_volume())
    {
        Box2f crop_window{app_pos_at_pixel(float2{m_roi_live.min}), app_pos_at_pixel(float2{m_roi_live.max})};
        ImGui::DrawLabeledRect(draw_list, crop_window, ImGui::ColorConvertFloat4ToU32(float4{float3{0.5f}, 1.f}),
                               "Selection", {0.5f, 1.f}, true);
    }
}

void HDRViewApp::draw_tool_decorations() const
{
    if (!current_image())
        return;

    auto draw_list = ImGui::GetBackgroundDrawList();

    if (m_draw_watched_pixels)
    {
        ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase);
        for (int i = 0; i < (int)m_watched_pixels.size(); ++i)
            ImGui::DrawCrosshairs(draw_list, app_pos_at_pixel(m_watched_pixels[i].pixel + 0.5f),
                                  fmt::format(" {}", i + 1));
        ImGui::PopFont();
    }

    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 18.f / 14.f);

    float2 pos = ImGui::GetIO().MousePos;
    if (m_mouse_mode == MouseMode_RectangularSelection)
    {
        // draw selection indicator
        ImGui::AddTextAligned(draw_list, pos + int2{18} + int2{1, 1}, IM_COL32_BLACK, ICON_MY_SELECT, {0.5f, 0.5f});
        ImGui::AddTextAligned(draw_list, pos + int2{18}, IM_COL32_WHITE, ICON_MY_SELECT, {0.5f, 0.5f});
    }
    else if (m_mouse_mode == MouseMode_ColorInspector)
    {
        // draw pixel watcher indicator
        ImGui::DrawCrosshairs(draw_list, pos + int2{18}, " +");
    }

    ImGui::PopFont();
}

void HDRViewApp::draw_image() const
{
    auto set_color = [this](Target_ target, ConstImagePtr img)
    {
        float4x4 M_to_sRGB{la::identity};
        int      channels_type = (int)ChannelGroup::Single_Channel;
        float3   yw            = sRGB_Yw();

        if (img)
        {
            int                 group_idx = target == Target_Primary ? img->selected_group : img->reference_group;
            const ChannelGroup &group     = img->groups[group_idx];

            // FIXME: tried to pass this as a 3x3 matrix, but the data was somehow not being passed properly to MSL.
            // resulted in rapid flickering. So, for now, just pad the 3x3 matrix into a 4x4 one.
            M_to_sRGB = float4x4{
                {img->M_to_sRGB[0], 0.f}, {img->M_to_sRGB[1], 0.f}, {img->M_to_sRGB[2], 0.f}, {0.f, 0.f, 0.f, 1.f}};
            channels_type = (int)group.type;
            yw            = img->luminance_weights;
        }

        if (target == Target_Primary)
            m_shader->set_uniform_block(
                "fsp",
                {{"primary_M_to_sRGB", M_to_sRGB}, {"primary_channels_type", channels_type}, {"primary_yw", yw}});
        else
            m_shader->set_uniform_block(
                "fsp",
                {{"secondary_M_to_sRGB", M_to_sRGB}, {"secondary_channels_type", channels_type}, {"secondary_yw", yw}});
    };

    set_color(Target_Primary, current_image());
    set_color(Target_Secondary, reference_image());

    if (current_image() && !current_image()->data_window.is_empty())
    {
        static mt19937 rng(53);
        float2         randomness(generate_canonical<float, 10>(rng) * 255, generate_canonical<float, 10>(rng) * 255);

        const bool   has_reference   = (bool)reference_image();
        const float2 secondary_pos   = has_reference ? image_position(reference_image()) : float2{0.f};
        const float2 secondary_scale = has_reference ? image_scale(reference_image()) : float2{1.f};

        // fs_params/vs_params are GLSL uniform blocks (see image-shader.sglsl); booleans are `int` there
        // since GLSL uniform blocks cannot contain `bool` members.
        m_shader->set_uniform_block("fsp", {{"time", (float)ImGui::GetTime()},
                                            {"draw_clip_warnings", (int)m_draw_clip_warnings},
                                            {"clip_range", m_clip_range},
                                            {"randomness", randomness},
                                            {"gain", powf(2.0f, m_exposure_live)},
                                            {"offset", m_offset_live},
                                            {"gamma", m_gamma_live},
                                            {"tonemap_mode", (int)m_tonemap},
                                            {"clamp_to_LDR", (int)m_clamp_to_LDR},
                                            {"do_dither", (int)m_dither},
                                            {"blend_mode", (int)m_blend_mode},
                                            {"channel", (int)m_channel},
                                            {"bg_mode", (int)m_bg_mode},
                                            {"bg_color", m_bg_color},
                                            {"reverse_colormap", (int)m_reverse_colormap},
                                            {"has_reference", (int)has_reference}});

        m_shader->set_uniform_block("vsp", {{"primary_pos", image_position(current_image())},
                                            {"primary_scale", image_scale(current_image())},
                                            {"secondary_pos", secondary_pos},
                                            {"secondary_scale", secondary_scale}});

        m_shader->set_texture("colormap", Colormap::texture(m_colormaps[m_colormap_index]));

        m_shader->begin();
        m_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
        m_shader->end();
    }

    // ImGui::Begin("Texture window");
    // ImGui::Image((ImTextureID)(intptr_t)Colormap::texture(m_colormap)->texture_handle(),
    //              ImGui::GetContentRegionAvail());
    // ImGui::End();
}

void HDRViewApp::process_shortcuts()
{
    // spdlog::trace("Processing shortcuts (frame: {})", ImGui::GetFrameCount());

    for (auto &a : m_actions)
        if (a.second.chord)
            if (a.second.enabled() && !ImGui::GetIO().NavVisible &&
                ImGui::GlobalShortcut(a.second.chord, a.second.flags))
            {
                spdlog::trace("Processing shortcut for action '{}' (frame: {})", a.second.names[0],
                              ImGui::GetFrameCount());
                if (a.second.p_selected)
                    *a.second.p_selected = !*a.second.p_selected;
                a.second.callback();
#ifdef __EMSCRIPTEN__
                ImGui::GetIO().ClearInputKeys(); // FIXME: somehow needed in emscripten, otherwise the key (without
                                                 // modifiers) needs to be pressed before this chord is detected again
#endif
                break;
            }

    set_image_textures();
}

void HDRViewApp::draw_background()
{
    using namespace literals;

    // Decide once, here, whether this frame needs color management, so that the two halves of the colorpass
    // -- which run at different points in the frame -- can never disagree. Then, if it does, redirect this
    // frame's rendering (this call, and the ImGui rendering that follows it) into an offscreen target
    // instead of the real framebuffer; end_colorpass_frame() converts it to the real framebuffer right
    // before the frame is presented. Both no-op otherwise.
    update_colorpass();
    begin_colorpass_frame();

    static auto prev_frame                   = chrono::steady_clock::now();
    static auto last_file_changes_check_time = chrono::steady_clock::now();
    auto        this_frame                   = chrono::steady_clock::now();

    if ((m_play_forward || m_play_backward) &&
        this_frame - prev_frame >= chrono::milliseconds(int(1000 / m_playback_speed)))
    {
        set_current_image_index(
            next_visible_image_index(m_current, m_play_forward ? Direction_Forward : Direction_Backward));
        set_image_textures();
        prev_frame = this_frame;
    }

    // process_shortcuts();

    // If watching files for changes, do so every 250ms
    if (m_watch_files_for_changes && this_frame - last_file_changes_check_time >= 250ms)
    {
        spdlog::trace("Checking for file changes...");
        m_image_loader.load_new_and_modified_files();
        last_file_changes_check_time = this_frame;
    }

    try
    {
        auto &io = ImGui::GetIO();

        calculate_viewport();

        handle_mouse_interaction();

        float2 fbscale = io.DisplayFramebufferScale;
        // RenderPass expects things in framebuffer coordinates
        m_render_pass->resize(int2{float2{io.DisplaySize} * fbscale});
        m_render_pass->set_viewport(int2(m_viewport_min * fbscale), int2(m_viewport_size * fbscale));

        auto_fit_viewport();

        m_render_pass->begin();
        draw_image();
        m_render_pass->end();

        draw_pixel_info();
        draw_pixel_grid();
        draw_image_border();
        draw_tool_decorations();
    }
    catch (const exception &e)
    {
        spdlog::error("Drawing failed:\n\t{}.", e.what());
    }
}

void HDRViewApp::set_image_textures()
{
    try
    {
        // bind the primary and secondary images, or a placehold black texture when we have no current or
        // reference image
        if (auto img = current_image())
            img->set_as_texture(Target_Primary);
        else
            Image::set_null_texture(Target_Primary);

        if (auto ref = reference_image())
            ref->set_as_texture(Target_Secondary);
        else
            Image::set_null_texture(Target_Secondary);
    }
    catch (const exception &e)
    {
        spdlog::error("Could not upload texture to graphics backend: {}.", e.what());
    }
}

void HDRViewApp::setup_rendering()
{
    try
    {
        // Query (and cache) the backend's max texture size while a graphics context is guaranteed to be current on
        // this (the main) thread. Image::finalize() relies on the cached value from background loader threads.
        spdlog::info("Maximum supported texture size: {}", Texture::max_size());

        m_render_pass = new RenderPass(false, true);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);
        m_render_pass->set_depth_test(RenderPass::DepthTest::Always, false);
        m_render_pass->set_clear_color(float4(0.15f, 0.15f, 0.15f, 1.f));

        // colorspaces.sglsl's shared functions are baked directly into image-shader_frag's generated text at
        // sokol-shdc compile time (see assets/shaders/image-shader.sglsl), so no runtime prepend_includes()
        // is needed here anymore, unlike the old hand-maintained image-shader_frag.{glsl,metal}.
        m_shader = new Shader(m_render_pass,
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

//
// The colorpass: when m_color_managed is true, everything HDRView draws -- the image content
// (draw_background()) and Dear ImGui's own UI alike -- keeps emitting HDRView's usual extended-sRGB colors
// (Dear ImGui has no notion of display color space). Both are redirected into an offscreen texture, and a
// single full-screen pass converts that to whatever the real framebuffer needs, right before the frame is
// presented -- mirrors nanogui-1's Screen::m_wants_color_management / ColorPass. Inert (every function
// below no-ops) when m_color_managed is false, including on macOS EDR, which consumes HDRView's
// extended-sRGB output directly.
//
// The two halves run at different points in the frame -- begin_colorpass_frame() from CustomBackground,
// end_colorpass_frame() from BeforeSwap -- so update_colorpass() decides once per frame, ahead of both, and
// they must never re-decide independently.
//

bool HDRViewApp::supports_hdr() const
{
    // --sdr means "behave as if this were an SDR display", so it has to win over anything the hardware
    // reports. Checked here, once, rather than being re-derived by each platform branch below: we never
    // asked for a float buffer in this mode, so the HDR-only UI would be inert anyway.
    if (m_force_sdr)
        return false;

#if defined(__APPLE__)
    // Metal's EDR path: hasEdrSupport() inspects the attached screens' EDR headroom directly, and the
    // colorpass never runs here.
    return HelloImGui::hasEdrSupport();
#else
    // Mirrors tev's rule. We can exceed SDR either through a float framebuffer or through a transfer
    // function that is itself HDR, and the display must not have told us its ceiling is merely SDR. Note
    // that 0 means "unknown", not "no headroom" -- assume HDR in that case, as some systems never report it.
    const bool extended_range = m_float_buffer || m_display_cs.tf.type == TransferFunction::BT2100_PQ ||
                                m_display_cs.tf.type == TransferFunction::BT2100_HLG;
    return extended_range && (m_display_cs.max_nits > 80.f || m_display_cs.max_nits == 0.f);
#endif
}

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

            // Everything the frame draws lands in m_colorpass_target; m_resolve_pass then converts it to the
            // window's framebuffer. The resolve pass must not clear -- it overwrites every pixel anyway --
            // and neither writes depth.
            m_colorpass_target = make_unique<RenderPass>(false, true, m_color_texture.get());
            m_resolve_pass     = make_unique<RenderPass>(false, false);
            m_colorpass_target->set_cull_mode(RenderPass::CullMode::Disabled);
            m_resolve_pass->set_cull_mode(RenderPass::CullMode::Disabled);
            // Opaque black, not RenderPass's transparent-black default: the colorpass passes the offscreen
            // target's alpha straight through to the real framebuffer, and a zero alpha there can make the
            // window itself translucent on compositors that honor it.
            m_colorpass_target->set_clear_color(float4{0.f, 0.f, 0.f, 1.f});

            // colorspaces.sglsl's shared functions are baked directly into colorpass_frag's generated text
            // at sokol-shdc compile time (see assets/shaders/colorpass.sglsl), so no runtime
            // prepend_includes() is needed here, matching how setup_rendering() loads the main image shader.
            m_colorpass_shader =
                make_unique<Shader>(m_resolve_pass.get(), "ColorPass", Shader::from_asset("shaders/colorpass_vert"),
                                    Shader::from_asset("shaders/colorpass_frag"), Shader::BlendMode::None);

            const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};
            m_colorpass_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);

            spdlog::info("Initialized the HDR colorpass (offscreen target + final display conversion).");
        }

        // Resizes the attached texture and revalidates the FBO; a no-op when the size hasn't changed.
        m_colorpass_target->resize(fb_size);
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

    // Stays active across the rest of the frame -- including all of Dear ImGui's rendering, which happens
    // outside HDRView's control -- until end_colorpass_frame() closes it just before the swap.
    m_colorpass_target->begin();
#endif
}

void HDRViewApp::end_colorpass_frame()
{
#if defined(HELLOIMGUI_HAS_OPENGL)
    if (!m_color_managed)
        return;

    m_colorpass_target->end();

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
    // attachment to it. All of these must be released while the GL context is still alive, which is why
    // this is called from BeforeExit rather than a destructor.
    m_colorpass_shader.reset();
    m_resolve_pass.reset();
    m_colorpass_target.reset();
    m_color_texture.reset();
    m_color_managed = false;
}
