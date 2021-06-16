//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "drawline.h"
#include "common.h"
#include <cmath>
#include <nanogui/vector.h>

#include <spdlog/spdlog.h>

#include <spdlog/fmt/ostr.h>

using namespace nanogui;

namespace
{

inline void draw_line_x(int x1, int y1, int x2, int incdec, int e, int e_noinc, int e_inc, float inv_denom, int dx,
                        const SmoothPlotPixelFunc &plot)
{
    int   two_v_dx         = 0;
    float two_dx_inv_denom = 2.0f * dx * inv_denom;
    for (int x = x1; x <= x2; x++)
    {
        if (!plot(x, y1, two_v_dx * inv_denom, x1, x2, x))
            break;

        float offset = incdec * two_v_dx * inv_denom;
        plot(x, y1 + 1, two_dx_inv_denom - offset, x1, x2, x);
        plot(x, y1 - 1, two_dx_inv_denom + offset, x1, x2, x);

        if (e < 0)
        {
            two_v_dx = e + dx;
            e += e_noinc;
        }
        else
        {
            two_v_dx = e - dx;
            y1 += incdec;
            e += e_inc;
        }
    }
}

inline void draw_line_y(int x1, int y1, int y2, int incdec, int e, int e_noinc, int e_inc, float inv_denom, int dy,
                        const SmoothPlotPixelFunc &plot)
{
    int   two_v_dy         = 0;
    float two_dy_inv_denom = 2.0f * dy * inv_denom;
    for (int y = y1; y <= y2; y++)
    {
        if (!plot(x1, y, two_v_dy * inv_denom, y1, y2, y))
            break;

        float offset = incdec * two_v_dy * inv_denom;
        plot(x1 + 1, y, two_dy_inv_denom - offset, y1, y2, y);
        plot(x1 - 1, y, two_dy_inv_denom + offset, y1, y2, y);

        if (e < 0)
        {
            two_v_dy = e + dy;
            e += e_noinc;
        }
        else
        {
            two_v_dy = e - dy;
            x1 += incdec;
            e += e_inc;
        }
    }
}

//
// from: https://en.wikipedia.org/wiki/Centripetal_Catmull–Rom_spline
//
inline float get_t(float t, float alpha, const Vector2f &p0, const Vector2f &p1)
{
    auto  d = p1 - p0;
    float a = dot(d, d);
    float b = pow(a, alpha * .5f);
    return (b + t);
}

inline Vector2f CatmullRom(const Vector2f &p0, const Vector2f &p1, const Vector2f &p2, const Vector2f &p3, float t,
                           float alpha = .5f)
{
    float t0    = 0.0f;
    float t1    = get_t(t0, alpha, p0, p1);
    float t2    = get_t(t1, alpha, p1, p2);
    float t3    = get_t(t2, alpha, p2, p3);
    t           = lerp(t1, t2, t);
    Vector2f A1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1;
    Vector2f A2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
    Vector2f A3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;
    Vector2f B1 = (t2 - t) / (t2 - t0) * A1 + (t - t0) / (t2 - t0) * A2;
    Vector2f B2 = (t3 - t) / (t3 - t1) * A2 + (t - t1) / (t3 - t1) * A3;
    Vector2f C  = (t2 - t) / (t2 - t1) * B1 + (t - t1) / (t2 - t1) * B2;
    return C;
}

inline void chaikin(std::vector<Vector2f> &out, const std::vector<Vector2f> &in)
{
    for (size_t i = 0; i < in.size() - 1; ++i)
    {
        out.push_back(lerp(in[i], in[i + 1], 0.25f));
        out.push_back(lerp(in[i], in[i + 1], 0.75f));
    }
}

} // namespace

void draw_line(int x1, int y1, int x2, int y2, const PlotPixelFunc &plot)
{
    // Difference of x and y values
    int dx = x2 - x1;
    int dy = y2 - y1;

    // Absolute values of differences
    int ix = dx < 0 ? -dx : dx;
    int iy = dy < 0 ? -dy : dy;

    // Larger of the x and y differences
    int inc = ix > iy ? ix : iy;

    int x = 0, y = 0;

    // plot at plotx, ploty
    // plot(x1, y1);

    for (int i = 0; i < inc; i++)
    {
        x += ix;
        y += iy;

        if (x > inc)
        {
            x -= inc;
            if (dx < 0)
                x1--;
            else
                x1++;
        }

        if (y > inc)
        {
            y -= inc;
            if (dy < 0)
                y1--;
            else
                y1++;
        }

        // plot at plotx, ploty
        plot(x1, y1);
    }
}

void draw_line(int x1, int y1, int x2, int y2, const SmoothPlotPixelFunc &plot)
{
    if (x1 > x2)
    {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }

    int dx = x2 - x1;
    int dy = y2 - y1;

    if (dy <= dx && dy >= 0) // 0 <= slope <= 1
        draw_line_x(x1, y1, x2, 1, 2 * dy - dx, 2 * dy, 2 * (dy - dx), 1.0f / (2.0f * sqrtf(dx * dx + dy * dy)), dx,
                    plot);

    else if (dy > dx && dy >= 0) // 1 < slope <= infinity
        draw_line_y(x1, y1, y2, 1, 2 * dx - dy, 2 * dx, 2 * (dx - dy), 1.0f / (2.0f * sqrtf(dx * dx + dy * dy)), dy,
                    plot);

    else if (-dy <= dx && dy <= 0) // 0 >= slope >= -1
        draw_line_x(x1, y1, x2, -1, -2 * dy - dx, -2 * dy, 2 * (-dy - dx), 1.0f / (2.0f * sqrtf(dx * dx + dy * dy)), dx,
                    plot);

    else if (-dy > dx && dy <= 0) // -1 > slope >= 0
        draw_line_y(x2, y2, y1, -1, -2 * dx - dy, 2 * dx, -2 * (-dx - dy), 1.0f / (2.0f * sqrtf(dx * dx + dy * dy)),
                    -dy, plot);
}

void draw_CatmullRom(int p0x, int p0y, int p1x, int p1y, int p2x, int p2y, int p3x, int p3y, const PlotPixelFunc &plot,
                     float a)
{
    Vector2f p0(p0x, p0y), p1(p1x, p1y), p2(p2x, p2y), p3(p3x, p3y);

    // desired tangents at p1 and p2
    Vector2f m1 = (p2 - p0) * 0.5f;
    Vector2f m2 = (p3 - p1) * 0.5f;

    Vector2f bp0(p1), bp1(p1 + m1 / 3), bp2(p2 - m2 / 3), bp3(p2);

    constexpr int max_segments = 16;
    Vector2f      points[max_segments + 1];
    float         len          = norm(bp0 - bp1) + norm(bp1 - bp2) + norm(bp2 - bp3);
    int           num_segments = std::clamp((int)round(len / 10), 1, max_segments);

    for (int i = 0; i <= num_segments; ++i) points[i] = CatmullRom(p0, p1, p2, p3, i / float(num_segments), a);

    for (int i = 0; i < num_segments; ++i)
        draw_line(round(points[i].x()), round(points[i].y()), round(points[i + 1].x()), round(points[i + 1].y()), plot);
}

void draw_quadratic(int p0x, int p0y, int p1x, int p1y, int p2x, int p2y, const PlotPixelFunc &plot, int levels,
                    bool include_start, bool include_end)
{
    size_t max_size = pow(2, levels) + 2;

    // allocate two buffers that we'll ping-pong between
    std::vector<Vector2f> a1;
    std::vector<Vector2f> a2;
    a1.reserve(max_size);
    a2.reserve(max_size);

    a1.push_back(Vector2f(p0x, p0y));
    a1.push_back(Vector2f(p1x, p1y));
    a1.push_back(Vector2f(p2x, p2y));

    std::vector<Vector2f> *in = &a2, *out = &a1;

    for (int level = 0; level < levels; ++level)
    {
        std::swap(in, out);
        out->clear();
        chaikin(*out, *in);
    }

    if (include_start)
        out->front() = Vector2f(p0x, p0y);
    if (include_end)
        out->back() = Vector2f(p2x, p2y);

    size_t end = include_end ? out->size() - 1 : out->size() - 2;
    for (size_t i = 0; i < end; ++i)
        draw_line(round((*out)[i].x()), round((*out)[i].y()), round((*out)[i + 1].x()), round((*out)[i + 1].y()), plot);
}
