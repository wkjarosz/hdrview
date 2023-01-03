//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "tool.h"
#include "brush.h"
#include "hdrimageview.h"
#include "hdrviewscreen.h"
#include "helpwindow.h"
#include "hscrollpanel.h"
#include "imagelistpanel.h"
#include "menu.h"
#include "rasterdraw.h"
#include "well.h"
#include <hdrview_resources.h>
#include <nanogui/icons.h>
#include <nanogui/toolbutton.h>

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

#include <nanogui/opengl.h>

using namespace nanogui;
using namespace std;
using json = nlohmann::json;

Tool::Tool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel, const string &name,
           const string &tooltip, int icon, ETool tool) :
    m_name(name),
    m_tooltip(tooltip), m_icon(icon), m_tool(tool), m_screen(screen), m_image_view(image_view),
    m_images_panel(images_panel), m_button(nullptr), m_options(nullptr)
{
    // empty
}

nlohmann::json &Tool::all_tool_settings()
{
    if (!m_screen->settings().contains("tools") || !m_screen->settings()["tools"].is_object())
        m_screen->settings()["tools"] = json::object();
    return m_screen->settings()["tools"];
}

const nlohmann::json Tool::all_tool_settings() const
{
    json j = json::object();
    if (m_screen->settings().contains("tools") && m_screen->settings()["tools"].is_object())
        j = m_screen->settings()["tools"];
    return j;
}

nlohmann::json &Tool::this_tool_settings()
{
    if (!all_tool_settings().contains(m_name) || !all_tool_settings()[m_name].is_object())
        all_tool_settings()[m_name] = json::object();
    return all_tool_settings()[m_name];
}

const nlohmann::json Tool::this_tool_settings() const
{
    json j = json::object();
    if (all_tool_settings().contains(m_name) && all_tool_settings()[m_name].is_object())
        j = all_tool_settings()[m_name];
    return j;
}

void Tool::write_settings()
{
    // create a json object to hold the tool's settings
    // settings();
}

void Tool::create_toolbutton(Widget *toolbar)
{
    if (m_button)
        return;

    m_button = new ToolButton(toolbar, m_icon);
    m_button->set_fixed_size(Vector2i(0));
    m_button->set_flags(Button::Flags::RadioButton);
    m_button->set_callback([this] { m_screen->set_tool(m_tool); });
    m_button->set_tooltip(m_name + ": " + m_tooltip);
    m_button->set_icon_extra_scale(1.5f);
}

void Tool::create_menuitem(Dropdown *menu, int modifier, int button)
{
    if (m_menuitem)
        return;

    m_menuitem = menu->popup()->add<MenuItem>(m_name);
    m_menuitem->set_hotkey(modifier, button);
    m_menuitem->set_flags(Button::RadioButton);
    m_menuitem->set_change_callback(
        [this](bool b)
        {
            spdlog::info("changing tool item {} with parent {}", (void *)m_menuitem, (void *)m_menuitem->parent());
            m_screen->set_tool((ETool)m_tool);
            return true;
        });
}

void Tool::update_width(int w)
{
    spdlog::trace("update width");
    if (m_options)
        m_options->set_fixed_width(w);
    else
        spdlog::error("Options widget for {} never created.", m_name);
}

void Tool::draw_crosshairs(NVGcontext *ctx, const Vector2i &p) const
{
    nvgLineCap(ctx, NVG_ROUND);
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, p.x() - 5, p.y());
    nvgLineTo(ctx, p.x() + 5, p.y());

    nvgMoveTo(ctx, p.x(), p.y() - 5);
    nvgLineTo(ctx, p.x(), p.y() + 5);

    nvgStrokeColor(ctx, Color(0, 255));
    nvgStrokeWidth(ctx, 2.f);
    nvgStroke(ctx);

    nvgStrokeColor(ctx, Color(255, 255));
    nvgStrokeWidth(ctx, 1.f);
    nvgStroke(ctx);
}

void Tool::draw(NVGcontext *ctx) const
{
    auto img = m_images_panel->current_image();
    if (!img || img->roi().is_empty())
        return;

    double time = glfwGetTime();
    int    w, h;
    auto   stripes = hdrview_image_icon(ctx, stripe7, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_NEAREST);
    nvgImageSize(ctx, stripes, &w, &h);
    NVGpaint paint = nvgImagePattern(ctx, mod(time * 30, (double)w), 0, w, h, 0, stripes, 1.0f);
    nvgStrokePaint(ctx, paint);

    nvgBeginPath(ctx);
    Vector2i tl          = m_image_view->position_at_pixel(img->roi().min);
    Vector2i br          = m_image_view->position_at_pixel(img->roi().max);
    Vector2i border_size = br - tl;
    nvgRect(ctx, tl.x(), tl.y(), border_size.x(), border_size.y());
    nvgStrokeWidth(ctx, 1.0f);
    nvgStroke(ctx);
}

bool Tool::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    // empty
    return false;
}

bool Tool::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    m_image_view->set_pixel_at_position(p + rel, m_image_view->pixel_at_position(p));
    return false;
}

void Tool::add_shortcuts(HelpWindow *w) { return; }

bool Tool::keyboard(int key, int scancode, int action, int modifiers) { return false; }

HandTool::HandTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel, const string &name,
                   const string &tooltip, int icon, ETool tool) :
    Tool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

void HandTool::create_options_bar(nanogui::Widget *parent)
{
    if (m_options)
        return;

    bool  sRGB     = m_image_view->sRGB();
    float gamma    = m_image_view->gamma();
    float exposure = m_image_view->exposure();

    m_options    = new HScrollPanel(parent);
    auto content = new Widget(m_options);
    content->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 5, 5));

    new Label(content, "EV:");
    auto exposure_slider  = new Slider(content);
    auto exposure_textbox = new FloatBox<float>(content, exposure);
    auto normalize_button = new Button(content, "", FA_MAGIC);
    normalize_button->set_fixed_size(nanogui::Vector2i(19, 19));
    normalize_button->set_icon_extra_scale(1.15f);
    normalize_button->set_callback(
        [this]()
        {
            m_image_view->normalize_exposure();
            m_images_panel->request_histogram_update(true);
        });
    normalize_button->set_tooltip("Normalize exposure.");

    auto reset_button = new Button(content, "", FA_UNDO);
    reset_button->set_fixed_size(nanogui::Vector2i(19, 19));
    reset_button->set_icon_extra_scale(1.15f);
    reset_button->set_callback(
        [this]()
        {
            m_image_view->reset_tonemapping();
            m_images_panel->request_histogram_update(true);
        });
    reset_button->set_tooltip("Reset tonemapping.");

    auto sRGB_checkbox = new CheckBox(content, "sRGB");
    auto gamma_label   = new Label(content, "Gamma:");
    auto gamma_slider  = new Slider(content);
    auto gamma_textbox = new FloatBox<float>(content);

    exposure_textbox->number_format("%1.2f");
    exposure_textbox->set_editable(true);
    exposure_textbox->set_spinnable(true);
    exposure_textbox->set_fixed_width(50);
    exposure_textbox->set_min_value(-9.0f);
    exposure_textbox->set_max_value(9.0f);
    exposure_textbox->set_alignment(TextBox::Alignment::Right);
    exposure_textbox->set_callback([this](float e) { m_image_view->set_exposure(e); });
    exposure_slider->set_callback([this](float v) { m_image_view->set_exposure(round(4 * v) / 4.0f); });
    exposure_slider->set_final_callback(
        [this](float v)
        {
            m_image_view->set_exposure(round(4 * v) / 4.0f);
            m_images_panel->request_histogram_update(true);
        });
    exposure_slider->set_fixed_width(100);
    exposure_slider->set_range({-9.0f, 9.0f});
    exposure_textbox->set_value(exposure);

    gamma_textbox->set_editable(true);
    gamma_textbox->set_spinnable(true);
    gamma_textbox->number_format("%1.3f");
    gamma_textbox->set_fixed_width(55);
    gamma_textbox->set_min_value(0.02f);
    gamma_textbox->set_max_value(9.0f);

    gamma_textbox->set_alignment(TextBox::Alignment::Right);
    gamma_textbox->set_callback(
        [this, gamma_slider](float value)
        {
            m_image_view->set_gamma(value);
            gamma_slider->set_value(value);
        });
    gamma_slider->set_callback(
        [&, gamma_slider, gamma_textbox](float value)
        {
            float g = max(gamma_slider->range().first, round(10 * value) / 10.0f);
            m_image_view->set_gamma(g);
            gamma_textbox->set_value(g);
            gamma_slider->set_value(g); // snap values
        });
    gamma_slider->set_fixed_width(100);
    gamma_slider->set_range({0.02f, 9.0f});
    gamma_slider->set_value(gamma);
    gamma_textbox->set_value(gamma);

    m_image_view->set_exposure_callback(
        [this, exposure_textbox, exposure_slider](float e)
        {
            exposure_textbox->set_value(e);
            exposure_slider->set_value(e);
            m_images_panel->request_histogram_update();
        });
    m_image_view->set_gamma_callback(
        [gamma_textbox, gamma_slider](float g)
        {
            gamma_textbox->set_value(g);
            gamma_slider->set_value(g);
        });
    m_image_view->set_sRGB_callback(
        [sRGB_checkbox, gamma_textbox, gamma_slider](bool b)
        {
            sRGB_checkbox->set_checked(b);
            gamma_textbox->set_enabled(!b);
            gamma_textbox->set_spinnable(!b);
            gamma_slider->set_enabled(!b);
        });
    m_image_view->set_exposure(exposure);
    m_image_view->set_gamma(gamma);

    sRGB_checkbox->set_callback(
        [&, this, gamma_slider, gamma_textbox, gamma_label](bool value)
        {
            m_image_view->set_sRGB(value);
            gamma_slider->set_enabled(!value);
            gamma_textbox->set_spinnable(!value);
            gamma_textbox->set_enabled(!value);
            gamma_label->set_enabled(!value);
            gamma_label->set_color(value ? m_screen->theme()->m_disabled_text_color : m_screen->theme()->m_text_color);
            m_screen->request_layout_update();
        });

    sRGB_checkbox->set_checked(sRGB);
    sRGB_checkbox->callback()(sRGB);
    sRGB_checkbox->set_tooltip("Use the sRGB non-linear response curve (instead of inverse power gamma correction).");

    (new CheckBox(content, "Dither", [this](bool v) { m_image_view->set_dithering(v); }))
        ->set_checked(m_image_view->dithering_on());
    (new CheckBox(content, "Grid", [this](bool v) { m_image_view->set_draw_grid(v); }))
        ->set_checked(m_image_view->draw_grid_on());
    (new CheckBox(content, "RGB values", [this](bool v) { m_image_view->set_draw_pixel_info(v); }))
        ->set_checked(m_image_view->draw_pixel_info_on());
}

RectangularMarquee::RectangularMarquee(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel,
                                       const string &name, const string &tooltip, int icon, ETool tool) :
    Tool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

bool RectangularMarquee::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    auto img = m_images_panel->current_image();
    if (down)
    {
        auto ic       = m_image_view->pixel_at_position(p - m_image_view->position());
        m_roi_clicked = Vector2i(round(ic.x()), round(ic.y()));
        img->roi()    = Box2i(img->box().clamp(m_roi_clicked));
    }
    else
    {
        if (!img->roi().has_volume())
            img->roi() = Box2i();
    }
    return true;
}

bool RectangularMarquee::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    auto     ic = m_image_view->pixel_at_position(p - m_image_view->position());
    Vector2i drag_pixel(round(ic.x()), round(ic.y()));

    auto   img = m_images_panel->current_image();
    Box2i &roi = img->roi();
    roi        = Box2i(img->box().clamp(m_roi_clicked));
    roi.enclose(drag_pixel);
    roi.intersect(img->box());

    return true;
}

BrushTool::BrushTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel, const string &name,
                     const string &tooltip, int icon, ETool tool) :
    Tool(screen, image_view, images_panel, name, tooltip, icon, tool),
    m_brush(make_shared<Brush>(80)), m_p0(std::numeric_limits<int>::lowest()), m_p1(std::numeric_limits<int>::lowest()),
    m_p2(std::numeric_limits<int>::lowest()), m_p3(std::numeric_limits<int>::lowest())
{
    // empty
}

void BrushTool::write_settings()
{
    // create a json object to hold the tool's settings
    auto &settings        = this_tool_settings();
    settings["size"]      = m_brush->radius();
    settings["hardness"]  = m_brush->hardness();
    settings["flow"]      = m_brush->flow();
    settings["angle"]     = m_brush->angle();
    settings["roundness"] = m_brush->roundness();
    settings["spacing"]   = m_brush->spacing();
    settings["smoothing"] = m_smoothing_checkbox->checked();
}

bool BrushTool::is_valid(const Vector2i &p) const { return p.x() != std::numeric_limits<int>::lowest(); }

void BrushTool::create_options_bar(nanogui::Widget *parent)
{
    if (m_options)
        return;

    auto &settings = this_tool_settings();

    m_brush->set_radius(settings.value("size", 15));
    m_brush->set_hardness(settings.value("hardness", 0.f));
    m_brush->set_flow(settings.value("flow", 1.f));
    m_brush->set_angle(settings.value("angle", 0.f));
    m_brush->set_roundness(settings.value("roundness", 1.f));
    m_brush->set_spacing(settings.value("spacing", 0.f));

    m_options = new HScrollPanel(parent);
    m_options->set_visible(false);
    auto content = new Widget(m_options);
    content->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 5, 0));

    content->add<Label>("Size:");
    m_size_slider  = new Slider(content);
    m_size_textbox = new IntBox<int>(content);

    m_size_textbox->set_editable(true);
    m_size_textbox->set_fixed_width(45);
    m_size_textbox->set_min_value(1);
    m_size_textbox->set_max_value(300);
    m_size_textbox->set_units("px");
    m_size_textbox->set_alignment(TextBox::Alignment::Right);
    m_size_textbox->set_callback(
        [this](int v)
        {
            m_brush->set_radius(v);
            m_size_slider->set_value(v);
        });
    m_size_slider->set_fixed_width(75);
    m_size_slider->set_range({1, 300});
    m_size_slider->set_callback(
        [this](int v)
        {
            m_brush->set_radius(v);
            m_size_textbox->set_value(v);
        });

    m_size_textbox->set_value(m_brush->radius());
    m_size_slider->set_value(m_brush->radius());

    // spacer
    content->add<Widget>()->set_fixed_width(5);

    content->add<Label>("Hard:");
    m_hardness_slider  = new Slider(content);
    m_hardness_textbox = new FloatBox<float>(content);

    m_hardness_textbox->number_format("%3.0f");
    m_hardness_textbox->set_editable(true);
    m_hardness_textbox->set_fixed_width(40);
    m_hardness_textbox->set_min_value(0.f);
    m_hardness_textbox->set_max_value(100.f);
    m_hardness_textbox->set_units("%");
    m_hardness_textbox->set_alignment(TextBox::Alignment::Right);
    m_hardness_textbox->set_callback(
        [this](int v)
        {
            m_brush->set_hardness(v / 100.f);
            m_hardness_slider->set_value(v);
        });
    m_hardness_slider->set_fixed_width(75);
    m_hardness_slider->set_range({0.f, 100.f});
    m_hardness_slider->set_callback(
        [this](int v)
        {
            m_brush->set_hardness(v / 100.f);
            m_hardness_textbox->set_value(v);
        });

    m_hardness_textbox->set_value(m_brush->hardness() * 100.f);
    m_hardness_slider->set_value(m_brush->hardness() * 100.f);

    // spacer
    content->add<Widget>()->set_fixed_width(5);

    content->add<Label>("Flow:");
    m_flow_slider  = new Slider(content);
    m_flow_textbox = new FloatBox<float>(content);

    m_flow_textbox->number_format("%3.0f");
    m_flow_textbox->set_editable(true);
    m_flow_textbox->set_fixed_width(40);
    m_flow_textbox->set_min_value(0.5f);
    m_flow_textbox->set_max_value(100.f);
    m_flow_textbox->set_units("%");
    m_flow_textbox->set_alignment(TextBox::Alignment::Right);
    m_flow_textbox->set_callback(
        [this](int v)
        {
            m_brush->set_flow(v / 100.f);
            m_flow_slider->set_value(v);
        });
    m_flow_slider->set_fixed_width(75);
    m_flow_slider->set_range({0.5f, 100.f});
    m_flow_slider->set_callback(
        [this](int v)
        {
            m_brush->set_flow(v / 100.f);
            m_flow_textbox->set_value(v);
        });

    m_flow_textbox->set_value(m_brush->flow() * 100.f);
    m_flow_slider->set_value(m_brush->flow() * 100.f);

    // spacer
    content->add<Widget>()->set_fixed_width(5);

    content->add<Label>("Angle:");
    m_angle_slider  = new Slider(content);
    m_angle_textbox = new FloatBox<float>(content);

    m_angle_textbox->number_format("%3.0f");
    m_angle_textbox->set_editable(true);
    m_angle_textbox->set_fixed_width(35);
    m_angle_textbox->set_min_value(0.f);
    m_angle_textbox->set_max_value(180.f);
    m_angle_textbox->set_units("°");
    m_angle_textbox->set_alignment(TextBox::Alignment::Right);
    m_angle_textbox->set_callback(
        [this](int v)
        {
            m_brush->set_angle(v);
            m_angle_slider->set_value(v);
        });
    m_angle_slider->set_fixed_width(75);
    m_angle_slider->set_range({0.f, 180.f});
    m_angle_slider->set_callback(
        [this](int v)
        {
            m_brush->set_angle(v);
            m_angle_textbox->set_value(v);
        });

    m_angle_textbox->set_value(m_brush->angle());
    m_angle_slider->set_value(m_brush->angle());

    // spacer
    content->add<Widget>()->set_fixed_width(5);

    content->add<Label>("Round:");
    m_roundness_slider  = new Slider(content);
    m_roundness_textbox = new FloatBox<float>(content);

    m_roundness_textbox->number_format("%3.0f");
    m_roundness_textbox->set_editable(true);
    m_roundness_textbox->set_fixed_width(40);
    m_roundness_textbox->set_min_value(0.5f);
    m_roundness_textbox->set_max_value(100.f);
    m_roundness_textbox->set_units("%");
    m_roundness_textbox->set_alignment(TextBox::Alignment::Right);
    m_roundness_textbox->set_callback(
        [this](int v)
        {
            m_brush->set_roundness(v / 100.f);
            m_roundness_slider->set_value(v);
        });
    m_roundness_slider->set_fixed_width(75);
    m_roundness_slider->set_range({0.5f, 100.f});
    m_roundness_slider->set_callback(
        [this](int v)
        {
            m_brush->set_roundness(v / 100.f);
            m_roundness_textbox->set_value(v);
        });

    m_roundness_textbox->set_value(m_brush->roundness() * 100.f);
    m_roundness_slider->set_value(m_brush->roundness() * 100.f);

    // spacer
    content->add<Widget>()->set_fixed_width(5);

    content->add<Label>("Spacing:");
    m_spacing_slider  = new Slider(content);
    m_spacing_textbox = new FloatBox<float>(content);

    m_spacing_textbox->number_format("%3.0f");
    m_spacing_textbox->set_editable(true);
    m_spacing_textbox->set_fixed_width(40);
    m_spacing_textbox->set_min_value(0.f);
    m_spacing_textbox->set_max_value(100.f);
    m_spacing_textbox->set_units("%");
    m_spacing_textbox->set_alignment(TextBox::Alignment::Right);
    m_spacing_textbox->set_callback(
        [this](int v)
        {
            m_brush->set_spacing(v / 100.f);
            m_spacing_slider->set_value(v);
        });
    m_spacing_slider->set_fixed_width(75);
    m_spacing_slider->set_range({0.f, 100.f});
    m_spacing_slider->set_callback(
        [this](int v)
        {
            m_brush->set_spacing(v / 100.f);
            m_spacing_textbox->set_value(v);
        });

    m_spacing_textbox->set_value(m_brush->spacing() * 100.f);
    m_spacing_slider->set_value(m_brush->spacing() * 100.f);

    // spacer
    content->add<Widget>()->set_fixed_width(5);

    m_smoothing_checkbox = new CheckBox(content, "Smoothing");
    m_smoothing_checkbox->set_callback([this](bool b) { m_smoothing = b; });
    m_smoothing = settings.value("smoothing", true);
    m_smoothing_checkbox->set_checked(m_smoothing);
}

void BrushTool::add_shortcuts(HelpWindow *w)
{
    auto section_name = "Brush tools";
    if (!w->add_section(section_name))
        return;

    w->add_shortcut(section_name, "[ / ]", "Decrease/Increase brush radius");
    w->add_shortcut(section_name, "H / Shift+H", "Decrease/Increase brush hardness");
    w->add_shortcut(section_name, "F / Shift+F", "Decrease/Increase brush flow rate");
    w->add_shortcut(section_name, "R / Shift+R", "Decrease/Increase brush roundness");
    w->add_shortcut(section_name, "A / Shift+A", "Decrease/Increase brush angle");
}

bool BrushTool::keyboard(int key, int scancode, int action, int modifiers)
{
    if (action == GLFW_RELEASE)
        return false;

    // handle the no-modifier or shift-modified shortcuts
    if (modifiers == 0 || modifiers == GLFW_MOD_SHIFT)
    {
        switch (key)
        {
        case '[':
        {
            spdlog::trace("Key `[` pressed");
            int dr = std::min(-1, (int)ceil(m_brush->radius() / 1.1 - m_brush->radius()));
            int r  = std::clamp(m_brush->radius() + dr, 1, (int)m_size_slider->range().second);
            m_brush->set_radius(r);
            m_size_textbox->set_value(m_brush->radius());
            m_size_slider->set_value(m_brush->radius());
            return true;
        }

        case ']':
        {
            spdlog::trace("Key `]` pressed");
            int dr = std::max(1, (int)ceil(m_brush->radius() * 1.1 - m_brush->radius()));
            int r  = std::clamp(m_brush->radius() + dr, 1, (int)m_size_slider->range().second);
            m_brush->set_radius(r);
            m_size_textbox->set_value(m_brush->radius());
            m_size_slider->set_value(m_brush->radius());
            return true;
        }

        case 'A':
            if (modifiers & GLFW_MOD_SHIFT)
            {
                spdlog::trace("Key `A` pressed");
                m_brush->set_angle(mod(m_brush->angle() + 5.f, 180.f));
                m_angle_textbox->set_value(m_brush->angle());
                m_angle_slider->set_value(m_brush->angle());
            }
            else
            {
                spdlog::trace("Key `a` pressed");
                m_brush->set_angle(mod(m_brush->angle() - 5.f, 180.f));
                m_angle_textbox->set_value(m_brush->angle());
                m_angle_slider->set_value(m_brush->angle());
            }
            return true;

        case 'R':
            if (modifiers & GLFW_MOD_SHIFT)
            {
                spdlog::trace("Key `R` pressed");
                m_brush->set_roundness(m_brush->roundness() + 0.05f);
                m_roundness_textbox->set_value(m_brush->roundness() * 100);
                m_roundness_slider->set_value(m_brush->roundness() * 100);
            }
            else
            {
                spdlog::trace("Key `r` pressed");
                m_brush->set_roundness(m_brush->roundness() - 0.05f);
                m_roundness_textbox->set_value(m_brush->roundness() * 100);
                m_roundness_slider->set_value(m_brush->roundness() * 100);
            }
            return true;

        case 'F':
            if (modifiers & GLFW_MOD_SHIFT)
            {
                spdlog::trace("Key `F` pressed");
                m_brush->set_flow(m_brush->flow() + 0.05f);
                m_flow_textbox->set_value(m_brush->flow() * 100);
                m_flow_slider->set_value(m_brush->flow() * 100);
            }
            else
            {
                spdlog::trace("Key `f` pressed");
                m_brush->set_flow(m_brush->flow() - 0.05f);
                m_flow_textbox->set_value(m_brush->flow() * 100);
                m_flow_slider->set_value(m_brush->flow() * 100);
            }
            return true;

        case 'H':
            if (modifiers & GLFW_MOD_SHIFT)
            {
                spdlog::trace("Key `H` pressed");
                m_brush->set_hardness(m_brush->hardness() + 0.05f);
                m_hardness_textbox->set_value(m_brush->hardness() * 100);
                m_hardness_slider->set_value(m_brush->hardness() * 100);
            }
            else
            {
                spdlog::trace("Key `h` pressed");
                m_brush->set_hardness(m_brush->hardness() - 0.05f);
                m_hardness_textbox->set_value(m_brush->hardness() * 100);
                m_hardness_slider->set_value(m_brush->hardness() * 100);
            }
            return true;
        }
    }

    return Tool::keyboard(key, scancode, action, modifiers);
}

void BrushTool::plot_pixel(const HDRImagePtr &img, int x, int y, float a, int modifiers) const
{
    Color4 fg =
        modifiers & GLFW_MOD_ALT ? m_screen->background()->exposed_color() : m_screen->foreground()->exposed_color();
    fg.a *= a;
    Color4 bg = (*img)(x, y);

    (*img)(x, y) = fg.over(bg);
}

void BrushTool::start_stroke(const Vector2i &pixel, const HDRImagePtr &new_image, const Box2i &roi, int modifiers) const
{
    m_brush->set_step(0);
    m_brush->stamp_onto(
        pixel.x(), pixel.y(),
        [this, new_image, modifiers](int x, int y, float a) { plot_pixel(new_image, x, y, a, modifiers); }, roi);
}

void BrushTool::draw_line(const Vector2i &from_pixel, const Vector2i &to_pixel, const HDRImagePtr &new_image,
                          const Box2i &roi, int modifiers) const
{
    auto splat = [this, new_image, modifiers, &roi](int x, int y)
    {
        m_brush->stamp_onto(
            x, y, [this, new_image, modifiers](int i, int j, float a) { plot_pixel(new_image, i, j, a, modifiers); },
            roi);
    };

    ::draw_line(from_pixel.x(), from_pixel.y(), to_pixel.x(), to_pixel.y(), splat);
}

void BrushTool::draw_curve(const Vector2i &a, const Vector2i &b, const Vector2i &c, const Vector2i &d,
                           const HDRImagePtr &new_image, const Box2i &roi, int modifiers, bool include_start,
                           bool include_end) const
{
    auto splat = [this, new_image, modifiers, &roi](int x, int y)
    {
        m_brush->stamp_onto(
            x, y, [this, new_image, modifiers](int i, int j, float a) { plot_pixel(new_image, i, j, a, modifiers); },
            roi);
    };

    ::draw_Yuksel_curve(a.x(), a.y(), b.x(), b.y(), c.x(), c.y(), d.x(), d.y(), splat, YukselType::Hybrid,
                        include_start, include_end);
}

void BrushTool::draw_curve(const Vector2i &a, const Vector2i &b, const Vector2i &c, const HDRImagePtr &new_image,
                           const Box2i &roi, int modifiers) const
{
    auto splat = [this, new_image, modifiers, &roi](int x, int y)
    {
        m_brush->stamp_onto(
            x, y, [this, new_image, modifiers](int i, int j, float a) { plot_pixel(new_image, i, j, a, modifiers); },
            roi);
    };

    ::draw_Yuksel_ellipse(a.x(), a.y(), b.x(), b.y(), c.x(), c.y(), splat);
}

bool BrushTool::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    spdlog::trace("modifier: {}", modifiers);

    Box2i roi = m_images_panel->current_image()->roi();
    if (roi.has_volume())
        roi.intersect(m_images_panel->current_image()->box());
    else
        roi = m_images_panel->current_image()->box();

    auto coord = m_image_view->pixel_at_position(p - m_image_view->position());

    // shift mouse location history
    m_p0 = m_p1;
    m_p1 = m_p2;
    m_p2 = m_p3;
    m_p3 = Vector2i(round(coord.x()), round(coord.y()));

    if (!down)
    {
        // draw the last part of the stroke
        if (!m_smoothing)
        {
            // just a line if we aren't smoothing
            m_images_panel->current_image()->direct_modify([this, &roi, modifiers](const HDRImagePtr &new_image)
                                                           { draw_line(m_p2, m_p3, new_image, roi, modifiers); });
        }
        else
        {
            // if we have 4 points to connect, then finish the C^2 curve
            if (is_valid(m_p0))
                m_images_panel->current_image()->direct_modify(
                    [this, &roi, modifiers](const HDRImagePtr &new_image)
                    { draw_curve(m_p0, m_p1, m_p2, m_p3, new_image, roi, modifiers, false, true); });
            else if (is_valid(m_p1))
            {
                // if we have 3 points to connect, then draw an ellipse connecting the points
                m_images_panel->current_image()->direct_modify(
                    [this, &roi, modifiers](const HDRImagePtr &new_image)
                    { draw_curve(m_p1, m_p2, m_p3, new_image, roi, modifiers); });
            }
            else if (is_valid(m_p2))
            {
                // if we have 2 points to connect, then draw an ellipse connecting the points
                m_images_panel->current_image()->direct_modify([this, &roi, modifiers](const HDRImagePtr &new_image)
                                                               { draw_line(m_p2, m_p3, new_image, roi, modifiers); });
            }
        }
    }
    else if (down && modifiers & GLFW_MOD_SHIFT)
    {
        // draw a straight line from the previously clicked point
        if (is_valid(m_p2))
        {
            m_images_panel->current_image()->start_modify(
                [this, &roi, modifiers](const ConstHDRImagePtr &img,
                                        const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    auto new_image = make_shared<HDRImage>(*img);
                    draw_line(m_p2, m_p3, new_image, roi, modifiers);
                    return {new_image, make_shared<FullImageUndo>(*img)};
                });
        }
    }
    else if (down)
    {
        m_images_panel->current_image()->start_modify(
            [this, &roi, modifiers](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto new_image = make_shared<HDRImage>(*img);
                start_stroke(m_p3, new_image, roi, modifiers);
                return {new_image, make_shared<FullImageUndo>(*img)};
            });
    }

    m_p0 = m_p1 = m_p2 = std::numeric_limits<int>::lowest();
    m_screen->request_layout_update();
    m_screen->update_caption();

    return true;
}

bool BrushTool::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    // // reduce number of mouse positions (useful for debugging the smoothing)
    if (m_smoothing)
    {
        static int skip = 0;
        if (skip++ % 2 != 0)
            return false;
    }

    m_screen->request_layout_update();
    auto coord      = m_image_view->pixel_at_position(p - m_image_view->position());
    auto pixel      = Vector2i(round(coord.x()), round(coord.y()));
    auto prev_coord = m_image_view->pixel_at_position(p - rel - m_image_view->position());
    auto prev_pixel = Vector2i(round(prev_coord.x()), round(prev_coord.y()));

    if (prev_pixel == pixel)
        return false;

    bool include_start = is_valid(m_p1) && !is_valid(m_p0);

    // shift mouse location history
    m_p0 = m_p1;
    m_p1 = m_p2;
    m_p2 = m_p3;
    m_p3 = pixel;

    m_images_panel->current_image()->direct_modify(
        [this, modifiers, include_start](const HDRImagePtr &new_image)
        {
            Box2i roi = m_images_panel->current_image()->roi();
            if (roi.has_volume())
                roi.intersect(new_image->box());
            else
                roi = new_image->box();

            if (!m_smoothing)
                draw_line(m_p2, m_p3, new_image, roi, modifiers);
            else if (is_valid(m_p0))
                draw_curve(m_p0, m_p1, m_p2, m_p3, new_image, roi, modifiers, include_start, false);
        });

    m_screen->update_caption();
    return true;
}

void BrushTool::draw(NVGcontext *ctx) const
{
    if (!m_images_panel->current_image())
        return;

    draw_brush(ctx, m_screen->mouse_pos() - m_image_view->absolute_position());

    Tool::draw(ctx);
}

void BrushTool::draw_brush(NVGcontext *ctx, const Vector2i &center) const
{
    nvgSave(ctx);
    nvgTranslate(ctx, center.x(), center.y());
    nvgRotate(ctx, 2 * M_PI * m_brush->angle() / 360.0f);
    nvgScale(ctx, 1.f, m_brush->roundness());

    nvgBeginPath(ctx);
    nvgCircle(ctx, 0, 0, m_brush->radius() * m_image_view->zoom());

    nvgStrokeColor(ctx, Color(0, 255));
    nvgStrokeWidth(ctx, 2.f);
    nvgStroke(ctx);

    nvgStrokeColor(ctx, Color(255, 255));
    nvgStrokeWidth(ctx, 1.f);
    nvgStroke(ctx);

    nvgRestore(ctx);
}

EraserTool::EraserTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel,
                       const string &name, const string &tooltip, int icon, ETool tool) :
    BrushTool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

void EraserTool::plot_pixel(const HDRImagePtr &img, int x, int y, float a, int modifiers) const
{
    float c        = modifiers & GLFW_MOD_ALT ? 1.f : 0.f;
    (*img)(x, y).a = c * a + (*img)(x, y).a * (1.0f - a);
}

CloneStampTool::CloneStampTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel,
                               const string &name, const string &tooltip, int icon, ETool tool) :
    BrushTool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

void CloneStampTool::plot_pixel(const HDRImagePtr &img, int dst_x, int dst_y, float a, int modifiers) const
{
    int src_x = dst_x + m_dpixel.x();
    int src_y = dst_y + m_dpixel.y();

    Color4 src_color(0.f);
    if (src_x >= 0 && src_y >= 0 && src_x < img->width() && src_y < img->height())
        src_color = (*img)(src_x, src_y);

    float alpha          = a * src_color.a;
    src_color.a          = 1.f;
    (*img)(dst_x, dst_y) = src_color * alpha + (*img)(dst_x, dst_y) * (1.0f - alpha);
}

bool CloneStampTool::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    if (modifiers & GLFW_MOD_ALT)
    {
        m_has_src   = true;
        m_src_pixel = m_image_view->pixel_at_position(p - m_image_view->position());
    }
    else if (down)
    {
        m_has_dst   = true;
        m_dst_pixel = m_image_view->pixel_at_position(p - m_image_view->position());
        m_dpixel    = m_src_pixel - m_dst_pixel;
    }
    else
        m_has_dst = false;

    return BrushTool::mouse_button(p, button, down, modifiers);
}

bool CloneStampTool::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    m_dpixel = m_src_pixel - m_dst_pixel;
    return BrushTool::mouse_drag(p, rel, button, modifiers);
}

void CloneStampTool::add_shortcuts(HelpWindow *w)
{
    auto section_name = m_name;
    w->add_shortcut(section_name, fmt::format("{0}+Click", HelpWindow::ALT), "Select source location");
    w->add_shortcut(section_name, " ", "All brush tool shortcuts");
}

bool CloneStampTool::keyboard(int key, int scancode, int action, int modifiers)
{
    m_modifier_down = (modifiers & GLFW_MOD_ALT) && !(action == GLFW_RELEASE);
    return BrushTool::keyboard(key, scancode, action, modifiers);
}

void CloneStampTool::draw(NVGcontext *ctx) const
{
    if (!m_images_panel->current_image())
        return;

    Vector2i cur_pixel = m_image_view->pixel_at_position(m_screen->mouse_pos() - m_image_view->position());

    if (m_has_src && !m_has_dst)
    {
        // draw brush with crosshairs at the source clicked point
        auto center = m_image_view->position_at_pixel(Vector2f(m_src_pixel) + 0.5f);
        draw_brush(ctx, center);
        draw_crosshairs(ctx, center);
    }
    else if (m_has_dst && m_has_src)
    {
        // draw brush with crosshairs at the source clicked point, offset by current drag amount
        auto center = m_image_view->position_at_pixel(Vector2f(cur_pixel - m_dst_pixel + m_src_pixel) + 0.5f);
        draw_brush(ctx, center);
        draw_crosshairs(ctx, center);
    }

    // draw brush and crosshairs centered at current mouse position
    draw_brush(ctx, m_image_view->position_at_pixel(Vector2f(cur_pixel) + 0.5f));
    if (m_modifier_down)
        draw_crosshairs(ctx, m_image_view->position_at_pixel(Vector2f(cur_pixel) + 0.5f));

    Tool::draw(ctx);
}

Eyedropper::Eyedropper(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel,
                       const string &name, const string &tooltip, int icon, ETool tool) :
    Tool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

void Eyedropper::write_settings()
{
    // create a json object to hold the tool's settings
    auto &settings   = this_tool_settings();
    settings["size"] = m_size;
}

void Eyedropper::create_options_bar(nanogui::Widget *parent)
{
    if (m_options)
        return;

    auto &settings = this_tool_settings();

    m_options = new HScrollPanel(parent);
    m_options->set_visible(false);
    auto content = new Widget(m_options);
    content->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 5, 5));

    new Label(content, "Sample size:");

    auto size = new Dropdown(content, {"Point sample", "3 × 3 average", "5 × 5 average", "7 × 7 average"});
    size->set_tooltip("The number of pixels sampled by the eyedropper.");
    size->set_selected_callback([this](int s) { m_size = s; });
    size->set_selected_index(std::clamp(settings.value("size", 0), 0, 3));
    size->set_fixed_height(19);
}

bool Eyedropper::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    auto img = m_images_panel->current_image();
    if (!img)
        return false;

    if (down)
    {
        const HDRImage &image = img->image();

        Color4 c_sum(0.f);
        int    w_sum = 0;
        for (int dx = -m_size; dx <= m_size; ++dx)
            for (int dy = -m_size; dy <= m_size; ++dy)
            {
                Vector2i pixel(m_image_view->pixel_at_position((p - m_image_view->position())));
                pixel += Vector2i(dx, dy);
                if (image.contains(pixel.x(), pixel.y()))
                {
                    c_sum += image(pixel.x(), pixel.y());
                    w_sum++;
                }
            }

        if (w_sum > 0)
        {
            if (modifiers & GLFW_MOD_ALT)
                m_screen->background()->set_color(c_sum / w_sum);
            else
                m_screen->foreground()->set_color(c_sum / w_sum);
        }

        return true;
    }
    return false;
}

bool Eyedropper::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    return mouse_button(p, button, true, modifiers);
}

void Eyedropper::draw(NVGcontext *ctx) const
{
    auto img = m_images_panel->current_image();
    if (!img)
        return;

    auto center = m_screen->mouse_pos() - m_image_view->absolute_position();

    Vector2i        center_pixel(m_image_view->pixel_at_position((center)));
    const HDRImage &image = img->image();

    if (image.contains(center.x(), center.y()))
    {
        Color4 c_sum(0.f);
        int    w_sum = 0;
        for (int dx = -m_size; dx <= m_size; ++dx)
            for (int dy = -m_size; dy <= m_size; ++dy)
            {
                Vector2i pixel = center_pixel + Vector2i(dx, dy);
                if (image.contains(pixel.x(), pixel.y()))
                {
                    c_sum += image(pixel.x(), pixel.y());
                    w_sum++;
                }
            }

        Color4 color_orig  = c_sum / w_sum;
        Color4 color_toned = m_image_view->tonemap(color_orig);
        // FIXME: the conversion operator doesn't seem to work on linux
        // Color ng_color(color_toned);
        Color ng_color(color_toned[0], color_toned[1], color_toned[2], color_toned[3]);

        nvgBeginPath(ctx);
        nvgCircle(ctx, center.x(), center.y(), 26);
        nvgFillColor(ctx, ng_color);
        nvgFill(ctx);

        nvgStrokeColor(ctx, Color(0, 255));
        nvgStrokeWidth(ctx, 2 + 1.f);
        nvgStroke(ctx);

        nvgStrokeColor(ctx, Color(192, 255));
        nvgStrokeWidth(ctx, 2);
        nvgStroke(ctx);

        nvgFontSize(ctx, m_image_view->font_size());
        nvgFontFace(ctx, "icons");
        nvgFillColor(ctx, ng_color.contrasting_color());
        nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);

        nvgText(ctx, center.x(), center.y(), utf8(FA_EYE_DROPPER).data(), nullptr);
    }

    Tool::draw(ctx);
}

Ruler::Ruler(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel, const string &name,
             const string &tooltip, int icon, ETool tool) :
    Tool(screen, image_view, images_panel, name, tooltip, icon, tool),
    m_start_pixel(std::numeric_limits<int>::lowest()), m_end_pixel(std::numeric_limits<int>::lowest())
{
    // empty
}

bool Ruler::is_valid(const Vector2i &p) const { return p.x() != std::numeric_limits<int>::lowest(); }

float Ruler::distance() const
{
    if (!is_valid(m_start_pixel) || !is_valid(m_end_pixel))
        return std::numeric_limits<float>::quiet_NaN();

    return norm(Vector2f(m_end_pixel - m_start_pixel));
}

float Ruler::angle() const
{
    if (!is_valid(m_start_pixel) || !is_valid(m_end_pixel))
        return std::numeric_limits<float>::quiet_NaN();

    auto to = Vector2f(m_end_pixel - m_start_pixel);
    return fabs(mod(nvgRadToDeg(-atan2(to.y(), to.x())), 360.0f));
}

bool Ruler::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    auto img = m_images_panel->current_image();
    if (!img)
        return false;

    if (down)
    {
        m_start_pixel   = m_image_view->pixel_at_position(p - m_image_view->position());
        m_end_pixel.x() = std::numeric_limits<int>::lowest();
        return true;
    }
    else if (is_valid(m_start_pixel))
    {
        m_end_pixel = m_image_view->pixel_at_position(p - m_image_view->position());
        if (modifiers & GLFW_MOD_SHIFT)
        {
            auto to           = m_end_pixel - m_start_pixel;
            int  axis         = abs(to.y()) < abs(to.x());
            m_end_pixel[axis] = m_start_pixel[axis];
        }
        return true;
    }

    return false;
}

bool Ruler::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    return mouse_button(p, button, false, modifiers);
}

void Ruler::draw(NVGcontext *ctx) const
{
    auto img = m_images_panel->current_image();
    if (!img)
        return;

    Vector2i start_pos(m_image_view->position_at_pixel(Vector2f(m_start_pixel) + 0.5f));
    if (is_valid(m_end_pixel))
    {
        Vector2i end_pos(m_image_view->position_at_pixel(Vector2f(m_end_pixel) + 0.5f));
        nvgBeginPath(ctx);
        nvgMoveTo(ctx, start_pos.x(), start_pos.y());
        nvgLineTo(ctx, end_pos.x(), end_pos.y());

        nvgStrokeColor(ctx, Color(0, 255));
        nvgStrokeWidth(ctx, 2.f);
        nvgStroke(ctx);

        nvgStrokeColor(ctx, Color(255, 255));
        nvgStrokeWidth(ctx, 1.f);
        nvgStroke(ctx);

        draw_crosshairs(ctx, end_pos);
    }

    draw_crosshairs(ctx, start_pos);

    Tool::draw(ctx);
}

LineTool::LineTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel, const string &name,
                   const string &tooltip, int icon, ETool tool) :
    Ruler(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

void LineTool::write_settings()
{
    // create a json object to hold the tool's settings
    auto &settings    = this_tool_settings();
    settings["width"] = m_width_slider->value();
}

void LineTool::create_options_bar(nanogui::Widget *parent)
{
    if (m_options)
        return;

    auto &settings = this_tool_settings();

    m_width = std::clamp(settings.value("width", 2.f), 1.f, 100.f);

    m_options = new HScrollPanel(parent);
    m_options->set_visible(false);
    auto content = new Widget(m_options);
    content->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 5, 5));

    content->add<Label>("Width:");
    m_width_slider  = new Slider(content);
    m_width_textbox = new FloatBox<float>(content);

    m_width_textbox->number_format("%3.1f");
    m_width_textbox->set_editable(true);
    m_width_textbox->set_spinnable(true);
    m_width_textbox->set_fixed_width(60);
    m_width_textbox->set_units("px");
    m_width_textbox->set_min_value(1.f);
    m_width_textbox->set_max_value(100);
    m_width_textbox->set_alignment(TextBox::Alignment::Right);
    m_width_textbox->set_callback(
        [this](float v)
        {
            m_width = v;
            m_width_slider->set_value(v);
        });
    m_width_slider->set_fixed_width(100);
    m_width_slider->set_range({1.f, 50.f});
    m_width_slider->set_callback(
        [this](float v)
        {
            m_width = v;
            m_width_textbox->set_value(v);
        });

    m_width_textbox->set_value(m_width);
    m_width_slider->set_value(m_width);
}

void LineTool::add_shortcuts(HelpWindow *w)
{
    auto section_name = m_name;
    w->add_shortcut(section_name, "[ / ]", "Decreasing/Increase line width");
}

bool LineTool::keyboard(int key, int scancode, int action, int modifiers)
{
    if (action == GLFW_RELEASE)
        return false;

    switch (key)
    {
    case '[':
    {
        spdlog::trace("Key `[` pressed");
        float dw = std::min(-1.f, std::ceil(m_width / 1.1f - m_width));
        float w  = std::clamp(m_width + dw, 1.f, m_width_slider->range().second);
        m_width  = w;
        m_width_textbox->set_value(m_width);
        m_width_slider->set_value(m_width);
        return true;
    }

    case ']':
        spdlog::trace("Key `]` pressed");
        float dw = std::max(1.f, std::ceil(m_width * 1.1f - m_width));
        float w  = std::clamp(m_width + dw, 1.f, m_width_slider->range().second);
        m_width  = w;
        m_width_textbox->set_value(m_width);
        m_width_slider->set_value(m_width);
        return true;
    }

    return Tool::keyboard(key, scancode, action, modifiers);
}

bool LineTool::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    Ruler::mouse_button(p, button, down, modifiers);

    auto color =
        modifiers & GLFW_MOD_ALT ? m_screen->background()->exposed_color() : m_screen->foreground()->exposed_color();

    Box2i roi = m_images_panel->current_image()->roi();
    if (roi.has_volume())
        roi.intersect(m_images_panel->current_image()->box());
    else
        roi = m_images_panel->current_image()->box();

    // draw a straight line from the previously clicked point
    if (!down)
    {
        m_images_panel->current_image()->start_modify(
            [&color, &roi, this](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto new_image = make_shared<HDRImage>(*img);

                auto put_pixel = [new_image, &color, &roi](int x, int y, float alpha)
                {
                    if (!roi.contains(Vector2i(x, y)))
                        return;

                    Color4 c(color.r(), color.g(), color.b(), color.a());
                    (*new_image)(x, y) = c * (1.0f - alpha) + (*new_image)(x, y) * alpha;
                };

                ::draw_line(m_start_pixel.x(), m_start_pixel.y(), m_end_pixel.x(), m_end_pixel.y(), (float)m_width,
                            put_pixel);

                return {new_image, make_shared<FullImageUndo>(*img)};
            });

        m_screen->update_caption();
    }

    m_dragging = down;

    return true;
}

bool LineTool::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    m_dragging = true;
    return Ruler::mouse_button(p, button, false, modifiers);
}

void LineTool::draw(NVGcontext *ctx) const
{
    Tool::draw(ctx);
    if (m_dragging)
    {
        auto img = m_images_panel->current_image();
        if (!img)
            return;

        Vector2i start_pos(m_image_view->position_at_pixel(Vector2f(m_start_pixel) + 0.5f));
        if (is_valid(m_end_pixel))
        {
            auto color = m_screen->foreground()->exposed_color();

            // draw a rectangle for the line (just drawing an nvgLine only works up to a max stroke width)
            nvgSave(ctx);
            Vector2i end_pos(m_image_view->position_at_pixel(Vector2f(m_end_pixel) + 0.5f));
            Vector2f to = end_pos - start_pos;
            Vector2f u  = normalize(to);
            Vector2f v(u.y(), -u.x());
            v *= m_width * m_image_view->zoom();
            nvgTransform(ctx, u.x(), u.y(), v.x(), v.y(), start_pos.x(), start_pos.y());

            nvgBeginPath(ctx);
            nvgRect(ctx, 0, -0.5f, norm(to), 1.f);

            nvgFillColor(ctx, color);
            nvgFill(ctx);

            nvgRestore(ctx);

            draw_crosshairs(ctx, end_pos);
        }

        draw_crosshairs(ctx, start_pos);

        Tool::draw(ctx);
    }
}