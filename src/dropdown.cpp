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

#include <spdlog/spdlog.h>

#include <spdlog/fmt/ostr.h>

using std::string;
using std::vector;

NAMESPACE_BEGIN(nanogui)

Dropdown::Dropdown(Widget *parent) : Button(parent), m_selected_index(0), m_selected_index_f(0.f)
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

    // first turn off the checkmark to get consistent widths regardless of which item is selected
    // for (auto it : m_popup->children())
    //     if (auto i = dynamic_cast<PopupMenu::Item *>(it))
    //         i->set_checked(false);
    auto result = Vector2i(m_popup->preferred_size(ctx).x(), font_size + 5);
    // re-enabled checkmark
    // dynamic_cast<PopupMenu::Item *>(m_popup->child_at(m_selected_index))->set_checked(true);
    return result;
}

void Dropdown::set_selected_index(int idx)
{
    if (m_items_short.empty())
        return;
    m_selected_index   = idx;
    m_selected_index_f = (float)m_selected_index;
    set_caption(m_items_short[idx]);

    for (auto it : m_popup->children())
        if (auto i = dynamic_cast<PopupMenu::Item *>(it))
            i->set_checked(false);
    dynamic_cast<PopupMenu::Item *>(m_popup->child_at(m_selected_index))->set_checked(true);
}

void Dropdown::set_items(const vector<string> &items, const vector<string> &items_short)
{
    assert(items.size() == items_short.size());
    m_items       = items;
    m_items_short = items_short;

    if (m_selected_index < 0 || m_selected_index >= (int)items.size())
    {
        m_selected_index   = 0;
        m_selected_index_f = (float)m_selected_index;
    }
    while (m_popup->child_count() != 0) m_popup->remove_child_at(m_popup->child_count() - 1);

    int index = 0;
    for (const auto &str : items)
    {
        auto button = m_popup->add_item(str);
        // button->set_flags(Button::RadioButton);
        button->set_callback(
            [&, index, this]
            {
                set_selected_index(index);
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
    if (!enabled())
        return false;

    float speed = 0.1f;
    set_pushed(false);
    m_popup->set_visible(false);

    m_selected_index_f = std::clamp(m_selected_index_f + rel.y() * speed, 0.0f, items().size() - 1.f);
    m_selected_index   = std::clamp((int)round(m_selected_index_f), 0, (int)(items().size() - 1));
    set_caption(m_items_short[m_selected_index]);

    if (m_callback)
        m_callback(m_selected_index);
    return true;
}

bool Dropdown::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    if (m_enabled)
    {
        if (button == GLFW_MOUSE_BUTTON_1 && down && !m_focused)
            request_focus();

        Vector2i offset(-3, -m_selected_index * PopupMenu::menu_item_height - 4);
        Vector2i abs_pos = absolute_position() + offset;

        // prevent bottom of menu from getting clipped off screen
        if (abs_pos.y() + m_popup->size().y() >= screen()->height())
            abs_pos.y() = absolute_position().y() - m_popup->size().y() + 2;

        // prevent top of menu from getting clipped off screen
        if (abs_pos.y() <= 1)
            abs_pos.y() = absolute_position().y() + size().y() - 2;

        m_popup->set_position(abs_pos);

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
                // first turn focus off on all menu buttons
                for (auto it : m_popup->children()) it->mouse_enter_event(p - m_pos, false);

                // now turn focus on to just the button under the cursor
                if (auto w = m_popup->find_widget(screen()->mouse_pos() - m_popup->parent()->absolute_position()))
                    w->mouse_enter_event(p + absolute_position() - w->absolute_position(), true);
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

    Widget::draw(ctx);

    NVGcolor grad_top = m_theme->m_button_gradient_top_unfocused;
    NVGcolor grad_bot = m_theme->m_button_gradient_bot_unfocused;

    if (m_pushed || (m_mouse_focus && (m_flags & MenuButton)))
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
        if (m_pushed)
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
    nvgRoundedRect(ctx, m_pos.x() + 0.5f, m_pos.y() + (m_pushed ? 0.5f : 1.5f), m_size.x() - 1,
                   m_size.y() - 1 - (m_pushed ? 0.0f : 1.0f), m_theme->m_button_corner_radius);
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

    Vector2f center = Vector2f(m_pos) + Vector2f(m_size) * 0.5f;
    Vector2f text_pos(m_pos.x() + 10, center.y() - 1);
    NVGcolor text_color = m_text_color.w() == 0 ? m_theme->m_text_color : m_text_color;
    if (!m_enabled)
        text_color = m_theme->m_disabled_text_color;

    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, m_theme->m_text_color_shadow);
    nvgText(ctx, text_pos.x(), text_pos.y(), m_caption.c_str(), nullptr);
    nvgFillColor(ctx, text_color);
    nvgText(ctx, text_pos.x(), text_pos.y() + 1, m_caption.c_str(), nullptr);

    auto icon = utf8(FA_SORT);

    nvgFontSize(ctx, (m_font_size < 0 ? m_theme->m_button_font_size : m_font_size) * icon_scale());
    nvgFontFace(ctx, "icons");
    nvgFillColor(ctx, m_enabled ? text_color : NVGcolor(m_theme->m_disabled_text_color));
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    float    iw = nvgTextBounds(ctx, 0, 0, icon.data(), nullptr, nullptr);
    Vector2f icon_pos(0, m_pos.y() + m_size.y() * 0.5f);

    icon_pos[0] = m_pos.x() + m_size.x() - iw - 8;

    nvgText(ctx, icon_pos.x(), icon_pos.y(), icon.data(), nullptr);
}

NAMESPACE_END(nanogui)
