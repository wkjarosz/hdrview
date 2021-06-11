//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "dropdown.h"
#include <cassert>
#include <nanogui/icons.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/popup.h>
#include <nanogui/screen.h>

using std::string;
using std::vector;

NAMESPACE_BEGIN(nanogui)

Dropdown::Dropdown(Widget *parent) : Button(parent), m_selected_index(0)
{
    set_flags(Flags::ToggleButton);

    m_popup = new PopupMenu(screen(), window());
    m_popup->set_size(Vector2i(320, 250));
    m_popup->set_visible(false);
}

Dropdown::Dropdown(Widget *parent, const vector<string> &items) : Dropdown(parent) { set_items(items); }

Dropdown::Dropdown(Widget *parent, const vector<string> &items, const vector<string> &items_short) : Dropdown(parent)
{
    set_items(items, items_short);
}

Vector2i Dropdown::preferred_size(NVGcontext *ctx) const
{
    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    return Vector2i(m_popup->preferred_size(ctx).x(), font_size + 5);
}

void Dropdown::set_selected_index(int idx)
{
    if (m_items_short.empty())
        return;
    m_selected_index = idx;
    set_caption(m_items_short[idx]);
}

void Dropdown::set_items(const vector<string> &items, const vector<string> &items_short)
{
    assert(items.size() == items_short.size());
    m_items       = items;
    m_items_short = items_short;

    if (m_selected_index < 0 || m_selected_index >= (int)items.size())
        m_selected_index = 0;
    while (m_popup->child_count() != 0) m_popup->remove_child_at(m_popup->child_count() - 1);

    int index = 0;
    for (const auto &str : items)
    {
        auto button = m_popup->add_item(str);
        // button->set_flags(Button::RadioButton);
        button->set_callback(
            [&, index]
            {
                m_selected_index = index;
                set_caption(m_items_short[index]);
                // set_pushed(false);
                m_popup->set_visible(false);
                if (m_callback)
                    m_callback(index);
            });
        index++;
    }
    set_selected_index(m_selected_index);
}

bool Dropdown::scroll_event(const Vector2i &p, const Vector2f &rel)
{
    set_pushed(false);
    m_popup->set_visible(false);
    if (rel.y() < 0)
    {
        set_selected_index(std::min(m_selected_index + 1, (int)(items().size() - 1)));
        if (m_callback)
            m_callback(m_selected_index);
        return true;
    }
    else if (rel.y() > 0)
    {
        set_selected_index(std::max(m_selected_index - 1, 0));
        if (m_callback)
            m_callback(m_selected_index);
        return true;
    }
    return Widget::scroll_event(p, rel);
}

bool Dropdown::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    if (m_enabled)
    {
        int xoffset = std::max(size().x() - m_popup->size().x(), 0) / 2;
        int yoffset = -m_selected_index * 24 - 5;
        m_popup->set_position(absolute_position() + Vector2i(xoffset, yoffset));

        if (down)
        {
            if (m_popup->visible())
            {
                m_popup->set_visible(false);
                return true;
            }
            else
            {
                m_popup->set_visible(true);
                // m_popup->mouse_enter_event(p, true);
                for (auto it = m_popup->children().rbegin(); it != m_popup->children().rend(); ++it)
                    (*it)->mouse_enter_event(p - position(), false);

                m_popup->children()[m_selected_index]->mouse_enter_event(p - position(), true);
            }
        }
        return true;
    }

    return Widget::mouse_button_event(p, button, down, modifiers);
}

void Dropdown::draw(NVGcontext *ctx)
{
    set_pushed(m_popup->visible());

    if (!m_enabled && m_pushed)
        m_pushed = false;

    Button::draw(ctx);

    auto     icon       = utf8(FA_SORT);
    NVGcolor text_color = m_text_color.w() == 0 ? m_theme->m_text_color : m_text_color;

    nvgFontSize(ctx, (m_font_size < 0 ? m_theme->m_button_font_size : m_font_size) * icon_scale());
    nvgFontFace(ctx, "icons");
    nvgFillColor(ctx, m_enabled ? text_color : NVGcolor(m_theme->m_disabled_text_color));
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    float    iw = nvgTextBounds(ctx, 0, 0, icon.data(), nullptr, nullptr);
    Vector2f icon_pos(0, m_pos.y() + m_size.y() * 0.5f);

    icon_pos[0] = m_pos.x() + m_size.x() - iw - 6;

    nvgText(ctx, icon_pos.x(), icon_pos.y(), icon.data(), nullptr);
}

NAMESPACE_END(nanogui)
