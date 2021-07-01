//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "popupmenu.h"
#include <cassert>
#include <nanogui/button.h>
#include <nanogui/icons.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/theme.h>
#include <nanogui/vscrollpanel.h>
#include <spdlog/spdlog.h>

#include <spdlog/fmt/ostr.h>

NAMESPACE_BEGIN(nanogui)

PopupMenu::PopupMenu(Widget *parent, Window *parent_window) : Popup(parent, parent_window)
{
    set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 3));
    set_visible(false);

    auto flat_theme                             = new Theme(screen()->nvg_context());
    flat_theme                                  = new Theme(screen()->nvg_context());
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

Button *PopupMenu::add_item(const std::string &name, int icon)
{
    if (name == "")
    {
        return add<Separator>();
    }
    else
    {
        return add<Item>(name, icon);
    }
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

PopupMenu::Item::Item(Widget *parent, const std::string &caption, int button_icon) :
    Button(parent, caption, button_icon)
{
    set_fixed_height(PopupMenu::menu_item_height);
    m_icon_position = IconPosition::Left;
}

Vector2i PopupMenu::Item::preferred_size(NVGcontext *ctx) const
{
    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    float tw = nvgTextBounds(ctx, 0, 0, m_caption.c_str(), nullptr, nullptr);
    float iw = 0.0f, ih = font_size;

    {
        ih *= icon_scale();
        nvgFontFace(ctx, "icons");
        nvgFontSize(ctx, ih);
        iw = nvgTextBounds(ctx, 0, 0, utf8(m_icon).data(), nullptr, nullptr) + m_size.y() * 0.15f;
    }

    // if (m_icon)
    // {
    //     if (nvg_is_font_icon(m_icon))
    //     {
    //         ih *= icon_scale();
    //         nvgFontFace(ctx, "icons");
    //         nvgFontSize(ctx, ih);
    //         iw = nvgTextBounds(ctx, 0, 0, utf8(m_icon).data(), nullptr, nullptr) + m_size.y() * 0.15f;
    //     }
    //     else
    //     {
    //         int w, h;
    //         ih *= 0.9f;
    //         nvgImageSize(ctx, m_icon, &w, &h);
    //         iw = w * ih / h;
    //     }
    // }
    return Vector2i((int)(tw + iw) + 24, font_size + 10);
}

void PopupMenu::Item::draw(NVGcontext *ctx)
{
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
    Vector2f text_pos(6, center.y() - 1);
    NVGcolor text_color = m_text_color.w() == 0 ? m_theme->m_text_color : m_text_color;
    if (!m_enabled)
        text_color = m_theme->m_disabled_text_color;

    {
        auto  check = utf8(FA_CHECK);
        float ch    = font_size * icon_scale();
        nvgFontSize(ctx, ch);
        nvgFontFace(ctx, "icons");
        float cw = nvgTextBounds(ctx, 0, 0, check.data(), nullptr, nullptr);

        if (m_caption != "")
            cw += m_size.y() * 0.15f;

        nvgFillColor(ctx, text_color);
        nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        Vector2f check_pos(m_pos.x() + 6, center.y() - 1);

        text_pos.x() = check_pos.x() + cw + 2;

        if (m_checked)
            nvgText(ctx, check_pos.x(), check_pos.y() + 1, check.data(), nullptr);
    }

    // if (m_icon)
    // {
    //     auto icon = utf8(m_icon);

    //     float iw, ih = font_size;
    //     if (nvg_is_font_icon(m_icon))
    //     {
    //         ih *= icon_scale();
    //         nvgFontSize(ctx, ih);
    //         nvgFontFace(ctx, "icons");
    //         iw = nvgTextBounds(ctx, 0, 0, icon.data(), nullptr, nullptr);
    //     }
    //     else
    //     {
    //         int w, h;
    //         ih *= 0.9f;
    //         nvgImageSize(ctx, m_icon, &w, &h);
    //         iw = w * ih / h;
    //     }
    //     if (m_caption != "")
    //         iw += m_size.y() * 0.15f;
    //     nvgFillColor(ctx, text_color);
    //     nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    //     Vector2f icon_pos = center;
    //     icon_pos.y() -= 1;

    //     icon_pos.x() = m_pos.x() + 8;
    //     // text_pos.x() = icon_pos.x() + iw;

    //     // if (m_icon_position == IconPosition::LeftCentered)
    //     // {
    //     //     icon_pos.x() -= (tw + iw) * 0.5f;
    //     //     text_pos.x() += iw;
    //     // }
    //     // else if (m_icon_position == IconPosition::RightCentered)
    //     // {
    //     //     // text_pos.x() -= iw * 0.5f;
    //     //     icon_pos.x() += tw * 0.5f;
    //     // }
    //     // else if (m_icon_position == IconPosition::Left)
    //     // {
    //     //     icon_pos.x() = m_pos.x() + 8;
    //     //     text_pos.x() = icon_pos.x() + iw;
    //     // }
    //     // else if (m_icon_position == IconPosition::Right)
    //     // {
    //     //     icon_pos.x() = m_pos.x() + m_size.x() - iw - 8;
    //     // }

    //     if (nvg_is_font_icon(m_icon))
    //     {
    //         nvgText(ctx, icon_pos.x(), icon_pos.y() + 1, icon.data(), nullptr);
    //     }
    //     else
    //     {
    //         NVGpaint img_paint =
    //             nvgImagePattern(ctx, icon_pos.x(), icon_pos.y() - ih / 2, iw, ih, 0, m_icon, m_enabled ? 0.5f :
    //             0.25f);

    //         nvgFillPaint(ctx, img_paint);
    //         nvgFill(ctx);
    //     }
    // }

    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, m_theme->m_text_color_shadow);
    nvgText(ctx, text_pos.x(), text_pos.y(), m_caption.c_str(), nullptr);
    nvgFillColor(ctx, text_color);
    nvgText(ctx, text_pos.x(), text_pos.y() + 1, m_caption.c_str(), nullptr);
}

PopupMenu::Separator::Separator(Widget *parent) : Button(parent, "")
{
    set_enabled(false);
    set_fixed_height(PopupMenu::seperator_height);
}

void PopupMenu::Separator::draw(NVGcontext *ctx)
{
    if (!m_enabled && m_pushed)
        m_pushed = false;

    Button::draw(ctx);

    nvgBeginPath(ctx);
    nvgMoveTo(ctx, m_pos.x() + 8, m_pos.y() + m_size.y() * 0.5f);
    nvgLineTo(ctx, m_pos.x() + m_size.x() - 8, m_pos.y() + m_size.y() * 0.5f);
    nvgStrokeColor(ctx, Color(89, 255));
    nvgStrokeWidth(ctx, 1.f);
    nvgStroke(ctx);
}

NAMESPACE_END(nanogui)
