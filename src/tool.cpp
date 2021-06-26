//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "tool.h"
#include "brush.h"
#include "drawline.h"
#include "dropdown.h"
#include "hdrimageview.h"
#include "hdrviewscreen.h"
#include "imagelistpanel.h"
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

ToolButton *Tool::create_toolbutton(Widget *toolbar)
{
    m_button = new ToolButton(toolbar, m_icon);
    m_button->set_fixed_size(Vector2i(0));
    m_button->set_flags(Button::Flags::RadioButton);
    m_button->set_callback([this] { m_screen->set_tool(m_tool); });
    m_button->set_tooltip(m_name + ": " + m_tooltip);
    m_button->set_icon_extra_scale(1.5f);

    return m_button;
}

void Tool::set_active(bool b)
{
    spdlog::trace("setting {} active: {}.", m_name, b);
    if (m_button)
        m_button->set_pushed(b);
    else
        spdlog::error("Button for {} never created.", m_name);

    if (m_options)
    {
        m_options->set_visible(b);
        m_screen->request_layout_update();
    }
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
    Vector2i tl          = m_image_view->position_for_coordinate(img->roi().min);
    Vector2i br          = m_image_view->position_for_coordinate(img->roi().max);
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
    m_image_view->set_image_coordinate_at(p + rel, m_image_view->image_coordinate_at(p));
    return false;
}

bool Tool::keyboard(int key, int scancode, int action, int modifiers)
{
    auto img = m_images_panel->current_image();

    switch (key)
    {
    case ' ':
        spdlog::trace("KEY ` ` pressed");
        m_screen->set_tool(Tool_None);
        return true;

    case 'M':
        spdlog::trace("KEY `M` pressed");
        m_screen->set_tool(Tool_Rectangular_Marquee);
        return true;

    case 'S':
        spdlog::trace("KEY `S` pressed");
        m_screen->set_tool(Tool_Clone_Stamp);
        return true;

    case 'B':
        spdlog::trace("KEY `B` pressed");
        m_screen->set_tool(Tool_Brush);
        return true;

    case 'U':
        spdlog::trace("KEY `U` pressed");
        m_screen->set_tool(Tool_Line);
        return true;

    case 'I':
        spdlog::trace("KEY `I` pressed");
        m_screen->set_tool(Tool_Eyedropper);
        return true;

    case 'A':
        spdlog::trace("Key `A` pressed");
        if (img && modifiers & SYSTEM_COMMAND_MOD)
        {
            img->roi() = img->box();
            return true;
        }
        break;

    case 'D':
        spdlog::trace("Key `D` pressed");
        if (img && modifiers & SYSTEM_COMMAND_MOD)
        {
            img->roi() = Box2i();
            return true;
        }
        break;

    case 'G':
        spdlog::trace("KEY `G` pressed");
        if (modifiers & GLFW_MOD_SHIFT)
            m_image_view->set_gamma(m_image_view->gamma() + 0.02f);
        else
            m_image_view->set_gamma(std::max(0.02f, m_image_view->gamma() - 0.02f));
        return true;

    case 'E':
        spdlog::trace("KEY `E` pressed");
        if (modifiers & GLFW_MOD_SHIFT)
            m_image_view->set_exposure(m_image_view->exposure() + 0.25f);
        else
            m_image_view->set_exposure(m_image_view->exposure() - 0.25f);
        return true;
    }

    return false;
}

HandTool::HandTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel, const string &name,
                   const string &tooltip, int icon, ETool tool) :
    Tool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

Widget *HandTool::create_options_bar(nanogui::Widget *parent)
{
    bool  sRGB     = m_image_view->sRGB();
    float gamma    = m_image_view->gamma();
    float exposure = m_image_view->exposure();

    m_options = new Widget(parent);
    m_options->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 5, 5));

    new Label(m_options, "EV:");
    auto exposure_slider  = new Slider(m_options);
    auto exposure_textbox = new FloatBox<float>(m_options, exposure);
    auto normalize_button = new Button(m_options, "", FA_MAGIC);
    normalize_button->set_fixed_size(nanogui::Vector2i(19, 19));
    normalize_button->set_icon_extra_scale(1.15f);
    normalize_button->set_callback(
        [this]()
        {
            auto img = m_images_panel->current_image();
            if (!img)
                return;
            Color4 mC  = img->image().max();
            float  mCf = std::max({mC[0], mC[1], mC[2]});
            spdlog::debug("max value: {}", mCf);
            m_image_view->set_exposure(log2(1.0f / mCf));
            m_images_panel->request_histogram_update(true);
        });
    normalize_button->set_tooltip("Normalize exposure.");
    auto reset_button = new Button(m_options, "", FA_SYNC);
    reset_button->set_fixed_size(nanogui::Vector2i(19, 19));
    reset_button->set_icon_extra_scale(1.15f);
    reset_button->set_callback(
        [this]()
        {
            m_image_view->set_exposure(0.0f);
            m_image_view->set_gamma(2.2f);
            m_image_view->set_sRGB(true);
            m_images_panel->request_histogram_update(true);
        });
    reset_button->set_tooltip("Reset tonemapping.");

    auto sRGB_checkbox = new CheckBox(m_options, "sRGB");
    auto gamma_label   = new Label(m_options, "Gamma:");
    auto gamma_slider  = new Slider(m_options);
    auto gamma_textbox = new FloatBox<float>(m_options);

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

    (new CheckBox(m_options, "Dither", [&](bool v) { m_image_view->set_dithering(v); }))
        ->set_checked(m_image_view->dithering_on());
    (new CheckBox(m_options, "Grid", [&](bool v) { m_image_view->set_draw_grid(v); }))
        ->set_checked(m_image_view->draw_grid_on());
    (new CheckBox(m_options, "RGB values", [&](bool v) { m_image_view->set_draw_pixel_info(v); }))
        ->set_checked(m_image_view->draw_pixel_info_on());

    return m_options;
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
        auto ic       = m_image_view->image_coordinate_at(p - m_image_view->position());
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
    auto     ic = m_image_view->image_coordinate_at(p - m_image_view->position());
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
    m_clicked(std::numeric_limits<int>::lowest())
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
    settings["smoothing"] = m_smoothing_checkbox->pushed();
}

Widget *BrushTool::create_options_bar(nanogui::Widget *parent)
{
    auto &settings = this_tool_settings();

    m_brush->set_radius(settings.value("size", 15));
    m_brush->set_hardness(settings.value("hardness", 0.f));
    m_brush->set_flow(settings.value("flow", 1.f));
    m_brush->set_angle(settings.value("angle", 0.f));
    m_brush->set_roundness(settings.value("roundness", 1.f));
    m_brush->set_spacing(settings.value("spacing", 0.f));

    m_options = new Widget(parent);
    m_options->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 5, 0));
    m_options->set_visible(false);

    m_options->add<Label>("Size:");
    m_size_slider  = new Slider(m_options);
    m_size_textbox = new IntBox<int>(m_options);

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
    m_options->add<Widget>()->set_fixed_width(5);

    m_options->add<Label>("Hard:");
    m_hardness_slider  = new Slider(m_options);
    m_hardness_textbox = new FloatBox<float>(m_options);

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
    m_options->add<Widget>()->set_fixed_width(5);

    m_options->add<Label>("Flow:");
    m_flow_slider  = new Slider(m_options);
    m_flow_textbox = new FloatBox<float>(m_options);

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
    m_options->add<Widget>()->set_fixed_width(5);

    m_options->add<Label>("Angle:");
    m_angle_slider  = new Slider(m_options);
    m_angle_textbox = new FloatBox<float>(m_options);

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
    m_options->add<Widget>()->set_fixed_width(5);

    m_options->add<Label>("Round:");
    m_roundness_slider  = new Slider(m_options);
    m_roundness_textbox = new FloatBox<float>(m_options);

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
    m_options->add<Widget>()->set_fixed_width(5);

    m_options->add<Label>("Spacing:");
    m_spacing_slider  = new Slider(m_options);
    m_spacing_textbox = new FloatBox<float>(m_options);

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
    m_options->add<Widget>()->set_fixed_width(5);

    m_smoothing_checkbox = new CheckBox(m_options, "Smoothing");
    m_smoothing_checkbox->set_checked(settings.value("smoothing", true));
    m_smoothing_checkbox->set_callback([this](bool b) { m_smoothing = b; });

    return m_options;
}

bool BrushTool::keyboard(int key, int scancode, int action, int modifiers)
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
        spdlog::trace("Key `]` pressed");
        int dr = std::max(1, (int)ceil(m_brush->radius() * 1.1 - m_brush->radius()));
        int r  = std::clamp(m_brush->radius() + dr, 1, (int)m_size_slider->range().second);
        m_brush->set_radius(r);
        m_size_textbox->set_value(m_brush->radius());
        m_size_slider->set_value(m_brush->radius());
        return true;
    }

    return Tool::keyboard(key, scancode, action, modifiers);
}

bool BrushTool::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    spdlog::trace("modifier: {}", modifiers);
    auto color =
        modifiers & GLFW_MOD_ALT ? m_screen->background()->exposed_color() : m_screen->foreground()->exposed_color();

    Box2i roi = m_images_panel->current_image()->roi();
    if (roi.has_volume())
        roi.intersect(m_images_panel->current_image()->box());
    else
        roi = m_images_panel->current_image()->box();

    auto coord = m_image_view->image_coordinate_at(p - m_image_view->position());
    auto pixel = Vector2i(round(coord.x()), round(coord.y()));

    if (!down)
    {
        // draw a line for the last part of the stroke
        if (m_p1.x() != std::numeric_limits<int>::lowest())
        {
            m_images_panel->current_image()->direct_modify(
                [&pixel, &color, &roi, this](const HDRImagePtr &new_image)
                {
                    auto put_pixel = [this, new_image, &color, &roi](int x, int y)
                    {
                        m_brush->stamp_onto(
                            *new_image, x, y, [&color](int, int) { return color; }, roi);
                    };

                    draw_line(m_p1.x(), m_p1.y(), pixel.x(), pixel.y(), put_pixel);
                });
        }
    }
    else if (down && modifiers & GLFW_MOD_SHIFT)
    {
        // draw a straight line from the previously clicked point
        if (m_clicked.x() != std::numeric_limits<int>::lowest())
        {
            m_images_panel->current_image()->start_modify(
                [&pixel, &color, &roi, this](const ConstHDRImagePtr &img,
                                             const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    auto new_image = make_shared<HDRImage>(*img);

                    auto put_pixel = [this, new_image, &color, &roi](int x, int y)
                    {
                        m_brush->stamp_onto(
                            *new_image, x, y, [&color](int, int) { return color; }, roi);
                    };

                    draw_line(m_clicked.x(), m_clicked.y(), pixel.x(), pixel.y(), put_pixel);

                    return {new_image, make_shared<FullImageUndo>(*img)};
                });

            m_screen->update_caption();
        }
    }
    else if (down)
    {
        m_images_panel->current_image()->start_modify(
            [&pixel, &color, &roi, this](const ConstHDRImagePtr &img,
                                         const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto new_image = make_shared<HDRImage>(*img);

                m_brush->set_step(0);
                m_brush->stamp_onto(
                    *new_image, pixel.x(), pixel.y(), [&color](int, int) { return color; }, roi);

                return {new_image, make_shared<FullImageUndo>(*img)};
            });
    }

    m_screen->request_layout_update();
    m_screen->update_caption();

    m_clicked = pixel;

    m_p0 = std::numeric_limits<int>::lowest();
    m_p1 = std::numeric_limits<int>::lowest();

    return true;
}

bool BrushTool::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    m_screen->request_layout_update();
    auto coord      = m_image_view->image_coordinate_at(p - m_image_view->position());
    auto pixel      = Vector2i(round(coord.x()), round(coord.y()));
    auto prev_coord = m_image_view->image_coordinate_at(p - rel - m_image_view->position());
    auto prev_pixel = Vector2i(round(prev_coord.x()), round(prev_coord.y()));

    if (prev_pixel == pixel)
        return false;

    auto color =
        modifiers & GLFW_MOD_ALT ? m_screen->background()->exposed_color() : m_screen->foreground()->exposed_color();
    m_images_panel->current_image()->direct_modify(
        [&pixel, &prev_pixel, &color, this](const HDRImagePtr &new_image)
        {
            Box2i roi = m_images_panel->current_image()->roi();
            if (roi.has_volume())
                roi.intersect(new_image->box());
            else
                roi = new_image->box();

            auto put_pixel = [this, new_image, &color, &roi](int x, int y)
            {
                m_brush->stamp_onto(
                    *new_image, x, y, [&color](int, int) { return color; }, roi);
            };

            if (m_smoothing)
            {
                if (m_p1.x() != std::numeric_limits<int>::lowest())
                {
                    // draw_CatmullRom(m_p0.x(), m_p0.y(), m_p1.x(), m_p1.y(), prev_pixel.x(), prev_pixel.y(),
                    // pixel.x(), pixel.y(), put_pixel1, 0.5f);
                    draw_quadratic(m_p1.x(), m_p1.y(), prev_pixel.x(), prev_pixel.y(), pixel.x(), pixel.y(), put_pixel,
                                   4, m_p0.x() == std::numeric_limits<int>::lowest());
                    // auto a = (m_p1 + prev_pixel) / 2;
                    // auto b = prev_pixel;
                    // auto c = (prev_pixel + pixel) / 2;
                    // draw_quad_Bezier(a.x(), a.y(), b.x(), b.y(), c.x(), c.y(), put_pixel);
                }
            }
            else
                draw_line(prev_pixel.x(), prev_pixel.y(), pixel.x(), pixel.y(), put_pixel);

            m_p0 = m_p1;
            m_p1 = prev_pixel;
        });
    m_screen->update_caption();
    return true;
}

void BrushTool::draw(NVGcontext *ctx) const
{
    if (!m_images_panel->current_image())
        return;

    auto center = m_screen->mouse_pos() - m_image_view->absolute_position();

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

    Tool::draw(ctx);
}

EraserTool::EraserTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel,
                       const string &name, const string &tooltip, int icon, ETool tool) :
    BrushTool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

bool EraserTool::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    if (down)
    {
        m_screen->request_layout_update();
        auto coord = m_image_view->image_coordinate_at(p - m_image_view->position());
        auto pixel = Vector2i(round(coord.x()), round(coord.y()));
        m_images_panel->current_image()->start_modify(
            [&pixel, this](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto new_image = make_shared<HDRImage>(*img);

                Box2i roi = m_images_panel->current_image()->roi();
                if (roi.has_volume())
                    roi.intersect(m_images_panel->current_image()->box());
                else
                    roi = img->box();

                HDRImage &new_image_ref = *new_image;

                auto plot_pixel = [&new_image_ref](int x, int y, float a)
                { new_image_ref(x, y).a = 0.f * a + new_image_ref(x, y).a * (1.0f - a); };

                m_brush->set_step(0);
                m_brush->stamp_onto(pixel.x(), pixel.y(), plot_pixel, roi);

                return {new_image, make_shared<FullImageUndo>(*img)};
            });
        m_screen->update_caption();
        return true;
    }
    return false;
}

bool EraserTool::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    m_screen->request_layout_update();
    auto coord      = m_image_view->image_coordinate_at(p - m_image_view->position());
    auto pixel      = Vector2i(round(coord.x()), round(coord.y()));
    coord           = m_image_view->image_coordinate_at(p - rel - m_image_view->position());
    auto prev_pixel = Vector2i(round(coord.x()), round(coord.y()));
    m_images_panel->current_image()->direct_modify(
        [&pixel, &prev_pixel, this](const HDRImagePtr &new_image)
        {
            Box2i roi = m_images_panel->current_image()->roi();
            if (roi.has_volume())
                roi.intersect(m_images_panel->current_image()->box());
            else
                roi = m_images_panel->current_image()->box();

            HDRImage &new_image_ref = *new_image;

            auto plot_pixel = [&new_image_ref](int x, int y, float a)
            { new_image_ref(x, y).a = 0.f * a + new_image_ref(x, y).a * (1.0f - a); };

            draw_line(prev_pixel.x(), prev_pixel.y(), pixel.x(), pixel.y(),
                      [this, &plot_pixel, &roi](int x, int y) { m_brush->stamp_onto(x, y, plot_pixel, roi); });
        });
    m_screen->update_caption();
    return true;
}

CloneStampTool::CloneStampTool(HDRViewScreen *screen, HDRImageView *image_view, ImageListPanel *images_panel,
                               const string &name, const string &tooltip, int icon, ETool tool) :
    BrushTool(screen, image_view, images_panel, name, tooltip, icon, tool)
{
    // empty
}

bool CloneStampTool::mouse_button(const Vector2i &p, int button, bool down, int modifiers)
{
    if (modifiers & GLFW_MOD_ALT)
    {
        m_has_src   = true;
        m_src_click = p;
        return true;
    }
    else if (down)
    {
        m_has_dst   = true;
        m_dst_click = p;
        m_screen->request_layout_update();
        auto coord     = m_image_view->image_coordinate_at(p - m_image_view->position());
        auto pixel     = Vector2i(round(coord.x()), round(coord.y()));
        auto coord_dst = m_image_view->image_coordinate_at(m_dst_click - m_image_view->position());
        auto pixel_dst = Vector2i(round(coord_dst.x()), round(coord_dst.y()));
        auto coord_src = m_image_view->image_coordinate_at(m_src_click - m_image_view->position());
        auto dpixel    = Vector2i(round(coord_src.x()), round(coord_src.y())) - pixel_dst;
        m_images_panel->current_image()->start_modify(
            [&pixel, &dpixel, this](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto new_image = make_shared<HDRImage>(*img);

                Box2i roi = m_images_panel->current_image()->roi();
                if (roi.has_volume())
                    roi.intersect(m_images_panel->current_image()->box());
                else
                    roi = img->box();

                HDRImage &new_image_ref = *new_image;

                auto src_color = [&new_image_ref, &dpixel](int dst_x, int dst_y)
                {
                    int x = dst_x + dpixel.x();
                    int y = dst_y + dpixel.y();

                    if (x >= 0 && y >= 0 && x < new_image_ref.width() && y < new_image_ref.height())
                        return new_image_ref(x, y);
                    else
                        return Color4(0.f);
                };

                m_brush->set_step(0);
                m_brush->stamp_onto(new_image_ref, pixel.x(), pixel.y(), src_color, roi);

                return {new_image, make_shared<FullImageUndo>(*img)};
            });
        m_screen->update_caption();
        return true;
    }
    else
        m_has_dst = false;
    return false;
}

bool CloneStampTool::mouse_drag(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    m_screen->request_layout_update();
    auto coord      = m_image_view->image_coordinate_at(p - m_image_view->position());
    auto pixel      = Vector2i(round(coord.x()), round(coord.y()));
    auto coord_dst  = m_image_view->image_coordinate_at(m_dst_click - m_image_view->position());
    auto pixel_dst  = Vector2i(round(coord_dst.x()), round(coord_dst.y()));
    auto coord_src  = m_image_view->image_coordinate_at(m_src_click - m_image_view->position());
    auto dpixel     = Vector2i(round(coord_src.x()), round(coord_src.y())) - pixel_dst;
    coord           = m_image_view->image_coordinate_at(p - rel - m_image_view->position());
    auto prev_pixel = Vector2i(round(coord.x()), round(coord.y()));
    m_images_panel->current_image()->direct_modify(
        [&pixel, &prev_pixel, &dpixel, this](const HDRImagePtr &new_image)
        {
            Box2i roi = m_images_panel->current_image()->roi();
            if (roi.has_volume())
                roi.intersect(m_images_panel->current_image()->box());
            else
                roi = m_images_panel->current_image()->box();

            HDRImage &new_image_ref = *new_image;

            auto src_color = [&new_image_ref, &dpixel](int dst_x, int dst_y)
            {
                int x = dst_x + dpixel.x();
                int y = dst_y + dpixel.y();

                if (x >= 0 && y >= 0 && x < new_image_ref.width() && y < new_image_ref.height())
                    return new_image_ref(x, y);
                else
                    return Color4(0.f);
            };

            draw_line(prev_pixel.x(), prev_pixel.y(), pixel.x(), pixel.y(),
                      [this, &new_image_ref, &src_color, &roi](int x, int y)
                      { m_brush->stamp_onto(new_image_ref, x, y, src_color, roi); });
        });
    m_screen->update_caption();
    return true;
}

void CloneStampTool::draw(NVGcontext *ctx) const
{
    if (!m_images_panel->current_image())
        return;

    if (m_has_dst && m_has_src)
    {
        auto center = m_screen->mouse_pos() - m_image_view->absolute_position() - m_dst_click + m_src_click;

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

        draw_crosshairs(ctx, center);
    }
    else
        BrushTool::draw(ctx);
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

Widget *Eyedropper::create_options_bar(nanogui::Widget *parent)
{
    auto &settings = this_tool_settings();

    m_options = new Widget(parent);
    m_options->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 5, 5));
    m_options->set_visible(false);

    new Label(m_options, "Sample size:");

    auto size = new Dropdown(m_options, {"Point sample", "3 × 3 average", "5 × 5 average", "7 × 7 average"});
    size->set_tooltip("The number of pixels sampled by the eyedropper.");
    size->set_callback([this](int s) { m_size = s; });
    size->set_selected_index(std::clamp(settings.value("size", 0), 0, 3));
    size->set_fixed_height(19);

    return m_options;
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
                Vector2i pixel(m_image_view->image_coordinate_at((p - m_image_view->position())));
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

    Vector2i        center_pixel(m_image_view->image_coordinate_at((center)));
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
        m_start_pixel   = m_image_view->image_coordinate_at(p - m_image_view->position());
        m_end_pixel.x() = std::numeric_limits<int>::lowest();
        return true;
    }
    else if (is_valid(m_start_pixel))
    {
        m_end_pixel = m_image_view->image_coordinate_at(p - m_image_view->position());
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

    Vector2i start_pos(m_image_view->position_for_coordinate(Vector2f(m_start_pixel) + 0.5f));
    if (is_valid(m_end_pixel))
    {
        Vector2i end_pos(m_image_view->position_for_coordinate(Vector2f(m_end_pixel) + 0.5f));
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

Widget *LineTool::create_options_bar(nanogui::Widget *parent)
{
    auto &settings = this_tool_settings();

    m_width = std::clamp(settings.value("width", 2.f), 1.f, 100.f);

    m_options = new Widget(parent);
    m_options->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 5, 5));
    m_options->set_visible(false);

    m_options->add<Label>("Width:");
    m_width_slider  = new Slider(m_options);
    m_width_textbox = new FloatBox<float>(m_options);

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

    return m_options;
}

bool LineTool::keyboard(int key, int scancode, int action, int modifiers)
{
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

                draw_line(m_start_pixel.x(), m_start_pixel.y(), m_end_pixel.x(), m_end_pixel.y(), (float)m_width,
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

        Vector2i start_pos(m_image_view->position_for_coordinate(Vector2f(m_start_pixel) + 0.5f));
        if (is_valid(m_end_pixel))
        {
            auto color = m_screen->foreground()->exposed_color();

            // draw a rectangle for the line (just drawing an nvgLine only works up to a max stroke width)
            nvgSave(ctx);
            Vector2i end_pos(m_image_view->position_for_coordinate(Vector2f(m_end_pixel) + 0.5f));
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