//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "Well.h"
#include <nanogui/opengl.h>

using namespace nanogui;

Well::Well(Widget *parent, float radius, const Color & inner, const Color & outer)
    : Widget(parent), m_radius(radius), m_innerColor(inner), m_outerColor(outer)
{

}

void Well::draw(NVGcontext* ctx)
{
    NVGpaint paint = nvgBoxGradient(ctx, m_pos.x() + 1, m_pos.y() + 1,
                                    m_size.x()-2, m_size.y()-2, m_radius, m_radius+1,
                                    m_innerColor, m_outerColor);
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y()-1, m_radius);
    nvgFillPaint(ctx, paint);
    nvgFill(ctx);

    Widget::draw(ctx);
}