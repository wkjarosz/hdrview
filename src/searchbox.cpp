//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// Adapted from Nanogui's TextBox class
//

#include "searchbox.h"
#include <iostream>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/theme.h>
#include <nanogui/window.h>
#include <regex>
#include <spdlog/spdlog.h>

NAMESPACE_BEGIN(nanogui)

SearchBox::SearchBox(Widget *parent, const std::string &value) : TextBox(parent, value) {}

void SearchBox::draw(NVGcontext *ctx)
{
    Widget::draw(ctx);

    NVGpaint bg  = nvgBoxGradient(ctx, m_pos.x() + 1, m_pos.y() + 1 + 1.0f, m_size.x() - 2, m_size.y() - 2, 3, 4,
                                  Color(255, 32), Color(32, 32));
    NVGpaint fg1 = nvgBoxGradient(ctx, m_pos.x() + 1, m_pos.y() + 1 + 1.0f, m_size.x() - 2, m_size.y() - 2, 3, 4,
                                  Color(150, 32), Color(32, 32));
    NVGpaint fg2 = nvgBoxGradient(ctx, m_pos.x() + 1, m_pos.y() + 1 + 1.0f, m_size.x() - 2, m_size.y() - 2, 3, 4,
                                  nvgRGBA(255, 0, 0, 100), nvgRGBA(255, 0, 0, 50));

    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + 1, m_pos.y() + 1 + 1.0f, m_size.x() - 2, m_size.y() - 2, 3);

    if (m_editable && focused())
        m_valid_format ? nvgFillPaint(ctx, fg1) : nvgFillPaint(ctx, fg2);
    else if (m_spinnable && m_mouse_down_pos.x() != -1)
        nvgFillPaint(ctx, fg1);
    else
        nvgFillPaint(ctx, bg);

    nvgFill(ctx);

    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + 0.5f, m_pos.y() + 0.5f, m_size.x() - 1, m_size.y() - 1, 2.5f);
    nvgStrokeColor(ctx, Color(0, 48));
    nvgStroke(ctx);

    nvgFontSize(ctx, font_size());
    nvgFontFace(ctx, "sans");
    Vector2i draw_pos(m_pos.x(), m_pos.y() + m_size.y() * 0.5f + 1);

    float x_spacing = m_size.y() * 0.3f;

    float unit_width = 0;

    if (m_units_image > 0)
    {
        int w, h;
        nvgImageSize(ctx, m_units_image, &w, &h);
        float unit_height = m_size.y() * 0.4f;
        unit_width        = w * unit_height / h;
        NVGpaint img_paint =
            nvgImagePattern(ctx, m_pos.x() + m_size.x() - x_spacing - unit_width, draw_pos.y() - unit_height * 0.5f,
                            unit_width, unit_height, 0, m_units_image, m_enabled ? 0.7f : 0.35f);
        nvgBeginPath(ctx);
        nvgRect(ctx, m_pos.x() + m_size.x() - x_spacing - unit_width, draw_pos.y() - unit_height * 0.5f, unit_width,
                unit_height);
        nvgFillPaint(ctx, img_paint);
        nvgFill(ctx);
        unit_width += 2;
    }
    else if (!m_units.empty())
    {
        unit_width = nvgTextBounds(ctx, 0, 0, m_units.c_str(), nullptr, nullptr);
        nvgFillColor(ctx, Color(255, m_enabled ? 64 : 32));
        nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(ctx, m_pos.x() + m_size.x() - x_spacing, draw_pos.y(), m_units.c_str(), nullptr);
        unit_width += 2;
    }

    float spin_arrows_width = 0.f;

    if (m_spinnable && !focused())
    {
        spin_arrows_width = 14.f;

        nvgFontFace(ctx, "icons");
        nvgFontSize(ctx, ((m_font_size < 0) ? m_theme->m_button_font_size : m_font_size) * icon_scale());

        bool spinning = m_mouse_down_pos.x() != -1;

        /* up button */ {
            bool hover = m_mouse_focus && spin_area(m_mouse_pos) == SpinArea::Top;
            nvgFillColor(ctx,
                         (m_enabled && (hover || spinning)) ? m_theme->m_text_color : m_theme->m_disabled_text_color);
            auto icon = utf8(m_theme->m_text_box_up_icon);
            nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            Vector2f icon_pos(m_pos.x() + 4.f, m_pos.y() + m_size.y() / 2.f - x_spacing / 2.f);
            nvgText(ctx, icon_pos.x(), icon_pos.y(), icon.data(), nullptr);
        }

        /* down button */ {
            bool hover = m_mouse_focus && spin_area(m_mouse_pos) == SpinArea::Bottom;
            nvgFillColor(ctx,
                         (m_enabled && (hover || spinning)) ? m_theme->m_text_color : m_theme->m_disabled_text_color);
            auto icon = utf8(m_theme->m_text_box_down_icon);
            nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            Vector2f icon_pos(m_pos.x() + 4.f, m_pos.y() + m_size.y() / 2.f + x_spacing / 2.f + 1.5f);
            nvgText(ctx, icon_pos.x(), icon_pos.y(), icon.data(), nullptr);
        }

        nvgFontSize(ctx, font_size());
        nvgFontFace(ctx, "sans");
    }

    switch (m_alignment)
    {
    case Alignment::Left:
        nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        draw_pos.x() += x_spacing + spin_arrows_width;
        break;
    case Alignment::Right:
        nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        draw_pos.x() += m_size.x() - unit_width - x_spacing;
        break;
    case Alignment::Center:
        nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        draw_pos.x() += m_size.x() * 0.5f;
        break;
    }

    nvgFontSize(ctx, font_size());
    // Change compared to nanogui::TextBox to show the placeholder text when focused but empty
    nvgFillColor(ctx, m_enabled && ((!m_committed || !m_value.empty()) && !m_value_temp.empty())
                          ? m_theme->m_text_color
                          : m_theme->m_disabled_text_color);

    // clip visible text area
    float clip_x      = m_pos.x() + x_spacing + spin_arrows_width - 1.0f;
    float clip_y      = m_pos.y() + 1.0f;
    float clip_width  = m_size.x() - unit_width - spin_arrows_width - 2 * x_spacing + 2.0f;
    float clip_height = m_size.y() - 3.0f;

    nvgSave(ctx);
    nvgIntersectScissor(ctx, clip_x, clip_y, clip_width, clip_height);

    Vector2i old_draw_pos(draw_pos);
    draw_pos.x() += m_text_offset;

    if (m_committed)
    {
        nvgText(ctx, draw_pos.x(), draw_pos.y(), m_value.empty() ? m_placeholder.c_str() : m_value.c_str(), nullptr);
    }
    else
    {
        const int        max_glyphs = 1024;
        NVGglyphPosition glyphs[max_glyphs];
        float            text_bound[4];
        nvgTextBounds(ctx, draw_pos.x(), draw_pos.y(), m_value_temp.c_str(), nullptr, text_bound);
        float lineh = text_bound[3] - text_bound[1];

        // find cursor positions
        int nglyphs =
            nvgTextGlyphPositions(ctx, draw_pos.x(), draw_pos.y(), m_value_temp.c_str(), nullptr, glyphs, max_glyphs);
        update_cursor(ctx, text_bound[2], glyphs, nglyphs);

        // compute text offset
        int   prev_cpos = m_cursor_pos > 0 ? m_cursor_pos - 1 : 0;
        int   next_cpos = m_cursor_pos < nglyphs ? m_cursor_pos + 1 : nglyphs;
        float prev_cx   = cursor_index_to_position(prev_cpos, text_bound[2], glyphs, nglyphs);
        float next_cx   = cursor_index_to_position(next_cpos, text_bound[2], glyphs, nglyphs);

        if (next_cx > clip_x + clip_width)
            m_text_offset -= next_cx - (clip_x + clip_width) + 1;
        if (prev_cx < clip_x)
            m_text_offset += clip_x - prev_cx + 1;

        draw_pos.x() = old_draw_pos.x() + m_text_offset;

        // draw text with offset
        // Change compared to nanogui::TextBox to show the placeholder text when focused but empty
        std::string txt = m_value_temp.size() ? m_value_temp : m_placeholder;
        nvgText(ctx, draw_pos.x(), draw_pos.y(), txt.c_str(), nullptr);

        nvgTextBounds(ctx, draw_pos.x(), draw_pos.y(), m_value_temp.c_str(), nullptr, text_bound);

        // recompute cursor positions
        nglyphs =
            nvgTextGlyphPositions(ctx, draw_pos.x(), draw_pos.y(), m_value_temp.c_str(), nullptr, glyphs, max_glyphs);

        if (m_cursor_pos > -1)
        {
            if (m_selection_pos > -1)
            {
                float caretx = cursor_index_to_position(m_cursor_pos, text_bound[2], glyphs, nglyphs);
                float selx   = cursor_index_to_position(m_selection_pos, text_bound[2], glyphs, nglyphs);

                if (caretx > selx)
                    std::swap(caretx, selx);

                // draw selection
                nvgBeginPath(ctx);
                nvgFillColor(ctx, nvgRGBA(255, 255, 255, 80));
                nvgRect(ctx, caretx, draw_pos.y() - lineh * 0.5f, selx - caretx, lineh);
                nvgFill(ctx);
            }

            float caretx = cursor_index_to_position(m_cursor_pos, text_bound[2], glyphs, nglyphs);

            // draw cursor
            nvgBeginPath(ctx);
            nvgMoveTo(ctx, caretx, draw_pos.y() - lineh * 0.5f);
            nvgLineTo(ctx, caretx, draw_pos.y() + lineh * 0.5f);
            nvgStrokeColor(ctx, nvgRGBA(255, 192, 0, 255));
            nvgStrokeWidth(ctx, 1.0f);
            nvgStroke(ctx);
        }
    }
    nvgRestore(ctx);
}

bool SearchBox::focus_event(bool focused)
{
    auto ret = TextBox::focus_event(focused);
    if (m_editable & !focused)
        m_value_temp = "";
    return ret;
}

bool SearchBox::keyboard_event(int key, int scancode, int action, int modifiers)
{
    std::string backup  = m_value_temp;
    bool        ret     = true;
    bool        handled = true;
    if (m_editable && focused() && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        // don't capture up and down arrow presses
        if (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN)
            ret = false;
        // Command+Left or Right should act like Home and End
        else if (key == GLFW_KEY_LEFT && (modifiers & GLFW_MOD_SUPER))
            ret = TextBox::keyboard_event(GLFW_KEY_HOME, scancode, action, modifiers ^ GLFW_MOD_SUPER);
        else if (key == GLFW_KEY_RIGHT && (modifiers & GLFW_MOD_SUPER))
            ret = TextBox::keyboard_event(GLFW_KEY_END, scancode, action, modifiers ^ GLFW_MOD_SUPER);
        else if (key == GLFW_KEY_ESCAPE)
        {
            if (!m_committed)
            {
                m_value_temp = m_value;
                focus_event(false);
            }
            ret = true;
        }
        // move the cursor to the end of the pasted content
        else if (key == GLFW_KEY_V && modifiers == SYSTEM_COMMAND_MOD)
        {
            delete_selection();
            size_t prev_size = m_value_temp.size();
            paste_from_clipboard();
            m_cursor_pos += m_value_temp.size() - prev_size;
            ret = true;
        }
        else
            handled = false;
    }
    else
        handled = false;

    if (!handled)
        ret = TextBox::keyboard_event(key, scancode, action, modifiers);

    if (m_value_temp != backup)
    {
        if (m_temporary_callback && !m_temporary_callback(m_value_temp))
            m_value_temp = backup;
    }

    return ret;
}

bool SearchBox::keyboard_character_event(unsigned int codepoint)
{
    std::string backup = m_value_temp;

    bool ret = TextBox::keyboard_character_event(codepoint);

    if (m_value_temp != backup)
    {
        if (m_temporary_callback && !m_temporary_callback(m_value_temp))
            m_value_temp = backup;
    }

    return ret;
}

NAMESPACE_END(nanogui)
