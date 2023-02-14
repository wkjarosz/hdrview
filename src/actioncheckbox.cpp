//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This class is based off of the NanoGUI CheckBox class
//

#include "actioncheckbox.h"
#include <nanogui/opengl.h>
#include <nanogui/theme.h>

NAMESPACE_BEGIN(nanogui)

ActionCheckBox::ActionCheckBox(Widget *parent, Action *action) : ActionWidget(parent, action)
{
    set_caption(m_action->text());
    m_icon_extra_scale = 1.2f; // widget override
}

bool ActionCheckBox::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    ActionWidget::mouse_button_event(p, button, down, modifiers);
    if (!m_enabled)
        return false;

    if (button == GLFW_MOUSE_BUTTON_1)
    {
        if (down)
        {
            m_pushed = true;
        }
        else if (m_pushed)
        {
            if (contains(p))
                trigger();
            m_pushed = false;
        }
        return true;
    }
    return false;
}

Vector2i ActionCheckBox::preferred_size(NVGcontext *ctx) const
{
    if (m_fixed_size != Vector2i(0))
        return m_fixed_size;
    nvgFontSize(ctx, font_size());
    nvgFontFace(ctx, "sans");
    return Vector2i(nvgTextBounds(ctx, 0, 0, m_caption.c_str(), nullptr, nullptr) + 1.8f * font_size(),
                    font_size() * 1.3f);
}

void ActionCheckBox::draw(NVGcontext *ctx)
{
    Widget::draw(ctx);

    nvgFontSize(ctx, font_size());
    nvgFontFace(ctx, "sans");
    nvgFillColor(ctx, m_enabled ? m_theme->m_text_color : m_theme->m_disabled_text_color);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(ctx, m_pos.x() + 1.6f * font_size(), m_pos.y() + m_size.y() * 0.5f, m_caption.c_str(), nullptr);

    NVGpaint bg = nvgBoxGradient(ctx, m_pos.x() + 1.5f, m_pos.y() + 1.5f, m_size.y() - 2.0f, m_size.y() - 2.0f, 3, 3,
                                 m_pushed ? Color(0, 100) : Color(0, 32), Color(0, 0, 0, 180));

    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + 1.0f, m_pos.y() + 1.0f, m_size.y() - 2.0f, m_size.y() - 2.0f, 3);
    nvgFillPaint(ctx, bg);
    nvgFill(ctx);

    if (checked())
    {
        nvgFontSize(ctx, icon_scale() * m_size.y());
        nvgFontFace(ctx, "icons");
        nvgFillColor(ctx, m_enabled ? m_theme->m_icon_color : m_theme->m_disabled_text_color);
        nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(ctx, m_pos.x() + m_size.y() * 0.5f + 1, m_pos.y() + m_size.y() * 0.5f,
                utf8(m_theme->m_check_box_icon).data(), nullptr);
    }
}

NAMESPACE_END(nanogui)
