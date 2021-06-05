//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "well.h"
#include <nanogui/opengl.h>


NAMESPACE_BEGIN(nanogui)

Well::Well(Widget *parent, float radius, const Color & inner, const Color & outer)
    : Widget(parent), m_radius(radius), m_inner_color(inner), m_outer_color(outer)
{

}

void Well::draw(NVGcontext* ctx)
{
    NVGpaint paint = nvgBoxGradient(ctx, m_pos.x() + 1, m_pos.y() + 1,
                                    m_size.x()-2, m_size.y()-2, m_radius, m_radius+5,
                                    m_inner_color, m_outer_color);
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x()+1, m_pos.y()+1, m_size.x()-2, m_size.y()-2, m_radius);
    nvgFillPaint(ctx, paint);
    nvgFill(ctx);
    nvgStrokeWidth(ctx, 1.0f);
    nvgStrokeColor(ctx, Color(0, 150));
    nvgStroke(ctx);

    Widget::draw(ctx);
}

NAMESPACE_END(nanogui)