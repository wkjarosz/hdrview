//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "alignedlabel.h"
#include <nanogui/opengl.h>

NAMESPACE_BEGIN(nanogui)

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