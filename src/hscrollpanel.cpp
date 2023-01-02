//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hscrollpanel.h"
#include <nanogui/opengl.h>
#include <nanogui/theme.h>
#include <spdlog/spdlog.h>

NAMESPACE_BEGIN(nanogui)

HScrollPanel::HScrollPanel(Widget *parent, bool scrollbar_visible) :
    Widget(parent), m_scrollbar_visible(scrollbar_visible), m_child_preferred_width(0), m_scroll(0.f),
    m_update_layout(false)
{
}

void HScrollPanel::perform_layout(NVGcontext *ctx)
{
    Widget::perform_layout(ctx);

    if (m_children.empty())
        return;
    if (m_children.size() > 1)
        throw std::runtime_error("HScrollPanel should have one child.");

    Widget *child           = m_children[0];
    m_child_preferred_width = child->preferred_size(ctx).x();

    if (m_child_preferred_width > m_size.x())
    {
        child->set_position(Vector2i(-m_scroll * (m_child_preferred_width - m_size.x()), 0));
        child->set_size(Vector2i(m_child_preferred_width, m_size.y()));
    }
    else
    {
        child->set_position(Vector2i(0));
        child->set_size(m_size);
        m_scroll = 0;
    }
    child->perform_layout(ctx);
}

Vector2i HScrollPanel::preferred_size(NVGcontext *ctx) const
{
    if (m_children.empty())
        return Vector2i(0);
    return m_children[0]->preferred_size(ctx) + Vector2i(m_scrollbar_visible ? 12 : 0, 0);
}

bool HScrollPanel::mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    if (!m_children.empty() && m_child_preferred_width > m_size.x())
    {
        float scrollw = width() * std::min(1.f, width() / (float)m_child_preferred_width);

        m_scroll        = std::max(0.f, std::min(1.f, m_scroll + rel.x() / (m_size.x() - 8.f - scrollw)));
        m_update_layout = true;
        return true;
    }
    else
    {
        return Widget::mouse_drag_event(p, rel, button, modifiers);
    }
}

bool HScrollPanel::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    if (Widget::mouse_button_event(p, button, down, modifiers))
        return true;

    if (m_scrollbar_visible && down && button == GLFW_MOUSE_BUTTON_1 && !m_children.empty() &&
        m_child_preferred_width > m_size.x() && p.y() > m_pos.y() + m_size.y() - 13 &&
        p.y() < m_pos.y() + m_size.y() - 4)
    {
        int scrollw = (int)(width() * std::min(1.f, width() / (float)m_child_preferred_width));
        int start   = (int)(m_pos.x() + 4 + 1 + (m_size.x() - 8 - scrollw) * m_scroll);

        float delta = 0.f;

        if (p.x() < start)
            delta = -m_size.x() / (float)m_child_preferred_width;
        else if (p.x() > start + scrollw)
            delta = m_size.x() / (float)m_child_preferred_width;

        m_scroll = std::max(0.f, std::min(1.f, m_scroll + delta * 0.98f));

        m_children[0]->set_position(Vector2i(-m_scroll * (m_child_preferred_width - m_size.x()), 0));
        m_update_layout = true;
        return true;
    }
    return false;
}

bool HScrollPanel::scroll_event(const Vector2i &p, const Vector2f &rel)
{
    if (!m_children.empty() && m_child_preferred_width > m_size.x())
    {
        auto  child         = m_children[0];
        float scroll_amount = rel.x() * m_size.x() * .25f;

        m_scroll = std::max(0.f, std::min(1.f, m_scroll - scroll_amount / m_child_preferred_width));

        Vector2i old_pos = child->position();
        child->set_position(Vector2i(-m_scroll * (m_child_preferred_width - m_size.x()), 0));
        Vector2i new_pos = child->position();
        m_update_layout  = true;
        child->mouse_motion_event(p - m_pos, old_pos - new_pos, 0, 0);

        return true;
    }
    else
    {
        return Widget::scroll_event(p, rel);
    }
}

void HScrollPanel::draw(NVGcontext *ctx)
{
    if (m_children.empty())
        return;

    Widget *child   = m_children[0];
    int     xoffset = 0;
    if (m_child_preferred_width > m_size.x())
        xoffset = -m_scroll * (m_child_preferred_width - m_size.x());
    child->set_position(Vector2i(xoffset, 0));
    m_child_preferred_width = child->preferred_size(ctx).x();
    float scrollw           = width() * std::min(1.f, width() / (float)m_child_preferred_width);

    if (m_update_layout)
    {
        m_update_layout = false;
        child->perform_layout(ctx);
    }

    nvgSave(ctx);
    nvgTranslate(ctx, m_pos.x(), m_pos.y());
    nvgIntersectScissor(ctx, 0, 0, m_size.x(), m_size.y());
    if (child->visible())
        child->draw(ctx);
    nvgRestore(ctx);

    if (m_child_preferred_width <= m_size.x() || !m_scrollbar_visible)
        return;

    NVGpaint paint = nvgBoxGradient(ctx, m_pos.x() + 4 + 1, m_pos.y() + m_size.y() - 12 + 1, m_size.x() - 8, 8, 3, 4,
                                    Color(0, 32), Color(0, 92));
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + 4, m_pos.y() + m_size.y() - 12, m_size.x() - 8, 8, 3);
    nvgFillPaint(ctx, paint);
    nvgFill(ctx);

    paint = nvgBoxGradient(ctx, m_pos.x() + 4 + (m_size.x() - 8 - scrollw) * m_scroll - 1,
                           m_pos.y() + m_size.y() - 12 - 1, scrollw, 8, 3, 4, Color(220, 100), Color(128, 100));

    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + 4 + 1 + (m_size.x() - 8 - scrollw) * m_scroll, m_pos.y() + m_size.y() - 12 + 1,
                   scrollw - 2, 8 - 2, 2);
    nvgFillPaint(ctx, paint);
    nvgFill(ctx);
}

NAMESPACE_END(nanogui)
