//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <functional>

/// function to plot the pixel at position (x,y)
using PlotPixelFunc = std::function<void(int x, int y)>;

/// function to plot the pixel at position (x,y) with signed distance to the line
using SmoothPlotPixelFunc = std::function<bool(int x, int y, float distance, int t0, int t1, int t)>;

/// Draw a line between pixels (x1,y1) and (x2,y2), calling plot on each pixel along the way
void draw_line(int x1, int y1, int x2, int y2, const PlotPixelFunc &plot);

/// Draw an antialiased line between pixels (x1,y1) and (x2,y2), calling plot on each pixel along the way
void draw_line(int x1, int y1, int x2, int y2, const SmoothPlotPixelFunc &plot);

/// Draw a cubic Catmull-Rom split between pixels (x1,y1) and (x2,y2), using (x0,y0) and (x3,y3) to compute smooth
/// tangents
void draw_CatmullRom(int p0x, int p0y, int p1x, int p1y, int p2x, int p2y, int p3x, int p3y, const PlotPixelFunc &plot,
                     float a = 0.0f);

void draw_quadratic(int p0x, int p0y, int p1x, int p1y, int p2x, int p2y, const PlotPixelFunc &plot, int levels = 2,
                    bool include_start = false, bool include_end = false);