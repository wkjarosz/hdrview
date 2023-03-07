//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "well.h"
#include <nanogui/opengl.h>

NAMESPACE_BEGIN(nanogui)

Well::Well(Widget *parent, float radius, const Color &inner, const Color &outer, const Color &border) :
    Widget(parent), m_radius(radius), m_inner_color(inner), m_outer_color(outer), m_border_color(border)
{
}

void Well::draw(NVGcontext *ctx)
{
    {
        nvgBeginPath(ctx);
        nvgRoundedRect(ctx, m_pos.x() + 1, m_pos.y() + 1, m_size.x() - 2, m_size.y() - 2, m_radius);
        nvgFillColor(ctx, m_inner_color);
        nvgFill(ctx);
        // nvgStroke(ctx);
    }

    nvgSave(ctx);
    nvgIntersectScissor(ctx, m_pos.x() + 1, m_pos.y() + 1, m_size.x() - 2, m_size.y() - 2);
    Widget::draw(ctx);
    nvgRestore(ctx);

    {
        NVGpaint paint = nvgBoxGradient(ctx, m_pos.x() + 1, m_pos.y() + 1, m_size.x() - 2, m_size.y() - 2, m_radius,
                                        m_radius + 5, Color(0, 0), m_outer_color);
        nvgBeginPath(ctx);
        nvgRoundedRect(ctx, m_pos.x() + 1, m_pos.y() + 1, m_size.x() - 2, m_size.y() - 2, m_radius);
        nvgFillPaint(ctx, paint);
        nvgFill(ctx);
        nvgStrokeWidth(ctx, 1.0f);
        nvgStrokeColor(ctx, m_border_color);
        nvgStroke(ctx);
    }
}

AlignedLabel::AlignedLabel(Widget *parent, const std::string &caption, const std::string &font, int font_size) :
    Label(parent, caption, font, font_size), m_alignment(Alignment::Right)
{
}

void AlignedLabel::draw(NVGcontext *ctx)
{
    // Label::draw(ctx);
    Widget::draw(ctx);
    nvgFontFace(ctx, m_font.c_str());
    nvgFontSize(ctx, font_size());
    nvgFillColor(ctx, m_color);

    int draw_pos_x = m_pos.x();

    auto vert_align = m_fixed_size.x() > 0 ? NVG_ALIGN_TOP : NVG_ALIGN_MIDDLE;
    switch (m_alignment)
    {
    case Alignment::Left: nvgTextAlign(ctx, NVG_ALIGN_LEFT | vert_align); break;
    case Alignment::Right:
        nvgTextAlign(ctx, NVG_ALIGN_RIGHT | vert_align);
        draw_pos_x += m_size.x();
        break;
    case Alignment::Center:
        nvgTextAlign(ctx, NVG_ALIGN_CENTER | vert_align);
        draw_pos_x += m_size.x() * 0.5f;
        break;
    }

    if (m_fixed_size.x() > 0)
        nvgTextBox(ctx, m_pos.x(), m_pos.y(), m_fixed_size.x(), m_caption.c_str(), nullptr);
    else
        nvgText(ctx, draw_pos_x, m_pos.y() + m_size.y() * 0.5f, m_caption.c_str(), nullptr);
}

NAMESPACE_END(nanogui)