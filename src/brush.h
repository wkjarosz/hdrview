//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once
#include "fwd.h"
#include "hdrimage.h"

class Brush
{
public:
    using SrcColorFunc  = std::function<Color4(int x, int y)>;
    using PlotPixelFunc = std::function<void(int x, int y, float)>;

    Brush(int radius = 15, float hardness = 0.0f, float flow = 1.0f);

    int   step() { return m_step; }
    int   spacing() const { return m_spacing; }
    int   radius() const { return m_size + 1; }
    float flow() const { return m_flow; }
    float hardness() const { return m_hardness; }
    int   angle() const { return m_angle; }
    int   roundness() const { return m_roundness; }

    void set_step(int);
    void set_spacing(int);
    void set_radius(int);
    void set_flow(float);
    void set_hardness(float s);
    void set_angle(int angle);
    void set_roundness(int roundness);

    void set_parameters(int radius, float hardness, int angle, int roundness, float flow, int spacing);

    void stamp_onto(HDRImage &raster, int x, int y, const SrcColorFunc &src_color, const Box2i &roi = Box2i());
    void stamp_onto(int x, int y, const PlotPixelFunc &plot_pixel, const Box2i &roi = Box2i());

private:
    void make_brush();

    Array2D<float> m_brush;
    int            m_size;
    float          m_flow;
    float          m_hardness;
    int            m_angle;
    int            m_roundness;
    int            m_spacing;
    int            m_spacing_pixels;
    int            m_step;
    int            m_last_x, m_last_y;
};
