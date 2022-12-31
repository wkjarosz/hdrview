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
#include <stdexcept> // for runtime_error

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using std::runtime_error;
using std::string;
using std::vector;

NAMESPACE_BEGIN(nanogui)

Dropdown::Dropdown(Widget *parent, Mode mode, const string &caption) :
    MenuItem(parent, caption), m_selected_index(0), m_mode(mode)
{
    set_flags(Flags::ToggleButton);

    m_popup = new PopupMenu(screen(), window());
    m_popup->set_size(Vector2i(320, 250));
    m_popup->set_visible(false);

    if (m_mode == Menu)
        set_fixed_size(preferred_size(screen()->nvg_context()));
}

Dropdown::Dropdown(Widget *parent, const vector<string> &items, const vector<int> &icons, Mode mode,
                   const string &caption) :
    Dropdown(parent, mode, caption)
{
    set_items(items, icons);
}

Vector2i Dropdown::preferred_size(NVGcontext *ctx) const
{
    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    if (m_mode == ComboBox)
        return Vector2i(m_popup->preferred_size(ctx).x(), font_size + 5);
    else if (m_mode == Menu)
        return MenuItem::preferred_size(ctx) - Vector2i(5, 0);
    else
        return MenuItem::preferred_size(ctx);
}

MenuItem *Dropdown::item(int idx) const
{
    if (idx >= (int)m_popup->child_count())
        throw runtime_error(fmt::format("Trying to access invalid index {} on a menu with only {} items.", idx,
                                        (int)m_popup->child_count()));

    return (MenuItem *)m_popup->child_at(idx);
}

void Dropdown::set_selected_index(int idx)
{
    if (m_mode != ComboBox || m_popup->child_count() <= idx)
        return;

    item(m_selected_index)->set_pushed(false);
    item(idx)->set_pushed(true);

    m_selected_index = idx;
    set_caption(item(m_selected_index)->caption());
}

void Dropdown::set_items(const vector<string> &items, const vector<int> &icons)
{
    // remove all children
    while (m_popup->child_count() != 0) m_popup->remove_child_at(m_popup->child_count() - 1);

    for (int index = 0; index < (int)items.size(); ++index)
    {
        auto caption = items[index];
        auto icon    = icons.size() == items.size() ? icons[index] : 0;
        auto item    = m_popup->add<MenuItem>(caption, icon);
        item->set_flags(m_mode == ComboBox ? Button::RadioButton : Button::NormalButton);
        item->set_callback(
            [&, index, this]
            {
                set_selected_index(index);
                if (m_selected_callback)
                    m_selected_callback(index);
            });
    }
    set_selected_index(0);
}

Vector2i Dropdown::compute_position() const
{
    Vector2i offset;
    if (m_mode == ComboBox)
        offset = Vector2i(-3, -m_selected_index * PopupMenu::menu_item_height - 4);
    else if (m_mode == Menu)
        offset = Vector2i(0, PopupMenu::menu_item_height);
    else
        offset = Vector2i(size().x(), -4);

    Vector2i abs_pos = absolute_position() + offset;

    // prevent bottom of menu from getting clipped off screen
    abs_pos.y() += std::min(0, screen()->height() - (abs_pos.y() + m_popup->size().y() + 2));

    // prevent top of menu from getting clipped off screen
    if (abs_pos.y() <= 1)
        abs_pos.y() = absolute_position().y() + size().y() - 2;

    return abs_pos;
}

bool Dropdown::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    auto ret = MenuItem::mouse_button_event(p, button, down, modifiers);
    if (m_enabled && m_pushed)
    {
        if (!m_focused)
            request_focus();

        m_popup->set_position(compute_position());

        // first turn focus off on all menu buttons
        for (auto it : m_popup->children()) it->mouse_enter_event(p - m_pos, false);

        // now turn focus on to just the button under the cursor
        if (auto w = m_popup->find_widget(screen()->mouse_pos() - m_popup->parent()->absolute_position()))
            w->mouse_enter_event(p + absolute_position() - w->absolute_position(), true);

        m_popup->set_visible(true);
        m_popup->request_focus();
    }
    else
        m_popup->set_visible(false);
    return ret;
}

void Dropdown::draw(NVGcontext *ctx)
{
    if (!m_popup->visible())
        set_pushed(false);

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

    if (m_mode != Menu)
    {
        string icon = m_mode == ComboBox ? utf8(FA_SORT) : utf8(m_theme->m_popup_chevron_right_icon);

        nvgFontSize(ctx, (m_font_size < 0 ? m_theme->m_button_font_size : m_font_size) * icon_scale());
        nvgFontFace(ctx, "icons");
        nvgFillColor(ctx, m_enabled ? text_color : NVGcolor(m_theme->m_disabled_text_color));
        nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        float    iw = nvgTextBounds(ctx, 0, 0, icon.data(), nullptr, nullptr);
        Vector2f icon_pos(0, m_pos.y() + m_size.y() * 0.5f);

        icon_pos[0] = m_pos.x() + m_size.x() - iw - 8;

        nvgText(ctx, icon_pos.x(), icon_pos.y(), icon.data(), nullptr);
    }
}

NAMESPACE_END(nanogui)
