//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "menu.h"
#include "helpwindow.h"
#include <cassert>
#include <nanogui/button.h>
#include <nanogui/icons.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/popupbutton.h>
#include <nanogui/screen.h>
#include <nanogui/theme.h>
#include <nanogui/vscrollpanel.h>
#include <spdlog/spdlog.h>
#include <stdexcept> // for runtime_error

#include <spdlog/fmt/ostr.h>

using std::runtime_error;
using std::string;
using std::vector;

NAMESPACE_BEGIN(nanogui)

MenuItem::MenuItem(Widget *parent, const std::string &caption, int button_icon) : Button(parent, caption, button_icon)
{
    set_fixed_height(PopupMenu::menu_item_height);
    m_icon_position = IconPosition::Left;
}

void MenuItem::set_hotkey(int modcode, int keycode)
{
    m_button    = keycode;
    m_modifiers = modcode;
    string str;
    if (modcode & SYSTEM_COMMAND_MOD)
        str += "{CMD}+";
    if (modcode & GLFW_MOD_ALT)
        str += "{ALT}+";
    if (modcode & GLFW_MOD_SHIFT)
        str += "Shift+";

    // printable characters
    if (32 < keycode && keycode < 128)
        str += char(keycode);
    // function keys
    else if (keycode == GLFW_KEY_SPACE)
        str += "Space";
    else if (GLFW_KEY_F1 <= keycode && keycode <= GLFW_KEY_F25)
        str += fmt::format("F{}", keycode - GLFW_KEY_F1 + 1);
    else if (keycode == GLFW_KEY_BACKSPACE)
        str += "Backspace";
    else if (keycode == GLFW_KEY_DELETE)
        str += "Delete";
    else if (keycode == GLFW_KEY_UP)
        str += "Up";
    else if (keycode == GLFW_KEY_DOWN)
        str += "Down";
    else if (keycode == GLFW_KEY_LEFT)
        str += "Left";
    else if (keycode == GLFW_KEY_RIGHT)
        str += "Right";
    else if (keycode == GLFW_KEY_PAGE_UP)
        str += "Page Up";
    else if (keycode == GLFW_KEY_DOWN)
        str += "Page Down";
    else if (keycode == GLFW_KEY_TAB)
        str += "Tab";
    else if (keycode == GLFW_KEY_ESCAPE)
        str += "Esc";
    else if (keycode == GLFW_KEY_ENTER)
        str += "Enter";

    m_hotkey = HelpWindow::key_string(str);
}

Vector2i MenuItem::preferred_text_size(NVGcontext *ctx) const
{
    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    float tw = nvgTextBounds(ctx, 0, 0, m_caption.c_str(), nullptr, nullptr);

    return Vector2i((int)(tw) + 24, font_size + 10);
}

Vector2i MenuItem::preferred_size(NVGcontext *ctx) const
{
    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    // float iw = 0.0f, ih = font_size * icon_scale();
    // ih *= icon_scale();
    // nvgFontFace(ctx, "icons");
    // nvgFontSize(ctx, ih);
    // iw = nvgTextBounds(ctx, 0, 0, utf8(m_icon).data(), nullptr, nullptr) + m_size.y() * 0.15f;
    float iw = font_size * icon_scale();
    float sw = m_hotkey.size() ? nvgTextBounds(ctx, 0, 0, m_hotkey.c_str(), nullptr, nullptr) + iw * 5 : 0;
    return preferred_text_size(ctx) + Vector2i((int)(iw + sw), 0);
}

void MenuItem::draw(NVGcontext *ctx)
{
    Widget::draw(ctx);

    NVGcolor grad_top = m_theme->m_button_gradient_top_unfocused;
    NVGcolor grad_bot = m_theme->m_button_gradient_bot_unfocused;

    if (m_mouse_focus && m_enabled)
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
    Vector2f text_pos(6, center.y() - 1);
    NVGcolor text_color = m_text_color.w() == 0 ? m_theme->m_text_color : m_text_color;
    if (!m_enabled)
        text_color = m_theme->m_disabled_text_color;

    auto  icon = m_icon && !m_pushed ? utf8(m_icon) : utf8(FA_CHECK);
    float ih   = font_size * icon_scale();
    nvgFontSize(ctx, ih);
    nvgFontFace(ctx, "icons");
    float iw = nvgTextBounds(ctx, 0, 0, icon.data(), nullptr, nullptr);

    if (m_caption != "")
        ih += m_size.y() * 0.15f;

    nvgFillColor(ctx, text_color);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    Vector2f icon_pos(m_pos.x() + 6, center.y() - 1);

    text_pos.x() = icon_pos.x() + ih + 2;

    if (m_pushed || m_icon)
        nvgText(ctx, icon_pos.x() + (ih - iw - 3) / 2, icon_pos.y() + 1, icon.data(), nullptr);

    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, m_theme->m_text_color_shadow);
    nvgText(ctx, text_pos.x(), text_pos.y(), m_caption.c_str(), nullptr);
    nvgFillColor(ctx, text_color);
    nvgText(ctx, text_pos.x(), text_pos.y() + 1, m_caption.c_str(), nullptr);

    if (!m_hotkey.size())
        return;

    // float    sw = nvgTextBounds(ctx, 0, 0, m_hotkey.c_str(), nullptr, nullptr);
    Vector2f hotkey_pos(m_pos.x() + m_size.x() - 8, center.y() - 1);

    nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, m_theme->m_text_color_shadow);
    nvgText(ctx, hotkey_pos.x(), hotkey_pos.y(), m_hotkey.c_str(), nullptr);
    nvgFillColor(ctx, m_theme->m_disabled_text_color);
    nvgText(ctx, hotkey_pos.x(), hotkey_pos.y() + 1, m_hotkey.c_str(), nullptr);
}

Separator::Separator(Widget *parent) : MenuItem(parent, "")
{
    set_enabled(false);
    set_fixed_height(PopupMenu::seperator_height);
}

void Separator::draw(NVGcontext *ctx)
{
    if (!m_enabled && m_pushed)
        m_pushed = false;

    nvgBeginPath(ctx);
    nvgMoveTo(ctx, m_pos.x() + 8, m_pos.y() + m_size.y() * 0.5f);
    nvgLineTo(ctx, m_pos.x() + m_size.x() - 8, m_pos.y() + m_size.y() * 0.5f);
    nvgStrokeColor(ctx, Color(89, 255));
    nvgStrokeWidth(ctx, 1.f);
    nvgStroke(ctx);
}

PopupMenu::PopupMenu(Widget *parent, Window *parent_window) : Popup(parent, parent_window)
{
    set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 3));
    set_visible(false);

    auto flat_theme                             = new Theme(screen()->nvg_context());
    flat_theme->m_standard_font_size            = 16;
    flat_theme->m_button_font_size              = 15;
    flat_theme->m_text_box_font_size            = 14;
    flat_theme->m_window_corner_radius          = 4;
    flat_theme->m_window_fill_unfocused         = Color(50, 255);
    flat_theme->m_window_fill_focused           = Color(52, 255);
    flat_theme->m_window_header_height          = 0;
    flat_theme->m_drop_shadow                   = Color(0, 100);
    flat_theme->m_button_corner_radius          = 4;
    flat_theme->m_border_light                  = flat_theme->m_transparent;
    flat_theme->m_border_dark                   = flat_theme->m_transparent;
    flat_theme->m_button_gradient_top_focused   = Color(77, 124, 233, 255);
    flat_theme->m_button_gradient_bot_focused   = flat_theme->m_button_gradient_top_focused;
    flat_theme->m_button_gradient_top_unfocused = flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_unfocused = flat_theme->m_transparent;
    flat_theme->m_button_gradient_top_pushed    = flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_pushed    = flat_theme->m_button_gradient_top_pushed;
    flat_theme->m_window_popup                  = Color(38, 255);
    flat_theme->m_text_color_shadow             = flat_theme->m_transparent;
    set_theme(flat_theme);
}

bool PopupMenu::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    if (Popup::mouse_button_event(p, button, down, modifiers))
    {
        // close popup and defocus all menu items
        if (down)
        {
            set_visible(false);
            m_parent_window->request_focus();

            for (auto it = m_children.begin(); it != m_children.end(); ++it) (*it)->mouse_enter_event(p, false);
        }
        return true;
    }
    return false;
}

void PopupMenu::draw(NVGcontext *ctx)
{
    if (!m_visible)
        return;

    int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

    nvgSave(ctx);
    nvgResetScissor(ctx);

    /* Draw a drop shadow */
    NVGpaint shadow_paint = nvgBoxGradient(ctx, m_pos.x(), m_pos.y() + 0.25 * ds, m_size.x(), m_size.y(), cr * 2,
                                           ds * 2, m_theme->m_drop_shadow, m_theme->m_transparent);

    nvgBeginPath(ctx);
    nvgRect(ctx, m_pos.x() - ds, m_pos.y() - ds + 0.25 * ds, m_size.x() + 2 * ds, m_size.y() + 2 * ds);
    nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr);
    nvgPathWinding(ctx, NVG_HOLE);
    nvgFillPaint(ctx, shadow_paint);
    nvgFill(ctx);

    /* Draw window */
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr);
    nvgStrokeWidth(ctx, 3.f);
    nvgStrokeColor(ctx, Color(6, 255));
    nvgStroke(ctx);
    nvgStrokeWidth(ctx, 2.f);
    nvgStrokeColor(ctx, Color(89, 255));
    nvgStroke(ctx);
    nvgFillColor(ctx, m_theme->m_window_popup);
    nvgFill(ctx);

    nvgRestore(ctx);

    Widget::draw(ctx);
}

Dropdown::Dropdown(Widget *parent, Mode mode, const string &caption) :
    MenuItem(parent, caption), m_selected_index(0), m_mode(mode)
{
    set_flags(Flags::ToggleButton);

    m_popup = new PopupMenu(screen(), window());
    m_popup->set_size(Vector2i(320, 250));
    m_popup->set_visible(false);

    if (m_mode == Menu)
        set_fixed_size(preferred_size(screen()->nvg_context()));

    set_fixed_height(PopupMenu::menu_item_height);
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
    {
        int w = 0;
        for (auto c : m_popup->children())
            if (auto i = dynamic_cast<MenuItem *>(c))
                w = std::max(w, i->preferred_text_size(ctx).x());
        return Vector2i(w, font_size + 5);
    }
    else if (m_mode == Menu)
        return MenuItem::preferred_size(ctx) - Vector2i(4 + font_size * icon_scale(), 0);
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
    int      font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    Vector2i offset;
    if (m_mode == ComboBox)
        offset = Vector2i(-3 - font_size * icon_scale(), -m_selected_index * PopupMenu::menu_item_height - 4);
    else if (m_mode == Menu)
        offset = Vector2i(0, height() + 4);
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
    else
        m_popup->set_position(compute_position());

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

MenuBar::MenuBar(Widget *parent, const string &title) : Window(parent, title)
{
    static auto menu_theme                      = new Theme(screen()->nvg_context());
    menu_theme->m_standard_font_size            = 16;
    menu_theme->m_button_font_size              = 15;
    menu_theme->m_text_box_font_size            = 14;
    menu_theme->m_window_corner_radius          = 0;
    menu_theme->m_window_fill_unfocused         = Color(25, 255);
    menu_theme->m_window_fill_focused           = Color(27, 255);
    menu_theme->m_drop_shadow                   = Color(0, 100);
    menu_theme->m_window_header_height          = 0;
    menu_theme->m_window_drop_shadow_size       = 0;
    menu_theme->m_button_corner_radius          = 4;
    menu_theme->m_border_light                  = menu_theme->m_transparent;
    menu_theme->m_border_dark                   = menu_theme->m_transparent;
    menu_theme->m_button_gradient_top_focused   = Color(77, 124, 233, 255);
    menu_theme->m_button_gradient_bot_focused   = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_bot_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_unfocused = menu_theme->m_transparent;
    menu_theme->m_button_gradient_bot_unfocused = menu_theme->m_transparent;
    menu_theme->m_window_popup                  = Color(38, 255);
    menu_theme->m_text_color_shadow             = menu_theme->m_transparent;

    set_theme(menu_theme);
    set_position(nanogui::Vector2i(0, 0));
    set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 2, 0));
}

Dropdown *MenuBar::add_menu(const string &name)
{
    auto menu = add<Dropdown>(Dropdown::Menu, name);
    menu->set_flags(Button::RadioButton);
    return menu;
}

bool MenuBar::mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    // if any menus are open, we switch menus via hover
    Dropdown *opened_menu = nullptr;
    for (auto c : children())
        if (auto d = dynamic_cast<Dropdown *>(c))
            if (d->popup()->visible())
            {
                opened_menu = d;
                break;
            }

    if (opened_menu)
    {
        auto hovered_item = dynamic_cast<Dropdown *>(find_widget(p));
        if (hovered_item && opened_menu != hovered_item)
        {
            opened_menu->set_pushed(false);
            opened_menu->popup()->set_visible(false);

            hovered_item->set_pushed(true);
            hovered_item->popup()->set_visible(true);
        }
    }

    return Window::mouse_motion_event(p, rel, button, modifiers);
}

bool MenuBar::process_hotkeys(int modifiers, int key)
{
    // spdlog::info("Hot keys:");
    for (auto c : children())
        if (auto menu = dynamic_cast<Dropdown *>(c))
        {
            for (auto c2 : menu->popup()->children())
                if (auto item = dynamic_cast<MenuItem *>(c2))
                {
                    auto [hotmod, hotkey] = item->hotkey();
                    // spdlog::info("{} > {}: {} + {}", menu->caption(), item->caption(), hotmod, hotkey);

                    if (key == hotkey && modifiers == hotmod)
                    {
                        spdlog::info("Would have processed: {} > {}: {} + {}", menu->caption(), item->caption(), hotmod,
                                     hotkey);
                        if (item->flags() & Button::NormalButton)
                        {
                            if (item->callback())
                                item->callback()();
                        }
                        else
                        {
                            if (item->change_callback())
                            {
                                spdlog::info("change callback: {}", item->pushed());
                                item->set_pushed(!item->pushed());
                                item->change_callback()(item->pushed());
                            }
                        }
                        return true;
                    }
                }
        }
    return false;
}

void MenuBar::add_shortcuts(HelpWindow *w)
{
    for (auto c : children())
        if (auto menu = dynamic_cast<Dropdown *>(c))
        {
            for (auto c2 : menu->popup()->children())
            {
                if (auto sep = dynamic_cast<Separator *>(c2))
                    if (!sep->visible())
                        w->add_separator(menu->caption());
                if (auto item = dynamic_cast<MenuItem *>(c2))
                    if (item->shortcut_string().size() && !item->visible())
                        w->add_shortcut(menu->caption(), item->shortcut_string(), item->caption());
            }
        }
}

PopupWrapper::PopupWrapper(Widget *parent, PopupMenu *menu) : Widget(parent), m_right_click_menu(menu)
{
    set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));
}

bool PopupWrapper::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
    if (m_enabled && m_right_click_menu)
    {
        if (m_right_click_menu->visible() && down)
        {
            m_right_click_menu->set_visible(false);
            return true;
        }

        if (down && (button == GLFW_MOUSE_BUTTON_2))
            m_right_click_menu->set_visible(!m_right_click_menu->visible());
        else if (down)
            m_right_click_menu->set_visible(false);

        m_right_click_menu->set_position(p + Vector2i(0, m_right_click_menu->size().y() / 2 - 10));
    }

    return Widget::mouse_button_event(p, button, down, modifiers);
}

NAMESPACE_END(nanogui)
