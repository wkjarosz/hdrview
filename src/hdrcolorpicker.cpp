//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrcolorpicker.h"
#include "box.h"
#include "colorslider.h"
#include "xpuimage.h"
#include <hdrview_resources.h>
#include <iostream>
#include <nanogui/colorwheel.h>
#include <nanogui/icons.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/textbox.h>
#include <nanogui/toolbutton.h>

#include <spdlog/spdlog.h>

#include <spdlog/fmt/ostr.h>

NAMESPACE_BEGIN(nanogui)

HDRColorPicker::HDRColorPicker(Widget *parent, const Color &color, float exposure, int components) :
    PopupButton(parent, ""), m_color(color), m_previous_color(color), m_exposure(exposure), m_previous_exposure(0.f)
{
    // initialize callback to do nothing; this is for users to hook into
    // receiving a new color value
    m_callback       = [](const Color &, float) {};
    m_final_callback = [](const Color &, float) {};

    set_background_color(m_color);
    Popup *popup = this->popup();
    popup->set_layout(new GroupLayout());

    // set the color wheel to the specified color
    m_color_wheel = new ColorWheel2(popup, m_color, components);

    // add the sub-widget that contains the color sliders
    auto panel = new Widget(popup);
    auto agrid = new AdvancedGridLayout({0, 20, 0}, {});
    agrid->set_margin(0);
    agrid->set_col_stretch(1, 1);
    panel->set_layout(agrid);

    auto row = new Widget(popup);
    int  num = 1 + bool(components & RESET_BTN) + bool(components & EYEDROPPER);
    row->set_layout(new GridLayout(Orientation::Horizontal, num, Alignment::Fill, 0, 5));

    // set the reset button to the specified color
    m_reset_button = new Button(row, "Reset");
    m_reset_button->set_background_color(m_color);
    m_reset_button->set_text_color(m_color.contrasting_color());
    m_reset_button->set_visible(components & RESET_BTN);

    // set the pick button to the specified color
    m_pick_button = new Button(row, "Pick");
    m_pick_button->set_background_color(m_color);
    m_pick_button->set_text_color(m_color.contrasting_color());

    m_eyedropper = new ToolButton(row, FA_EYE_DROPPER);
    m_eyedropper->set_icon_extra_scale(1.5f);
    m_eyedropper->set_visible(components & EYEDROPPER);

    PopupButton::set_change_callback(
        [&](bool)
        {
            if (m_pick_button->pushed())
            {
                // set_color(m_color);
                m_final_callback(m_color, m_exposure);
            }
        });

    // create the color sliders and hook up callbacks

    //
    // RGBA
    //

    std::string                    channel_names[]     = {"Red", "Green", "Blue", "Alpha", "Exposure"};
    int                            slider_components[] = {R_SLIDER, G_SLIDER, B_SLIDER, A_SLIDER, E_SLIDER};
    int                            box_components[]    = {R_BOX, G_BOX, B_BOX, A_BOX, E_BOX};
    std::vector<ColorSlider *>     sliders;
    std::vector<FloatBox<float> *> float_boxes;
    for (int c = 0; c < 5; ++c)
    {
        std::string tip = fmt::format("Change the color's {} value", channel_names[c]);
        agrid->append_row(0);
        auto l = new Label(panel, channel_names[c] + ":");
        agrid->set_anchor(l, AdvancedGridLayout::Anchor(0, agrid->row_count() - 1));

        auto float_box = new FloatBox<float>(panel, c < 4 ? m_color[c] : 0.f);
        agrid->set_anchor(float_box, AdvancedGridLayout::Anchor(2, agrid->row_count() - 1));
        float_box->number_format("%1.3f");
        float_box->set_editable(true);
        std::pair<float, float> range = {0.f, 1.f};
        if (c == 4)
            range = {-9.f, 9.f};
        float_box->set_min_value(range.first);
        float_box->set_max_value(range.second);
        float_box->set_spinnable(true);
        float_box->set_value_increment(c < 4 ? 0.01f : 0.125f);
        float_box->set_fixed_width(60);
        float_box->set_alignment(TextBox::Alignment::Right);
        float_box->set_tooltip(tip);

        agrid->append_row(0);
        auto slider = new ColorSlider(panel, m_color, ColorSlider::ColorMode(c));
        agrid->set_anchor(slider, AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));
        slider->set_color(m_color);
        slider->set_value(c < 4 ? m_color[c] : 0.f);
        slider->set_range(range);
        slider->set_tooltip(tip);

        l->set_visible(components & box_components[c]);
        float_box->set_visible(components & box_components[c]);
        slider->set_visible(components & slider_components[c]);
        if ((components & slider_components[c]) && (components & box_components[c]))
            agrid->append_row(10);

        float_boxes.push_back(float_box);
        sliders.push_back(slider);
    }

    // helper function shared by some of the callbacks below
    m_sync_helper = [float_boxes, sliders, this]()
    {
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            sliders[i]->set_color(m_color);
            float_boxes[i]->set_value(i < 4 ? m_color[i] : m_exposure);
            if (i == 4)
                sliders[i]->set_value(m_exposure);
        }

        auto e_color = exposed_color();
        auto t_color = e_color.contrasting_color();

        m_pick_button->set_background_color(e_color);
        m_pick_button->set_text_color(t_color);
    };

    for (size_t c = 0; c < sliders.size(); ++c)
    {
        auto callback = [this, c](float v)
        {
            if (c < 4)
                m_color[c] = v;
            else
                m_exposure = v;

            m_color_wheel->set_color(m_color);

            m_sync_helper();
            m_callback(m_color, m_exposure);
        };

        sliders[c]->set_callback(callback);
        float_boxes[c]->set_callback(callback);
    }

    m_color_wheel->set_callback(
        [this](const Color &c)
        {
            m_color = c;
            m_sync_helper();
            m_callback(m_color, m_exposure);
        });

    m_pick_button->set_callback(
        [this]()
        {
            if (m_pushed)
            {
                Color e_color = exposed_color();
                Color t_color = e_color.contrasting_color();

                set_pushed(false);
                // set_color(e_color);

                set_background_color(e_color);
                set_text_color(t_color);

                m_reset_button->set_background_color(e_color);
                m_reset_button->set_text_color(t_color);
                m_previous_color    = m_color;
                m_previous_exposure = m_exposure;

                m_final_callback(m_color, m_exposure);
            }
        });

    m_reset_button->set_callback(
        [this]()
        {
            update_all(m_previous_color, m_previous_exposure);

            m_callback(m_color, m_exposure);
            m_final_callback(m_color, m_exposure);
        });

    // popup->set_anchor_offset(popup->height());
    // popup->set_anchor_offset(0);
}

Color HDRColorPicker::exposed_color() const
{
    float gain    = pow(2.f, m_exposure);
    Color e_color = m_color * gain;
    e_color.a()   = m_color.a();
    return e_color;
}

void HDRColorPicker::update_all(const Color &c, float e)
{

    // normalize Color to 0..1 range, and extract
    // extra exposure to get back original HDR color
    float extra_exposure   = 0.f;
    Color normalized_color = c;
    float max_comp         = std::max({c[0], c[1], c[2]});
    if (max_comp > 1.f)
    {
        extra_exposure = log2(max_comp);
        normalized_color[0] /= max_comp;
        normalized_color[1] /= max_comp;
        normalized_color[2] /= max_comp;
    }

    m_color    = normalized_color;
    m_exposure = e + extra_exposure;
    // spdlog::trace("color {}, exposure {}", m_color, m_exposure);

    m_color_wheel->set_color(m_color);

    Color e_color = exposed_color();
    Color t_color = e_color.contrasting_color();

    set_background_color(e_color);
    set_text_color(t_color);

    m_sync_helper();

    // m_reset_button->set_background_color(e_color);
    // m_reset_button->set_text_color(t_color);
    // m_previous_color = m_color;
    // m_previous_exposure = m_exposure;
}

void HDRColorPicker::set_color(const Color &color)
{
    /* Ignore set_color() calls when the user is currently editing */
    // if (!m_pushed)
    update_all(color, 0.f);
}

void HDRColorPicker::set_exposure(float e)
{
    /* Ignore set_exposure() calls when the user is currently editing */
    // if (!m_pushed)
    {
        update_all(m_color, e);
    }
}

void HDRColorPicker::draw(NVGcontext *ctx)
{
    if (!m_enabled && m_pushed)
        m_pushed = false;

    m_popup->set_visible(m_pushed);

    int w = std::min(size().x(), size().y());

    int border_w = 1;

    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + border_w, m_pos.y() + border_w, w - 2 * border_w, w - 2 * border_w, 0);

    nvgStrokeWidth(ctx, 2 * border_w);
    nvgStrokeColor(ctx, m_background_color.contrasting_color());
    nvgStroke(ctx);

    int  h;
    auto checker = hdrview_image_icon(ctx, checker4, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_NEAREST);
    nvgImageSize(ctx, checker, &w, &h);
    NVGpaint paint = nvgImagePattern(ctx, m_pos.x(), m_pos.y() - 1, w, h, 0, checker, 1.f);
    nvgFillPaint(ctx, paint);
    nvgFill(ctx);

    nvgFillColor(ctx, m_background_color);
    nvgFill(ctx);
}

DualHDRColorPicker::DualHDRColorPicker(Widget *parent, int fgcomp, int bgcomp) :
    Widget(parent), m_background(new HDRColorPicker(this, Color(0), 0.f, bgcomp)),
    m_foreground(new HDRColorPicker(this, Color(0), 0.f, fgcomp))
{
    set_default_colors();
}

Vector2i DualHDRColorPicker::preferred_size(NVGcontext *) const { return Vector2i(64, 64); }

Box2i DualHDRColorPicker::foreground_box() const
{
    int w = std::min(size().x(), size().y());
    return Box2i(Vector2i(0), Vector2i((w * 2) / 3));
}

Box2i DualHDRColorPicker::background_box() const
{
    int w = std::min(size().x(), size().y());
    return foreground_box().move_max_to(Vector2i(w));
}

void DualHDRColorPicker::perform_layout(NVGcontext *ctx)
{
    auto fgb = foreground_box();
    auto bgb = background_box();

    m_foreground->set_position(fgb.min);
    m_foreground->set_size(fgb.size());

    m_background->set_position(bgb.min);
    m_background->set_size(bgb.size());

    m_foreground->perform_layout(ctx);
    m_background->perform_layout(ctx);
}

void DualHDRColorPicker::draw(NVGcontext *ctx)
{
    auto fgb = foreground_box();
    auto bgb = background_box();

    nvgTranslate(ctx, m_pos.x(), m_pos.y());

    m_background->draw(ctx);
    m_foreground->draw(ctx);

    //
    // draw the swapping arrows
    //
    int   min_size   = std::min(size().x(), size().y());
    float pad        = (fgb.size().x() / 8) / 2.f;
    float arrow_size = 2;

    int corner_size       = min_size - fgb.size().x();
    int corner_small_size = ceil(corner_size * 3.f / 4) - pad;

    // corner line
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, fgb.max.x() + pad, fgb.min.y() + arrow_size + pad);
    nvgLineTo(ctx, bgb.max.x() - arrow_size - pad, fgb.min.y() + arrow_size + pad);
    nvgLineTo(ctx, bgb.max.x() - arrow_size - pad, bgb.min.y() - pad);
    nvgStrokeWidth(ctx, 1.0f);
    nvgStrokeColor(ctx, Color(255, 255));
    nvgStroke(ctx);

    // top left arrowhead
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, fgb.max.x() + pad, fgb.min.y() + arrow_size + pad);
    nvgLineTo(ctx, fgb.max.x() + pad + arrow_size, fgb.min.y() + pad);
    nvgLineTo(ctx, fgb.max.x() + pad + arrow_size, fgb.min.y() + 2 * arrow_size + pad);
    nvgClosePath(ctx);
    nvgFillColor(ctx, Color(255, 255));
    nvgFill(ctx);

    // bottom right arrowhead
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, bgb.max.x() - arrow_size - pad, bgb.min.y() - pad);
    nvgLineTo(ctx, bgb.max.x() - pad, bgb.min.y() - pad - arrow_size);
    nvgLineTo(ctx, bgb.max.x() - 2 * arrow_size - pad, bgb.min.y() - pad - arrow_size);
    nvgClosePath(ctx);
    nvgFillColor(ctx, Color(255, 255));
    nvgFill(ctx);

    //
    // draw the default color button
    //

    // white background square
    nvgBeginPath(ctx);
    nvgRect(ctx, bgb.min.x() - corner_small_size - pad, bgb.max.y() - corner_small_size, corner_small_size,
            corner_small_size);
    nvgFillColor(ctx, Color(255, 255));
    nvgFill(ctx);

    // black foreground square with white border
    nvgBeginPath(ctx);
    nvgRect(ctx, fgb.min.x() + 0.5f, fgb.max.y() + pad + 0.5f, corner_small_size - 1, corner_small_size - 1);
    nvgFillColor(ctx, Color(0, 255));
    nvgFill(ctx);
    nvgStrokeWidth(ctx, 1.f);
    nvgStrokeColor(ctx, Color(255, 255));
    nvgStroke(ctx);

    nvgTranslate(ctx, -m_pos.x(), -m_pos.y());
}

bool DualHDRColorPicker::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    if (Widget::mouse_button_event(p, button, down, modifiers))
        return true;

    if (down)
    {
        if (p.x() - m_pos.x() > p.y() - m_pos.y())
        {
            swap_colors();
            return true;
        }
        else
        {
            set_default_colors();
            return true;
        }
    }
    return false;
}

/// Handle a mouse motion event
bool DualHDRColorPicker::mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    if (p.x() - m_pos.x() > foreground_box().max.x() && p.y() - m_pos.y() < background_box().min.y())
        set_tooltip("Swap foreground and background colors.");
    else
        set_tooltip("");

    return Widget::mouse_motion_event(p, rel, button, modifiers);
}

void DualHDRColorPicker::swap_colors()
{
    // swap colors
    spdlog::trace("swapping colors");
    Color tmp_c = m_foreground->color();
    float tmp_e = m_foreground->exposure();

    m_foreground->set_color(m_background->color());
    m_foreground->set_exposure(m_background->exposure());

    m_background->set_color(tmp_c);
    m_background->set_exposure(tmp_e);
}

void DualHDRColorPicker::set_default_colors()
{
    m_foreground->set_color(Color(0, 255));
    m_foreground->set_exposure(0.f);

    m_background->set_color(Color(255, 255));
    m_background->set_exposure(0.f);
}

NAMESPACE_END(nanogui)
