#include "hdrimageview.h"
#include "colorspace.h"
#include "common.h"
#include "dithermatrix256.h"
#include "hdrcolorpicker.h"
#include "hdrviewscreen.h"
#include "helpwindow.h"
#include "json.h"
#include <hdrview_resources.h>
#include <iostream>
#include <nanogui/opengl.h>
#include <nanogui/renderpass.h>
#include <nanogui/screen.h>
#include <nanogui/shader.h>
#include <nanogui/texture.h>
#include <random>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>
#include <sstream>

using namespace nanogui;
using namespace std;
using json = nlohmann::json;

namespace
{
std::mt19937 g_rand(53);
const float  MIN_ZOOM = 0.01f;
const float  MAX_ZOOM = 512.f;

std::string add_includes(std::string shader_string)
{
#if defined(NANOGUI_USE_OPENGL) or defined(NANOGUI_USE_GLES)
    std::string includes;

    includes += HDRVIEW_SHADER(colormaps_frag) + "\n";
    includes += HDRVIEW_SHADER(colorspaces_frag) + "\n";

    if (!includes.empty())
    {
        std::istringstream iss(shader_string);
        std::ostringstream oss;
        std::string        line;

        // first copy over all the #include or #version lines. these should stay at the top of the shader
        while (std::getline(iss, line) && (line.substr(0, 8) == "#include" || line.substr(0, 8) == "#version"))
            oss << line << std::endl;

        // now insert the new #includes
        oss << includes;

        // and copy over the rest of the lines in the shader
        do {
            oss << line << std::endl;
        } while (std::getline(iss, line));

        // spdlog::trace("GLSL #includes: {};\n MERGED: {}", includes, oss.str());

        shader_string = oss.str();
    }

#endif

    return shader_string;
}

} // namespace

HDRImageView::HDRImageView(Widget *parent, const json &settings) :
    Canvas(parent, 1, false, false, true), m_exposure_callback(FloatCallback()), m_gamma_callback(FloatCallback()),
    m_sRGB_callback(BoolCallback()), m_zoom_callback(FloatCallback()),
    m_drag_callback(
        [this](const Vector2i &p, const Vector2i &rel, int, int)
        {
            set_pixel_at_position(p + rel, pixel_at_position(p));
            return false;
        }),
    m_changed_callback(VoidCallback())
{
    m_zoom   = 1.f / screen()->pixel_ratio();
    m_offset = Vector2f(0.0f);

    // read settings
    json j = json::object();
    if (settings.contains("image view") && settings["image view"].is_object())
        j = settings["image view"];

    m_exposure        = j.value("exposure", 0.0f);
    m_gamma           = j.value("gamma", 2.2f);
    m_sRGB            = j.value("sRGB", true);
    m_bg_mode         = (EBGMode)std::clamp(j.value("background mode", (int)BG_DARK_CHECKER), 0, (int)NUM_BG_MODES - 1);
    m_bg_color        = j.value("background color", Color(0, 255));
    m_clamp_to_LDR    = j.value("LDR", false);
    m_dither          = j.value("dithering", true);
    m_draw_grid       = j.value("grid", true);
    m_grid_threshold  = j.value("grid threshold", 10);
    m_draw_pixel_info = j.value("pixel info", true);
    m_pixel_info_threshold = j.value("pixel info threshold", 40);
    m_zoom_sensitivity     = j.value("zoom sensitivity", 1.0717734625f);

    set_background_color(Color(0.15f, 0.15f, 0.15f, 1.f));

    try
    {
        m_image_shader = new Shader(render_pass(),
                                    /* An identifying name */
                                    "ImageView", HDRVIEW_SHADER(hdrimageview_vert),
                                    add_includes(HDRVIEW_SHADER(hdrimageview_frag)), Shader::BlendMode::AlphaBlend);

        const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

        set_draw_border(false);

        m_image_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);

        m_dither_tex = new Texture(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, Vector2i(256, 256),
                                   Texture::InterpolationMode::Nearest, Texture::InterpolationMode::Nearest,
                                   Texture::WrapMode::Repeat);
        m_dither_tex->upload((const uint8_t *)dither_matrix256);
        m_image_shader->set_texture("dither_texture", m_dither_tex);

        // create an empty texture so that nanogui's shader doesn't print errors
        // before we've selected a reference image
        // FIXME: at some point, find a more elegant solution for this.
        m_null_image   = new Texture(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, Vector2i(1, 1),
                                     Texture::InterpolationMode::Nearest, Texture::InterpolationMode::Nearest,
                                     Texture::WrapMode::Repeat);
        float null_tex = 1.f;
        m_null_image->upload((const uint8_t *)&null_tex);
        m_image_shader->set_texture("secondary_texture", m_null_image);
    }
    catch (const std::exception &e)
    {
        spdlog::critical("{}", e.what());
    }
}

void HDRImageView::write_settings(json &settings) const
{
    if (!settings.contains("image view") || !settings["image view"].is_object())
        settings["image view"] = json::object();

    settings["image view"]["exposure"]             = exposure();
    settings["image view"]["gamma"]                = gamma();
    settings["image view"]["sRGB"]                 = sRGB();
    settings["image view"]["clamp to LDR"]         = clamp_to_LDR();
    settings["image view"]["dithering"]            = dithering_on();
    settings["image view"]["background mode"]      = m_bg_mode;
    settings["image view"]["background color"]     = m_bg_color;
    settings["image view"]["grid"]                 = draw_grid_on();
    settings["image view"]["grid threshold"]       = grid_threshold();
    settings["image view"]["pixel info"]           = draw_pixel_info_on();
    settings["image view"]["pixel info threshold"] = pixel_info_threshold();
    settings["image view"]["zoom sensitivity"]     = zoom_sensitivity();
}

Color4 HDRImageView::tonemap(const Color4 &color) const
{
    float  gain = powf(2.0f, m_exposure);
    Color4 exposed{Color3(color) * gain, color.a};
    return m_sRGB ? LinearToSRGB(exposed) : pow(exposed, Color4(1.0 / m_gamma));
}

void HDRImageView::set_current_image(XPUImagePtr cur)
{
    // spdlog::debug("setting current image: {}", cur.get());
    m_current_image = cur;
    if (m_current_image && m_current_image->texture())
        m_image_shader->set_texture("primary_texture", m_current_image->texture());
    else
        m_image_shader->set_texture("primary_texture", m_null_image);

    m_changed_callback();
}

void HDRImageView::set_reference_image(XPUImagePtr ref)
{
    // spdlog::debug("setting reference image: {}", ref.get());
    m_reference_image = ref;
    if (m_reference_image && m_reference_image->texture())
        m_image_shader->set_texture("secondary_texture", m_reference_image->texture());
    else
        m_image_shader->set_texture("secondary_texture", m_null_image);
}

Vector2f HDRImageView::center_offset(ConstXPUImagePtr img) const { return (size_f() - scaled_image_size_f(img)) / 2; }

Vector2f HDRImageView::pixel_at_position(const Vector2f &position) const
{
    auto image_pos = position - (m_offset + center_offset(m_current_image));
    return image_pos / m_zoom;
}

Vector2f HDRImageView::position_at_pixel(const Vector2f &pixel) const
{
    return m_zoom * pixel + (m_offset + center_offset(m_current_image));
}

Vector2f HDRImageView::screen_position_at_pixel(const Vector2f &pixel) const
{
    return position_at_pixel(pixel) + position_f();
}

void HDRImageView::set_pixel_at_position(const Vector2f &position, const Vector2f &pixel)
{
    // Calculate where the new offset must be in order to satisfy the image position equation.
    // Round the floating point values to balance out the floating point to integer conversions.
    m_offset = position - (pixel * m_zoom);

    // Clamp offset so that the image remains near the screen.
    m_offset = max(min(m_offset, size_f()), -scaled_image_size_f(m_current_image));

    m_offset -= center_offset(m_current_image);
}

void HDRImageView::image_position_and_scale(Vector2f &position, Vector2f &scale, ConstXPUImagePtr image)
{
    scale    = scaled_image_size_f(image) / Vector2f(size());
    position = (m_offset + center_offset(image)) / Vector2f(size());
}

void HDRImageView::center() { m_offset = Vector2f(0.f, 0.f); }

void HDRImageView::fit()
{
    // Calculate the appropriate scaling factor.
    Vector2f factor(size_f() / image_size_f(m_current_image));
    m_zoom = std::min(factor[0], factor[1]);
    center();

    m_zoom_callback(m_zoom);
}

float HDRImageView::zoom_level() const { return log(m_zoom * screen()->pixel_ratio()) / log(m_zoom_sensitivity); }

void HDRImageView::set_zoom_level(float level)
{
    m_zoom = ::clamp(std::pow(m_zoom_sensitivity, level) / screen()->pixel_ratio(), MIN_ZOOM, MAX_ZOOM);
    m_zoom_callback(m_zoom);
}

void HDRImageView::zoom_by(float amount, const Vector2f &focus_pos)
{
    auto  focused_pixel = pixel_at_position(focus_pos);
    float scale_factor  = std::pow(m_zoom_sensitivity, amount);
    m_zoom              = ::clamp(scale_factor * m_zoom, MIN_ZOOM, MAX_ZOOM);
    set_pixel_at_position(focus_pos, focused_pixel);

    m_zoom_callback(m_zoom);
}

void HDRImageView::zoom_in()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = Vector2f(size_f() / 2.f);
    auto center_pixel = pixel_at_position(center_pos);

    // determine next higher power of 2 zoom level
    float level_for_sensitivity = std::ceil(log(m_zoom) / log(2.f) + 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = std::clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    set_pixel_at_position(center_pos, center_pixel);

    m_zoom_callback(m_zoom);
}

void HDRImageView::zoom_out()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = Vector2f(size_f() / 2.f);
    auto center_pixel = pixel_at_position(center_pos);

    // determine next lower power of 2 zoom level
    float level_for_sensitivity = std::floor(log(m_zoom) / log(2.f) - 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = std::clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    set_pixel_at_position(center_pos, center_pixel);

    m_zoom_callback(m_zoom);
}

bool HDRImageView::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    if (!m_enabled || !m_current_image)
        return false;

    if (m_mouse_callback)
        return m_mouse_callback(p, button, down, modifiers);

    return false;
}

bool HDRImageView::mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    // if (!m_enabled || !m_current_image)
    //     return false;

    spdlog::trace("image_view motion: {}; {}; {}", p, modifiers & GLFW_MOD_ALT, modifiers);

    if (m_motion_callback)
        return m_motion_callback(p, rel, button, modifiers);

    return false;
}

bool HDRImageView::mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    if (!m_enabled || !m_current_image)
        return false;

    return m_drag_callback(p, rel, button, modifiers);
}

bool HDRImageView::scroll_event(const Vector2i &p, const Vector2f &rel)
{
    if (Canvas::scroll_event(p, rel))
        return true;

    // query glfw directly to check if a modifier key is pressed
    int l_state = glfwGetKey(screen()->glfw_window(), GLFW_KEY_LEFT_SHIFT);
    int r_state = glfwGetKey(screen()->glfw_window(), GLFW_KEY_RIGHT_SHIFT);

    if (l_state == GLFW_PRESS || r_state == GLFW_PRESS)
    {
        // panning
        set_pixel_at_position(Vector2f(p) + rel * 4.f, pixel_at_position(p));
        return true;
    }
    else // if (screen()->modifiers() == 0)
    {
        // zooming
        float v = rel.y();
        if (std::abs(v) < 1)
            v = std::copysign(1.f, v);
        zoom_by(v / 4.f, p - position());

        return true;
    }

    return false;
}

void HDRImageView::add_shortcuts(HelpWindow *w)
{
    auto section_name = "Image view";
    w->add_shortcut(section_name, "Left Click+Drag / Shift+Scroll", "Pan image");
    w->add_shortcut(section_name, "Scroll", "Zoom In and Out Continuously");
}

bool HDRImageView::keyboard_event(int key, int /* scancode */, int action, int modifiers)
{
    if (!m_enabled || !m_current_image)
        return false;

    if (action != GLFW_PRESS)
        return false;

    switch (key)
    {
    case '=':
    case GLFW_KEY_KP_ADD:
        spdlog::trace("KEY `=` pressed");
        zoom_in();
        return true;

    case '-':
    case GLFW_KEY_KP_SUBTRACT:
        spdlog::trace("KEY `-` pressed");
        zoom_out();
        return true;
    }

    return false;
}

void HDRImageView::draw(NVGcontext *ctx)
{
    if (size().x() <= 1 || size().y() <= 1)
        return;

    Canvas::draw(ctx); // calls HDRImageView draw_contents

    if (m_current_image)
    {
        draw_image_border(ctx);
        draw_pixel_grid(ctx);
        draw_pixel_info(ctx);
    }

    if (m_draw_callback)
    {
        nvgTranslate(ctx, m_pos.x(), m_pos.y());
        m_draw_callback(ctx);
        nvgTranslate(ctx, -m_pos.x(), -m_pos.y());
    }

    draw_widget_border(ctx);
}

void HDRImageView::draw_image_border(NVGcontext *ctx) const
{
    if (!m_current_image || squared_norm(m_current_image->size()) == 0)
        return;

    int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

    Vector2i border_pos = m_pos + Vector2i(m_offset + center_offset(m_current_image));
    Vector2i border_size(scaled_image_size_f(m_current_image));

    if (m_reference_image && squared_norm(m_reference_image->size()) > 0)
    {
        border_pos  = min(border_pos, m_pos + Vector2i(m_offset + center_offset(m_reference_image)));
        border_size = max(border_size, Vector2i(scaled_image_size_f(m_reference_image)));
    }

    // Draw a drop shadow
    NVGpaint shadowPaint = nvgBoxGradient(ctx, border_pos.x(), border_pos.y(), border_size.x(), border_size.y(), cr * 2,
                                          ds * 2, m_theme->m_drop_shadow, m_theme->m_transparent);

    nvgSave(ctx);
    nvgBeginPath(ctx);
    nvgScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
    nvgRect(ctx, border_pos.x() - ds, border_pos.y() - ds, border_size.x() + 2 * ds, border_size.y() + 2 * ds);
    nvgRoundedRect(ctx, border_pos.x(), border_pos.y(), border_size.x(), border_size.y(), cr);
    nvgPathWinding(ctx, NVG_HOLE);
    nvgFillPaint(ctx, shadowPaint);
    nvgFill(ctx);
    nvgRestore(ctx);

    // draw a line border
    nvgSave(ctx);
    nvgBeginPath(ctx);
    nvgScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
    nvgStrokeWidth(ctx, 1.0f);
    nvgRect(ctx, border_pos.x(), border_pos.y(), border_size.x(), border_size.y());
    nvgStrokeColor(ctx, Color(0.5f, 0.5f, 0.5f, 1.0f));
    nvgStroke(ctx);
    nvgResetScissor(ctx);
    nvgRestore(ctx);
}

void HDRImageView::draw_pixel_grid(NVGcontext *ctx) const
{
    if (!m_current_image)
        return;

    if (!m_draw_grid || (m_grid_threshold == -1) || (m_zoom <= m_grid_threshold))
        return;

    float factor = clamp01((m_zoom - m_grid_threshold) / (2 * m_grid_threshold));
    float alpha  = lerp(0.0f, 0.2f, smoothstep(0.0f, 1.0f, factor));

    if (alpha > 0.0f)
    {
        Vector2f xy0  = screen_position_at_pixel(Vector2f(0.0f));
        int      minJ = max(0, int(-xy0.y() / m_zoom));
        int      maxJ = min(m_current_image->size().y(), int(ceil((screen()->size().y() - xy0.y()) / m_zoom)));
        int      minI = max(0, int(-xy0.x() / m_zoom));
        int      maxI = min(m_current_image->size().x(), int(ceil((screen()->size().x() - xy0.x()) / m_zoom)));

        nvgBeginPath(ctx);

        // draw vertical lines
        for (int i = minI; i <= maxI; ++i)
        {
            Vector2f sxy0 = screen_position_at_pixel(Vector2f(i, minJ));
            Vector2f sxy1 = screen_position_at_pixel(Vector2f(i, maxJ));
            nvgMoveTo(ctx, sxy0.x(), sxy0.y());
            nvgLineTo(ctx, sxy1.x(), sxy1.y());
        }

        // draw horizontal lines
        for (int j = minJ; j <= maxJ; ++j)
        {
            Vector2f sxy0 = screen_position_at_pixel(Vector2f(minI, j));
            Vector2f sxy1 = screen_position_at_pixel(Vector2f(maxI, j));
            nvgMoveTo(ctx, sxy0.x(), sxy0.y());
            nvgLineTo(ctx, sxy1.x(), sxy1.y());
        }

        nvgStrokeWidth(ctx, 2.0f);
        nvgStrokeColor(ctx, Color(1.0f, 1.0f, 1.0f, alpha));
        nvgStroke(ctx);
    }
}

void HDRImageView::draw_widget_border(NVGcontext *ctx) const
{
    int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

    if (m_size.x() <= ds || m_size.y() <= ds)
        return;

    // Draw an inner drop shadow. (adapted from Window) and tev
    NVGpaint shadowPaint = nvgBoxGradient(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr, ds,
                                          m_theme->m_transparent, m_theme->m_drop_shadow);

    nvgSave(ctx);
    nvgResetScissor(ctx);
    nvgBeginPath(ctx);
    nvgRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
    nvgRoundedRect(ctx, m_pos.x() + ds, m_pos.y() + ds, m_size.x() - 2 * ds, m_size.y() - 2 * ds, cr);
    nvgPathWinding(ctx, NVG_HOLE);
    nvgFillPaint(ctx, shadowPaint);
    nvgFill(ctx);
    nvgRestore(ctx);
}

void HDRImageView::draw_pixel_info(NVGcontext *ctx) const
{
    if (!m_draw_pixel_info || (m_pixel_info_threshold == -1) || (m_zoom <= m_pixel_info_threshold))
        return;

    float factor = clamp01((m_zoom - m_pixel_info_threshold) / (2 * m_pixel_info_threshold));
    float alpha  = lerp(0.0f, 0.5f, smoothstep(0.0f, 1.0f, factor));

    if (alpha > 0.0f && m_pixel_callback)
    {
        nvgSave(ctx);
        nvgIntersectScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());

        Vector2f xy0  = screen_position_at_pixel(Vector2f(0.0f));
        int      minJ = max(0, int(-xy0.y() / m_zoom));
        int      maxJ = min(m_current_image->size().y() - 1, int(ceil((screen()->size().y() - xy0.y()) / m_zoom)));
        int      minI = max(0, int(-xy0.x() / m_zoom));
        int      maxI = min(m_current_image->size().x() - 1, int(ceil((screen()->size().x() - xy0.x()) / m_zoom)));

        float font_size = m_zoom / 31.0f * 7;
        nvgFontFace(ctx, "sans");
        nvgFontSize(ctx, font_size);
        nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        constexpr size_t bsize = 20;
        char text_buf[4 * bsize], *text[4] = {text_buf, text_buf + bsize, text_buf + 2 * bsize, text_buf + 3 * bsize};

        for (int j = minJ; j <= maxJ; ++j)
        {
            for (int i = minI; i <= maxI; ++i)
            {
                m_pixel_callback(Vector2i(i, j), text, bsize);

                auto pos = screen_position_at_pixel(Vector2f(i + 0.5f, j + 0.5f));

                for (int ch = 0; ch < 4; ++ch)
                {
                    Color col(0.f, 0.f, 0.f, alpha);
                    nvgFillColor(ctx, col);
                    nvgFontBlur(ctx, 20);
                    float xpos = pos.x(), ypos = pos.y() + (ch - 1.5f) * font_size;
                    nvgText(ctx, xpos, ypos, text[ch], nullptr);
                    col = Color(0.3f, 0.3f, 0.3f, alpha);
                    if (ch == 3)
                        col[0] = col[1] = col[2] = 1.f;
                    else
                        col[ch] = 1.f;
                    nvgFillColor(ctx, col);
                    nvgFontBlur(ctx, 0);
                    nvgText(ctx, xpos, ypos, text[ch], nullptr);
                }
            }
        }

        nvgRestore(ctx);
    }
}

void HDRImageView::draw_ROI(NVGcontext *ctx) const
{
    if (m_current_image->roi().is_empty())
        return;

    double time = glfwGetTime();
    int    w, h;
    auto   stripes = hdrview_image_icon(ctx, stripe7, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_NEAREST);
    nvgImageSize(ctx, stripes, &w, &h);
    NVGpaint paint = nvgImagePattern(ctx, mod(time * 30, (double)w), 0, w, h, 0, stripes, m_enabled ? 1.0f : 0.25f);
    nvgStrokePaint(ctx, paint);

    nvgBeginPath(ctx);
    Vector2i tl          = screen_position_at_pixel(m_current_image->roi().min);
    Vector2i br          = screen_position_at_pixel(m_current_image->roi().max);
    Vector2i border_size = br - tl;
    nvgRect(ctx, tl.x(), tl.y(), border_size.x(), border_size.y());
    nvgStrokeWidth(ctx, 1.0f);
    nvgStroke(ctx);
}

void HDRImageView::draw_contents()
{
    if (m_current_image && size().x() > 0 && size().y() > 0)
    {
        Vector2f randomness(std::generate_canonical<float, 10>(g_rand) * 255,
                            std::generate_canonical<float, 10>(g_rand) * 255);

        m_image_shader->set_uniform("randomness", randomness);
        m_image_shader->set_uniform("gain", powf(2.0f, m_exposure));
        m_image_shader->set_uniform("gamma", m_gamma);
        m_image_shader->set_uniform("sRGB", m_sRGB);
        m_image_shader->set_uniform("clamp_to_LDR", m_clamp_to_LDR || !screen()->has_float_buffer());
        m_image_shader->set_uniform("do_dither", m_dither);

        Vector2f curr_pos, curr_scale;
        image_position_and_scale(curr_pos, curr_scale, m_current_image);
        m_image_shader->set_uniform("primary_pos", curr_pos);
        m_image_shader->set_uniform("primary_scale", curr_scale);

        m_image_shader->set_uniform("blend_mode", (int)m_blend_mode);
        m_image_shader->set_uniform("channel", (int)m_channel);
        m_image_shader->set_uniform("bg_mode", (int)m_bg_mode);
        m_image_shader->set_uniform("bg_color", m_bg_color);

        if (m_reference_image)
        {
            Vector2f ref_pos, ref_scale;
            image_position_and_scale(ref_pos, ref_scale, m_reference_image);
            m_image_shader->set_uniform("has_reference", true);
            m_image_shader->set_uniform("secondary_pos", ref_pos);
            m_image_shader->set_uniform("secondary_scale", ref_scale);
        }
        else
        {
            m_image_shader->set_uniform("has_reference", false);
            m_image_shader->set_uniform("secondary_pos", Vector2f(1.f, 1.f));
            m_image_shader->set_uniform("secondary_scale", Vector2f(1.f, 1.f));
        }

        m_image_shader->begin();
        m_image_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
        m_image_shader->end();
    }
}
