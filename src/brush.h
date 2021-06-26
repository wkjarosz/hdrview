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

    int   step() const { return m_step; }
    int   radius() const { return m_size + 1; }
    float spacing() const { return m_spacing; }
    float flow() const { return m_flow; }
    float hardness() const { return m_hardness; }
    float angle() const { return m_angle; }
    float roundness() const { return m_roundness; }

    void set_step(int);
    void set_spacing(float);
    void set_radius(int);
    void set_flow(float flow) { m_flow = std::clamp(flow, 0.f, 1.f); }
    void set_hardness(float s);
    void set_angle(float angle);
    void set_roundness(float roundness);

    void stamp_onto(HDRImage &raster, int x, int y, const SrcColorFunc &src_color, const Box2i &roi = Box2i());
    void stamp_onto(int x, int y, const PlotPixelFunc &plot_pixel, const Box2i &roi = Box2i());

private:
    void make_brush();

    Array2D<float> m_brush;
    int            m_size;           ///< in pixels
    float          m_flow;           ///< between 0 and 1
    float          m_hardness;       ///< between 0 and 1
    float          m_angle;          ///< in degrees
    float          m_roundness;      ///< between 0 and 1
    float          m_spacing;        ///< between 0 and 1
    int            m_spacing_pixels; ///< in pixels
    int            m_step;           ///< how many steps since the last stamp?
    int            m_last_x, m_last_y;
};
