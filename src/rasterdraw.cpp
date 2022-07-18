//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "rasterdraw.h"
#include "common.h"
#include <cmath>
#include <array>
#include <nanogui/vector.h>

#include <spdlog/spdlog.h>

#include <spdlog/fmt/ostr.h>

using namespace nanogui;

namespace
{

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

// The following code is manually converted from Cem Yuksel's javascript demo:
// A Class of C2 Interpolating Splines - Cem Yuksel, University of Utah
// http://www.cemyuksel.com/research/interpolating_splines/

inline auto vcross(const Vector2f &v0, const Vector2f &v1) { return v0.x() * v1.y() - v0.y() * v1.x(); }

struct Circle
{
    Vector2f             center;
    Vector2f             axis1, axis2;
    std::array<float, 3> limits;
};

void get_circle(Circle &circle, const Vector2f &point_j, const Vector2f &point_i, const Vector2f &point_k)
{
    auto vec1 = point_i - point_j;
    auto mid1 = point_j + vec1 / 2.f;
    auto dir1 = Vector2f(-vec1.y(), vec1.x());
    auto vec2 = point_k - point_i;
    auto mid2 = point_i + vec2 / 2.f;
    auto dir2 = Vector2f(-vec2.y(), vec2.x());
    auto det  = vcross(dir1, dir2);
    if (fabs(det) < 0.001f)
    {
        if (dot(vec1, vec2) >= 0.f)
        {
            const auto small_angle = 0.01f;
            const auto s           = sinf(small_angle);
            const auto l1          = norm(vec1);
            const auto l2          = norm(vec2);
            circle.center          = point_i;
            circle.axis1           = {0, 0};
            circle.axis2           = vec2 / s;
            circle.limits          = {-small_angle * l1 / l2, 0, small_angle};
            return;
        }
        else
            det = 0.001;
    }

    auto s      = vcross(mid2 - mid1, dir2) / det;
    auto center = mid1 + s * dir1;
    auto axis1  = point_i - center;
    auto axis2  = Vector2f(-axis1.y(), axis1.x());
    auto toPt2  = point_k - center;
    auto limit2 = atan2f(dot(axis2, toPt2), dot(axis1, toPt2));
    auto toPt1  = point_j - center;
    auto limit1 = atan2f(dot(axis2, toPt1), dot(axis1, toPt1));

    if (limit1 * limit2 > 0)
    {
        if (fabs(limit1) < fabs(limit2))
            limit2 += limit2 > 0 ? -2 * M_PI : 2 * M_PI;
        if (fabs(limit1) > fabs(limit2))
            limit1 += limit1 > 0 ? -2 * M_PI : 2 * M_PI;
    }

    circle.center = center;
    circle.axis1  = axis1;
    circle.axis2  = axis2;
    circle.limits = {limit1, 0, limit2};
    return;
}

void get_ellipse(Circle &circle, const Vector2f &point_j, const Vector2f &point_i, const Vector2f &point_k)
{
    constexpr int numIter = 16;
    auto          vec1    = point_j - point_i;
    auto          vec2    = point_k - point_i;

    auto len1 = norm(vec1);
    auto len2 = norm(vec2);
    auto cosa = dot(vec1, vec2) / (len1 * len2);
    auto maxA = acosf(cosa);
    auto ang  = maxA * 0.5f;
    auto incA = maxA * 0.25f;
    auto l1   = len1;
    auto l2   = len2;
    if (len1 < len2)
    {
        l1 = len2;
        l2 = len1;
    }

    float a, b, c, d;
    for (int iter = 0; iter < numIter; iter++)
    {
        float theta = ang * 0.5f;
        a           = l1 * sinf(theta);
        b           = l1 * cosf(theta);
        float beta  = maxA - theta;
        c           = l2 * sinf(beta);
        d           = l2 * cosf(beta);
        float v     = (1 - d / b) * (1 - d / b) + (c * c) / (a * a); // ellipse equation
        ang += (v > 1) ? incA : -incA;
        incA *= 0.5f;
    }

    Vector2f vec, pt2;
    float    len;
    if (len1 < len2)
    {
        vec = vec2;
        len = len2;
        pt2 = point_k;
    }
    else
    {
        vec = vec1;
        len = len1;
        pt2 = point_j;
    }
    auto     dir = vec / len;
    Vector2f perp(-dir.y(), dir.x());
    auto     cross = vcross(vec1, vec2);
    if ((len1 < len2 && cross > 0) || (len1 >= len2 && cross < 0))
        perp = {dir.y(), -dir.x()};
    auto v        = b * b / len;
    auto h        = b * a / len;
    auto axis1    = -v * dir - h * perp;
    auto center   = point_i - axis1;
    auto axis2    = pt2 - center;
    auto beta     = asinf(std::min(c / a, 1.f));
    circle.center = center;
    circle.axis1  = axis1;
    circle.axis2  = (len1 < len2) ? axis2 : Vector2f(-axis2.x(), -axis2.y());
    circle.limits =
        (len1 < len2) ? std::array<float, 3>{-beta, 0, M_PI * 0.5f} : std::array<float, 3>{-M_PI * 0.5f, 0, beta};
    return;
}

void get_hybrid(Circle &circle, const Vector2f &point_j, const Vector2f &point_i, const Vector2f &point_k)
{
    get_circle(circle, point_j, point_i, point_k);
    auto lim0 = circle.limits[0];
    auto lim2 = circle.limits[2];
    if (lim2 < lim0)
        std::swap(lim0, lim2);
    if (lim0 < -M_PI * 0.5f || lim2 > M_PI * 0.5f)
        get_ellipse(circle, point_j, point_i, point_k);
}

void get_yuksel(YukselType type, Circle &circle, const Vector2f &point_j, const Vector2f &point_i,
                const Vector2f &point_k)
{
    switch (type)
    {
    case YukselType::Circular: return get_circle(circle, point_j, point_i, point_k);
    case YukselType::Elliptical: return get_ellipse(circle, point_j, point_i, point_k);
    case YukselType::Hybrid: return get_hybrid(circle, point_j, point_i, point_k);
    }
}

inline Vector2f circle_pos(const Circle &circle, float t, int segment = 0)
{
    float tt = lerp(circle.limits[segment + 0], circle.limits[segment + 1], t);
    return circle.center + circle.axis1 * cosf(tt) + circle.axis2 * sinf(tt);
}

inline Vector2f blended_pos(const Circle &circle1, const Circle &circle2, float t)
{
    auto p1 = circle_pos(circle1, t, 1);
    auto p2 = circle_pos(circle2, t, 0);

    return square(cosf(M_PI_2 * t)) * p1 + square(sinf(M_PI_2 * t)) * p2;
}

} // namespace

void draw_Yuksel_ellipse(int p0x, int p0y, int p1x, int p1y, int p2x, int p2y, const PlotPixelFunc &plot)
{
    constexpr int                          num_segments = 16;
    constexpr int                          half_num     = num_segments / 2;
    std::array<Vector2f, num_segments + 1> points;
    for (int i = 0; i < half_num; ++i)
    {
        Circle c;
        get_ellipse(c, Vector2f(p0x, p0y), Vector2f(p1x, p1y), Vector2f(p2x, p2y));
        points[i] = circle_pos(c, i / float(half_num), 0);
    }

    for (int i = half_num; i <= num_segments; ++i)
    {
        Circle c;
        get_ellipse(c, Vector2f(p0x, p0y), Vector2f(p1x, p1y), Vector2f(p2x, p2y));
        points[i] = circle_pos(c, (i - half_num) / float(half_num), 1);
    }

    for (int i = 0; i < num_segments; ++i)
        draw_line(round(points[i].x()), round(points[i].y()), round(points[i + 1].x()), round(points[i + 1].y()), plot);
}

void draw_Yuksel_curve(int p0x, int p0y, int p1x, int p1y, int p2x, int p2y, int p3x, int p3y,
                       const PlotPixelFunc &plot, YukselType type, bool include_start, bool include_end)
{
    constexpr int                          max_segments = 16;
    std::array<Vector2f, max_segments + 1> start_curve;
    std::array<Vector2f, max_segments + 1> mid_curve;
    std::array<Vector2f, max_segments + 1> end_curve;

    // make the segments roughly 10 pixels long each
    int start_segments = std::clamp((int)round(norm(Vector2f(p1x - p0x, p1y - p0y)) / 10), 1, max_segments);
    int mid_segments   = std::clamp((int)round(norm(Vector2f(p2x - p1x, p2y - p1y)) / 10), 1, max_segments);
    int end_segments   = std::clamp((int)round(norm(Vector2f(p3x - p2x, p3y - p2y)) / 10), 1, max_segments);

    if (include_start)
        for (int i = 0; i <= start_segments; ++i)
        {
            Circle c1;
            Circle c2;
            get_yuksel(type, c1, Vector2f(p0x, p0y), Vector2f(p1x, p1y), Vector2f(p2x, p2y));
            get_yuksel(type, c2, Vector2f(p1x, p1y), Vector2f(p2x, p2y), Vector2f(p3x, p3y));

            start_curve[i] = circle_pos(c1, i / float(start_segments), 0);
        }
    if (include_end)
        for (int i = 0; i <= end_segments; ++i)
        {
            Circle c1;
            Circle c2;
            get_yuksel(type, c1, Vector2f(p0x, p0y), Vector2f(p1x, p1y), Vector2f(p2x, p2y));
            get_yuksel(type, c2, Vector2f(p1x, p1y), Vector2f(p2x, p2y), Vector2f(p3x, p3y));

            end_curve[i] = circle_pos(c2, i / float(end_segments), 1);
        }
    for (int i = 0; i <= mid_segments; ++i)
    {
        Circle c1;
        Circle c2;
        get_yuksel(type, c1, Vector2f(p0x, p0y), Vector2f(p1x, p1y), Vector2f(p2x, p2y));
        get_yuksel(type, c2, Vector2f(p1x, p1y), Vector2f(p2x, p2y), Vector2f(p3x, p3y));

        mid_curve[i] = blended_pos(c1, c2, i / float(mid_segments));
    }

    if (include_start)
        for (int i = 0; i < start_segments; ++i)
            draw_line(round(start_curve[i].x()), round(start_curve[i].y()), round(start_curve[i + 1].x()),
                      round(start_curve[i + 1].y()), plot);

    for (int i = 0; i < mid_segments; ++i)
        draw_line(round(mid_curve[i].x()), round(mid_curve[i].y()), round(mid_curve[i + 1].x()),
                  round(mid_curve[i + 1].y()), plot);

    if (include_end)
        for (int i = 0; i < end_segments; ++i)
            draw_line(round(end_curve[i].x()), round(end_curve[i].y()), round(end_curve[i + 1].x()),
                      round(end_curve[i + 1].y()), plot);
}

// Bresenham/midpoint line drawing algorithm
void draw_line(int x1, int y1, int x2, int y2, const PlotPixelFunc &plot)
{
    // Difference of x and y values
    int dx = x2 - x1;
    int dy = y2 - y1;

    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;

    // Absolute values of differences
    int ix = dx < 0 ? -dx : dx;
    int iy = dy < 0 ? -dy : dy;

    // Larger of the x and y differences
    int inc = ix > iy ? ix : iy;

    int x = 0, y = 0;

    for (int i = 0; i < inc; i++)
    {
        x += ix;
        y += iy;

        if (x > inc)
        {
            x -= inc;
            x1 += sx;
        }

        if (y > inc)
        {
            y -= inc;
            y1 += sy;
        }

        plot(x1, y1);
    }
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

// the following functions are adapted from http://members.chello.at/~easyfilter/bresenham.html
// author Zingl Alois

// draw a black (0) anti-aliased line on white (255) background
void draw_line(int x0, int y0, int x1, int y1, const PlotAAPixelFunc &plot)
{
    int   dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int   dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int   err = dx - dy, e2, x2; // error value e_xy
    float ed  = dx + dy == 0 ? 1 : sqrt(dx * dx + dy * dy);

    // pixel loop
    for (;;)
    {
        plot(x0, y0, abs(err - dx + dy) / ed);
        e2 = err;
        x2 = x0;

        // x step
        if (2 * e2 >= -dx)
        {
            if (x0 == x1)
                break;
            if (e2 + dy < ed)
                plot(x0, y0 + sy, (e2 + dy) / ed);
            err -= dy;
            x0 += sx;
        }

        // y step
        if (2 * e2 <= dy)
        {
            if (y0 == y1)
                break;
            if (dx - e2 < ed)
                plot(x2 + sx, y0, (dx - e2) / ed);
            err += dx;
            y0 += sy;
        }
    }
}

// plot an anti-aliased line of width wd pixel
void draw_line(int x0, int y0, int x1, int y1, float wd, const PlotAAPixelFunc &plot)
{
    int   dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int   dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int   err;
    float e2 = sqrt(dx * dx + dy * dy); // length

    if (wd <= 1 || e2 == 0)
        return draw_line(x0, y0, x1, y1, plot); // assert

    dx *= 255 / e2;
    dy *= 255 / e2;
    wd = 255 * (wd - 1); // scale values

    if (dx < dy)
    {                                    // steep line
        x1  = round((e2 + wd / 2) / dy); // start offset
        err = x1 * dy - wd / 2;          // shift error value to offset width
        for (x0 -= x1 * sx;; y0 += sy)
        {
            // aliasing pre-pixel
            plot(x1 = x0, y0, err / 255.0f);
            // pixel on the line
            for (e2 = dy - err - wd; e2 + dy < 255; e2 += dy) plot(x1 += sx, y0, 0.f);
            // aliasing post-pixel
            plot(x1 + sx, y0, e2 / 255.0f);
            if (y0 == y1)
                break;
            err += dx; // y-step
            if (err > 255)
            {
                err -= dy;
                x0 += sx;
            } // x-step
        }
    }
    else
    {                                    // flat line
        y1  = round((e2 + wd / 2) / dx); // start offset
        err = y1 * dx - wd / 2;          // shift error value to offset width
        for (y0 -= y1 * sy;; x0 += sx)
        {
            // aliasing pre-pixel
            plot(x0, y1 = y0, err / 255.0f);
            // pixel on the line
            for (e2 = dx - err - wd; e2 + dx < 255; e2 += dx) plot(x0, y1 += sy, 0.f);
            // aliasing post-pixel
            plot(x0, y1 + sy, e2 / 255.0f);
            if (x0 == x1)
                break;
            err += dy; // x-step
            if (err > 255)
            {
                err -= dx;
                y0 += sy;
            } // y-step
        }
    }
}

// plot a limited quadratic Bezier segment
void draw_quad_Bezier_seg(int x0, int y0, int x1, int y1, int x2, int y2, const PlotPixelFunc &plot)
{
    int    sx = x2 - x1, sy = y2 - y1;
    long   xx = x0 - x1, yy = y0 - y1, xy;       // relative values for checks
    double dx, dy, err, cur = xx * sy - yy * sx; // curvature

    assert(xx * sx <= 0 && yy * sy <= 0); // sign of gradient must not change

    // begin with longer part
    if (sx * (long)sx + sy * (long)sy > xx * xx + yy * yy)
    {
        x2  = x0;
        x0  = sx + x1;
        y2  = y0;
        y0  = sy + y1;
        cur = -cur; // swap P0 P2
    }
    // no straight line
    if (cur != 0)
    {
        xx += sx;
        xx *= sx = x0 < x2 ? 1 : -1; // x step direction
        yy += sy;
        yy *= sy = y0 < y2 ? 1 : -1; // y step direction
        xy       = 2 * xx * yy;
        xx *= xx;
        yy *= yy; // differences 2nd degree
        // negated curvature?
        if (cur * sx * sy < 0)
        {
            xx  = -xx;
            yy  = -yy;
            xy  = -xy;
            cur = -cur;
        }
        dx = 4.0 * sy * cur * (x1 - x0) + xx - xy; // differences 1st degree
        dy = 4.0 * sx * cur * (y0 - y1) + yy - xy;
        xx += xx;
        yy += yy;
        err = dx + dy + xy; // error 1st step
        do {
            plot(x0, y0); // plot curve
            if (x0 == x2 && y0 == y2)
                return;        // last pixel -> curve finished
            y1 = 2 * err < dx; // save value for test of y step
            if (2 * err > dy)
            {
                x0 += sx;
                dx -= xy;
                err += dy += yy;
            } // x step
            if (y1)
            {
                y0 += sy;
                dy -= xy;
                err += dx += xx;
            }                       // y step
        } while (dy < 0 && dx > 0); // gradient negates -> algorithm fails
    }
    draw_line(x0, y0, x2, y2, plot); // plot remaining part to end
}

// plot any quadratic Bezier curve
void draw_quad_Bezier(int x0, int y0, int x1, int y1, int x2, int y2, const PlotPixelFunc &plot)
{
    int    x = x0 - x1, y = y0 - y1;
    double t = x0 - 2 * x1 + x2, r;

    if ((long)x * (x2 - x1) > 0)
    {                                // horizontal cut at P4?
        if ((long)y * (y2 - y1) > 0) // vertical cut at P6 too?
            // which first?
            if (fabs((y0 - 2 * y1 + y2) / t * x) > abs(y))
            {
                x0 = x2;
                x2 = x + x1;
                y0 = y2;
                y2 = y + y1; // swap points
            }                // now horizontal cut at P4 comes first
        t = (x0 - x1) / t;
        r = (1 - t) * ((1 - t) * y0 + 2.0 * t * y1) + t * t * y2; // By(t=P4)
        t = (x0 * x2 - x1 * x1) * t / (x0 - x1);                  // gradient dP4/dx=0
        x = floor(t + 0.5);
        y = floor(r + 0.5);
        r = (y1 - y0) * (t - x0) / (x1 - x0) + y0; // intersect P3 | P0 P1
        draw_quad_Bezier_seg(x0, y0, x, floor(r + 0.5), x, y, plot);
        r  = (y1 - y2) * (t - x2) / (x1 - x2) + y2; // intersect P4 | P1 P2
        x0 = x1 = x;
        y0      = y;
        y1      = floor(r + 0.5); // P0 = P4, P1 = P8
    }

    // vertical cut at P6?
    if ((long)(y0 - y1) * (y2 - y1) > 0)
    {
        t = y0 - 2 * y1 + y2;
        t = (y0 - y1) / t;
        r = (1 - t) * ((1 - t) * x0 + 2.0 * t * x1) + t * t * x2; // Bx(t=P6)
        t = (y0 * y2 - y1 * y1) * t / (y0 - y1);                  // gradient dP6/dy=0
        x = floor(r + 0.5);
        y = floor(t + 0.5);
        r = (x1 - x0) * (t - y0) / (y1 - y0) + x0; // intersect P6 | P0 P1
        draw_quad_Bezier_seg(x0, y0, floor(r + 0.5), y, x, y, plot);
        r  = (x1 - x2) * (t - y2) / (y1 - y2) + x2; // intersect P7 | P1 P2
        x0 = x;
        x1 = floor(r + 0.5);
        y0 = y1 = y; // P0 = P6, P1 = P7
    }
    draw_quad_Bezier_seg(x0, y0, x1, y1, x2, y2, plot); // remaining part
}
