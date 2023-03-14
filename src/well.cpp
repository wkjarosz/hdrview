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

NAMESPACE_END(nanogui)