//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "actionbutton.h"
// #include <nanogui/popupbutton.h>
#include <nanogui/opengl.h>
#include <nanogui/theme.h>

NAMESPACE_BEGIN(nanogui)

ActionButton::ActionButton(Widget *parent, Action *action) :
    ActionWidget(parent, action), m_icon_position(IconPosition::LeftCentered), m_flags(NormalButton),
    m_background_color(Color(0, 0)), m_text_color(Color(0, 0))
{
    // empty
}

Vector2i ActionButton::preferred_size(NVGcontext *ctx) const
{
    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    float tw = nvgTextBounds(ctx, 0, 0, caption().c_str(), nullptr, nullptr);
    float iw = 0.0f, ih = font_size;

    if (m_action->icon())
    {
        if (nvg_is_font_icon(m_action->icon()))
        {
            ih *= icon_scale();
            nvgFontFace(ctx, "icons");
            nvgFontSize(ctx, ih);
            iw = nvgTextBounds(ctx, 0, 0, utf8(m_action->icon()).data(), nullptr, nullptr) + m_size.y() * 0.15f;
        }
        else
        {
            int w, h;
            ih *= 0.9f;
            nvgImageSize(ctx, m_action->icon(), &w, &h);
            iw = w * ih / h;
        }
    }
    return Vector2i((int)(tw + iw) + 20, font_size + 10);
}

bool ActionButton::mouse_enter_event(const Vector2i &p, bool enter)
{
    Widget::mouse_enter_event(p, enter);
    return true;
}

bool ActionButton::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    Widget::mouse_button_event(p, button, down, modifiers);
    /* Temporarily increase the reference count of the button in case the
       button causes the parent window to be destructed */
    ref<ActionButton> self = this;

    if (m_enabled == 1 && ((button == GLFW_MOUSE_BUTTON_1) || (button == GLFW_MOUSE_BUTTON_2)))
    {
        // trigger on mouse down for toggle buttons, and mouse up for normal buttons
        if (down && (m_flags & ToggleButton))
            m_action->trigger();
        else
            m_action->trigger();

        return true;
    }
    return false;
}

void ActionButton::draw(NVGcontext *ctx)
{
    Widget::draw(ctx);

    bool pushed = m_action->checked();

    NVGcolor grad_top = m_theme->m_button_gradient_top_unfocused;
    NVGcolor grad_bot = m_theme->m_button_gradient_bot_unfocused;

    if (pushed || (m_mouse_focus))
    {
        grad_top = m_theme->m_button_gradient_top_pushed;
        grad_bot = m_theme->m_button_gradient_bot_pushed;
    }
    else if (m_mouse_focus && m_enabled)
    {
        grad_top = m_theme->m_button_gradient_top_focused;
        grad_bot = m_theme->m_button_gradient_bot_focused;
    }

    nvgBeginPath(ctx);

    nvgRoundedRect(ctx, m_pos.x() + 1, m_pos.y() + 1.0f, m_size.x() - 2, m_size.y() - 2,
                   m_theme->m_button_corner_radius - 1);

    if (m_background_color.w() != 0)
    {
        nvgFillColor(ctx, Color(m_background_color[0], m_background_color[1], m_background_color[2], 1.f));
        nvgFill(ctx);
        if (pushed)
        {
            grad_top.a = grad_bot.a = 0.8f;
        }
        else
        {
            double v   = 1 - m_background_color.w();
            grad_top.a = grad_bot.a = m_enabled ? v : v * .5f + .5f;
        }
    }

    NVGpaint bg = nvgLinearGradient(ctx, m_pos.x(), m_pos.y(), m_pos.x(), m_pos.y() + m_size.y(), grad_top, grad_bot);

    nvgFillPaint(ctx, bg);
    nvgFill(ctx);

    nvgBeginPath(ctx);
    nvgStrokeWidth(ctx, 1.0f);
    nvgRoundedRect(ctx, m_pos.x() + 0.5f, m_pos.y() + (pushed ? 0.5f : 1.5f), m_size.x() - 1,
                   m_size.y() - 1 - (pushed ? 0.0f : 1.0f), m_theme->m_button_corner_radius);
    nvgStrokeColor(ctx, m_theme->m_border_light);
    nvgStroke(ctx);

    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + 0.5f, m_pos.y() + 0.5f, m_size.x() - 1, m_size.y() - 2,
                   m_theme->m_button_corner_radius);
    nvgStrokeColor(ctx, m_theme->m_border_dark);
    nvgStroke(ctx);

    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    float tw = nvgTextBounds(ctx, 0, 0, caption().c_str(), nullptr, nullptr);

    Vector2f center = Vector2f(m_pos) + Vector2f(m_size) * 0.5f;
    Vector2f text_pos(center.x() - tw * 0.5f, center.y() - 1);
    NVGcolor text_color = m_text_color.w() == 0 ? m_theme->m_text_color : m_text_color;
    if (!m_enabled)
        text_color = m_theme->m_disabled_text_color;

    if (m_action->icon())
    {
        auto icon = utf8(m_action->icon());

        float iw, ih = font_size;
        if (nvg_is_font_icon(m_action->icon()))
        {
            ih *= icon_scale();
            nvgFontSize(ctx, ih);
            nvgFontFace(ctx, "icons");
            iw = nvgTextBounds(ctx, 0, 0, icon.data(), nullptr, nullptr);
        }
        else
        {
            int w, h;
            ih *= 0.9f;
            nvgImageSize(ctx, m_action->icon(), &w, &h);
            iw = w * ih / h;
        }
        if (caption() != "")
            iw += m_size.y() * 0.15f;
        nvgFillColor(ctx, text_color);
        nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        Vector2f icon_pos = center;
        icon_pos.y() -= 1;

        if (m_icon_position == IconPosition::LeftCentered)
        {
            icon_pos.x() -= (tw + iw) * 0.5f;
            text_pos.x() += iw * 0.5f;
        }
        else if (m_icon_position == IconPosition::RightCentered)
        {
            text_pos.x() -= iw * 0.5f;
            icon_pos.x() += tw * 0.5f;
        }
        else if (m_icon_position == IconPosition::Left)
        {
            icon_pos.x() = m_pos.x() + 8;
        }
        else if (m_icon_position == IconPosition::Right)
        {
            icon_pos.x() = m_pos.x() + m_size.x() - iw - 8;
        }

        if (nvg_is_font_icon(m_action->icon()))
        {
            nvgText(ctx, icon_pos.x(), icon_pos.y() + 1, icon.data(), nullptr);
        }
        else
        {
            NVGpaint img_paint = nvgImagePattern(ctx, icon_pos.x(), icon_pos.y() - ih / 2, iw, ih, 0, m_action->icon(),
                                                 m_enabled ? 0.5f : 0.25f);

            nvgFillPaint(ctx, img_paint);
            nvgFill(ctx);
        }
    }

    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, m_theme->m_text_color_shadow);
    nvgText(ctx, text_pos.x(), text_pos.y(), caption().c_str(), nullptr);
    nvgFillColor(ctx, text_color);
    nvgText(ctx, text_pos.x(), text_pos.y() + 1, caption().c_str(), nullptr);
}

NAMESPACE_END(nanogui)
