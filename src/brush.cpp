//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "brush.h"

#include "color.h"
#include "common.h"
#include <nanogui/vector.h>

using namespace nanogui;

Brush::Brush(int radius, float hardness, float flow) :
    m_size(-1), m_flow(flow), m_hardness(hardness), m_angle(0), m_roundness(100), m_spacing(0), m_spacing_pixels(0),
    m_step(0), m_last_x(-1), m_last_y(-1)
{
    set_radius(radius);
}

void Brush::set_step(int step) { m_step = step; }

void Brush::set_spacing(int spacing)
{
    m_spacing        = spacing;
    m_spacing_pixels = std::max(1, int(spacing / 100.0f * std::min(m_brush.width(), m_brush.height())));
}

void Brush::set_radius(int radius)
{
    if (m_size != radius - 1)
    {
        m_size = radius - 1;
        make_brush();
    }
}

void Brush::set_hardness(float hardness)
{
    if (m_hardness != hardness)
    {
        m_hardness = hardness;
        make_brush();
    }
}

void Brush::set_angle(int angle)
{
    if (m_angle != angle)
    {
        m_angle = angle;
        make_brush();
    }
}

void Brush::set_roundness(int roundness)
{
    if (m_roundness != roundness)
    {
        m_roundness = roundness;
        make_brush();
    }
}

void Brush::set_flow(float flow) { m_flow = flow; }

void Brush::set_parameters(int radius, float hardness, int angle, int roundness, float flow, int spacing)
{
    if (m_size != radius - 1 || m_hardness != hardness || m_angle != angle || m_roundness != roundness ||
        m_spacing != spacing || m_flow != flow)
    {
        m_size      = radius - 1;
        m_hardness  = hardness;
        m_flow      = flow;
        m_angle     = angle;
        m_roundness = roundness;
        set_spacing(spacing);
        make_brush();
    }
}

void Brush::make_brush()
{
    int n = 2 * m_size + 1;
    m_brush.resize(n, n);

    float start = m_hardness * m_size;
    float end   = m_size + 1;

    float b         = 100.0f / m_roundness;
    float theta     = 2 * M_PI * m_angle / 360.0f;
    float sin_theta = sin(theta);
    float cos_theta = cos(theta);

    int min_x = n, max_x = 0, min_y = n, max_y = 0;
    for (int y = 0; y < n; y++)
    {
        for (int x = 0; x < n; x++)
        {
            Vector2f uv((x - m_size) * cos_theta + (y - m_size) * sin_theta,
                        b * ((y - m_size) * cos_theta - (x - m_size) * sin_theta));
            m_brush(x, y) = 1.0f - pow(smoothStep(start, end, norm(uv)), 0.8f);
            if (m_brush(x, y) > 0.00001f)
            {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }
    }

    // resize the brush array to fit the brush
    // Array2D<float> *oldBrush = m_brush;
    // m_brush                  = new Array2D<float>(max_x - min_x + 1, max_y - min_y + 1);
    // for (int y = min_y; y <= max_y; y++)
    //     for (int x = min_x; x <= max_x; x++) (*m_brush)(x - min_x, y - min_y) = (*oldBrush)(x, y);
    // delete oldBrush;

    set_spacing(m_spacing);
}

void Brush::stamp_onto(int x, int y, const PlotPixelFunc &plot_pixel, const Box2i &roi)
{
    if (!(m_step++ == 0) && m_spacing)
    {
        int distance2 = sqr(x - m_last_x) + sqr(y - m_last_y);
        if (distance2 < m_spacing_pixels * m_spacing_pixels)
            return;
    }

    m_last_x       = x;
    m_last_y       = y;
    int size_x     = (m_brush.width() - 1) / 2;
    int size_y     = (m_brush.height() - 1) / 2;
    int c_offset_x = x - size_x;
    int c_offset_y = y - size_y;
    int i_start    = std::clamp(x - size_x, roi.min.x(), roi.max.x());
    int i_end      = std::clamp(x + size_x + 1, roi.min.x(), roi.max.x());

    int j_start = std::clamp(y - size_y, roi.min.y(), roi.max.y());
    int j_end   = std::clamp(y + size_y + 1, roi.min.y(), roi.max.y());

    for (int j = j_start; j < j_end; j++)
        for (int i = i_start; i < i_end; i++) plot_pixel(i, j, m_flow * m_brush(i - c_offset_x, j - c_offset_y));
}

void Brush::stamp_onto(HDRImage &raster, int x, int y, const SrcColorFunc &src_color, const Box2i &roi)
{
    auto plot_pixel = [&raster, &src_color](int i, int j, float a)
    {
        auto   c4 = src_color(i, j);
        Color4 c3(c4.r, c4.g, c4.b, 1.f);
        float  alpha = a * c4.a;
        raster(i, j) = c3 * alpha + raster(i, j) * (1.0f - alpha);
    };

    return stamp_onto(x, y, plot_pixel, roi);
}
