//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorslider.h"
#include "xpuimage.h"
#include <hdrview_resources.h>
#include <nanogui/icons.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/theme.h>
#include <spdlog/spdlog.h>

NAMESPACE_BEGIN(nanogui)

ColorSlider::ColorSlider(Widget *parent, const Color &c, ColorMode m) : Slider(parent), m_color(c), m_mode(m) {}

Vector2i ColorSlider::preferred_size(NVGcontext *) const { return Vector2i(70, 16); }

bool ColorSlider::mouse_drag_event(const Vector2i &p, const Vector2i & /* rel */, int /* button */, int /* modifiers */)
{
    if (!m_enabled)
        return false;

    const float knob_radius = (int)(m_size.y() * 0.4f);
    const float knob_shadow = 3;
    const float start_x     = knob_radius + knob_shadow + m_pos.x() - 1;
    const float width_x     = m_size.x() - 2 * (knob_radius + knob_shadow);

    float value = (p.x() - start_x) / width_x, old_value = m_value;
    value   = value * (m_range.second - m_range.first) + m_range.first;
    m_value = std::min(std::max(value, m_range.first), m_range.second);
    if (m_mode <= ColorMode::ALPHA)
        m_color[(int)m_mode] = m_value;
    if (m_callback && m_value != old_value)
        m_callback(m_value);
    return true;
}

bool ColorSlider::mouse_button_event(const Vector2i &p, int /* button */, bool down, int /* modifiers */)
{
    if (!m_enabled)
        return false;

    const float knob_radius = (int)(m_size.y() * 0.4f);
    const float knob_shadow = 3;
    const float start_x     = knob_radius + knob_shadow + m_pos.x() - 1;
    const float width_x     = m_size.x() - 2 * (knob_radius + knob_shadow);

    float value = (p.x() - start_x) / width_x, old_value = m_value;
    value   = value * (m_range.second - m_range.first) + m_range.first;
    m_value = std::min(std::max(value, m_range.first), m_range.second);
    if (m_mode <= ColorMode::ALPHA)
        m_color[(int)m_mode] = m_value;
    if (m_callback && m_value != old_value)
        m_callback(m_value);
    if (m_final_callback && !down)
        m_final_callback(m_value);
    return true;
}

void ColorSlider::draw(NVGcontext *ctx)
{
    Vector2f    center      = Vector2f(m_pos) + Vector2f(m_size) * 0.5f;
    float       knob_radius = (int)(m_size.y() * 0.4f);
    const float knob_shadow = 3;
    int         bar_radius  = m_mode == ColorMode::ALPHA ? knob_radius - 1 : 2;

    float start_x = m_pos.x() + knob_radius + knob_shadow;
    float width_x = m_size.x() - 2 * knob_radius - 2 * knob_shadow;

    // draw the horizontal bar
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, start_x - bar_radius, center.y() - bar_radius, width_x + 2 * bar_radius, 2 * bar_radius + 1,
                   bar_radius);

    // draw the checkboard background (for semi-transparent gradients)
    if (m_mode == ColorMode::ALPHA)
    {
        int  w, h;
        auto checker = hdrview_image_icon(ctx, checker4, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_NEAREST);
        nvgImageSize(ctx, checker, &w, &h);
        NVGpaint paint = nvgImagePattern(ctx, m_pos.x(), m_pos.y() - 1, w, h, 0, checker, m_enabled ? 0.5f : 0.25f);
        nvgFillPaint(ctx, paint);
        nvgFill(ctx);
    }

    Color low(m_color), hi(m_color);
    if (m_mode <= ColorMode::ALPHA)
    {
        low[(int)m_mode] = m_range.first;
        hi[(int)m_mode]  = m_range.second;
        if (m_mode < ColorMode::ALPHA)
            low.a() = hi.a() = m_enabled ? 1.0f : 0.25f;
    }
    else
    {
        // this should really be an exponential gradient, but we'll use linear for simplicity
        low -= (m_color * std::pow(1.5f, range().second) - m_color);
        hi += (m_color * std::pow(1.5f, range().second) - m_color);
        low.a() = hi.a() = m_enabled ? 1.0f : 0.25f;
    }
    NVGpaint bg = nvgLinearGradient(ctx, start_x, center.y(), width_x, center.y(), low, hi);

    nvgFillPaint(ctx, bg);
    nvgStrokeColor(ctx, Color(0, m_enabled ? 255 : 128));
    nvgStrokeWidth(ctx, 1.f);
    nvgFill(ctx);
    nvgStroke(ctx);

    // // highlight the ends of the horizontal bar
    // if (m_highlighted_range.second != m_highlighted_range.first)
    // {
    //     nvgBeginPath(ctx);
    //     nvgRoundedRect(ctx, start_x + m_highlighted_range.first * m_size.x(),
    //                    center.y() - knob_shadow + 1,
    //                    width_x *
    //                        (m_highlighted_range.second - m_highlighted_range.first),
    //                    knob_shadow * 2, 2);
    //     nvgFillColor(ctx, m_highlight_color);
    //     nvgFill(ctx);
    // }

    // draw the knob
    Vector2f knob_pos(start_x + (m_value - m_range.first) / (m_range.second - m_range.first) * (width_x),
                      center.y() + 0.5f);
    NVGpaint shadow_paint = nvgRadialGradient(ctx, knob_pos.x(), knob_pos.y(), knob_radius - knob_shadow,
                                              knob_radius + knob_shadow, Color(0, 64), m_theme->m_transparent);

    nvgBeginPath(ctx);
    nvgRect(ctx, knob_pos.x() - knob_radius - 5, knob_pos.y() - knob_radius - 5, knob_radius * 2 + 10,
            knob_radius * 2 + 10 + knob_shadow);
    nvgCircle(ctx, knob_pos.x(), knob_pos.y(), knob_radius);
    nvgPathWinding(ctx, NVG_HOLE);
    nvgFillPaint(ctx, shadow_paint);
    nvgFill(ctx);

    nvgBeginPath(ctx);
    nvgCircle(ctx, knob_pos.x(), knob_pos.y(), knob_radius - 1);
    Color knob_fill = m_mode == ColorMode::EXPOSURE ? m_color * std::pow(2.f, m_value) : m_color;
    knob_fill.a()   = 1.f;
    nvgFillColor(ctx, knob_fill);
    nvgFill(ctx);
    nvgStrokeColor(ctx, Color(m_enabled ? 0 : 64, 255));
    nvgStrokeWidth(ctx, 2.5f);
    nvgStroke(ctx);
    nvgStrokeColor(ctx, Color(m_enabled ? 255 : 128, 255));
    nvgStrokeWidth(ctx, 1.5f);
    nvgStroke(ctx);
}

NAMESPACE_END(nanogui)
