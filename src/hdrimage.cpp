//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimage.h"
#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "envmap.h"
#include "timer.h"
#include <algorithm>  // for nth_element, transform
#include <cmath>      // for floor, pow, exp, ceil, round, sqrt
#include <ctype.h>    // for tolower
#include <exception>  // for exception
#include <functional> // for pointer_to_unary_function, function
#include <limits>

#include <spdlog/spdlog.h>

#include <spdlog/fmt/ostr.h>

#include <stdexcept> // for runtime_error, out_of_range
#include <stdlib.h>  // for abs
#include <string>    // for allocator, operator==, basic_string
#include <vector>    // for vector

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h" // for stbir_resize_float

#include "parallelfor.h"

using namespace std;

// local functions
namespace
{

const Color4 g_blackPixel(0, 0, 0, 0);

// create a vector containing the normalized values of a 1D Gaussian filter
Array2Df       horizontal_gaussian_kernel(float sigma, float truncate);
int            wrap_coord(int p, int maxP, HDRImage::BorderMode m);
void           update_coeffs(vector<Color4> *coeffs, const Color4 &hdr, float d_omega, const nanogui::Vector3f &xyz);
vector<Color4> diffuse_convolve(const Array2D<Color4> &buffer);

inline void clip_roi(Box2i &roi, const HDRImage &img)
{
    if (roi.has_volume())
        roi.intersect(img.box());
    else
        roi = img.box();
}

} // namespace

HDRImage::HDRImage(const HDRImage &img, const Box2i &roi) : Base(roi.size().x(), roi.size().y())
{
    copy_paste(img, roi, 0, 0, true);
}

// makes a copy of the image, applies func for each pixel in roi of the copy, and returns the result
HDRImage HDRImage::apply_function(function<Color4(const Color4 &)> func, Box2i roi) const
{
    clip_roi(roi, *this);

    HDRImage result = (*this);

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &func, &roi, &result](int y)
                 {
                     for (int x = roi.min.x(); x < roi.max.x(); ++x) result(x, y) = func((*this)(x, y));
                 });

    return result;
}

// applies func for each pixel in roi, and stores result back in this
HDRImage &HDRImage::apply_function(function<Color4(const Color4 &)> func, Box2i roi)
{
    clip_roi(roi, *this);

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &func, &roi](int y)
                 {
                     for (int x = roi.min.x(); x < roi.max.x(); ++x) (*this)(x, y) = func((*this)(x, y));
                 });

    return *this;
}

// for images this and other, computes result = func(this, other) for each pixel in roi, and returns the result
// useful for something like c = a+b
HDRImage HDRImage::apply_function(const HDRImage &other, function<Color4(const Color4 &, const Color4 &)> func,
                                  Box2i roi) const
{
    clip_roi(roi, *this);

    HDRImage result = (*this);

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &func, &roi, &result, &other](int y)
                 {
                     for (int x = roi.min.x(); x < roi.max.x(); ++x) result(x, y) = func((*this)(x, y), other(x, y));
                 });

    return result;
}

// for images this and other, computes this = func(this, other) for each pixel in roi, and stores result back in this
// useful for something like a += b
HDRImage &HDRImage::apply_function(const HDRImage &other, function<Color4(const Color4 &, const Color4 &)> func,
                                   Box2i roi)
{
    if (roi.has_volume())
        roi.intersect(box());
    else
    {
        roi = box();
        roi.intersect(other.box());
    }

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &func, &roi, &other](int y)
                 {
                     for (int x = roi.min.x(); x < roi.max.x(); ++x) (*this)(x, y) = func((*this)(x, y), other(x, y));
                 });

    return *this;
}

Color4 HDRImage::reduce(function<Color4(const Color4 &, const Color4 &)> func, const Color4 &initial, Box2i roi) const
{
    clip_roi(roi, *this);

    Color4 result = initial;
    for (int y = roi.min.y(); y < roi.max.y(); ++y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); ++x)
            if (x != roi.min.x() && y != roi.min.y())
                result = func(result, (*this)(x, y));
    }

    return result;
}

HDRImage HDRImage::squared(Box2i roi) const
{
    HDRImage result(*this);
    return result.apply_function([](const Color4 &c) { return c * c; }, roi);
}

HDRImage &HDRImage::square(Box2i roi)
{
    return apply_function([](const Color4 &c) { return c * c; }, roi);
}

HDRImage &HDRImage::abs(Box2i roi)
{
    return apply_function([](const Color4 &c) { return ::abs(c); }, roi);
}

Color4 HDRImage::sum(Box2i roi) const
{
    return reduce([](const Color4 &a, const Color4 &b) { return a + b; }, Color4(0.f), roi);
}

Color4 HDRImage::mean(Box2i roi) const { return sum(roi) / (roi.has_volume() ? roi.volume() : size()); }

Color4 HDRImage::min(Box2i roi) const
{
    return reduce([](const Color4 &a, const Color4 &b) { return ::min(a, b); },
                  Color4(std::numeric_limits<float>::infinity()), roi);
}

Color4 HDRImage::max_neg(Box2i roi) const
{
    return reduce([](const Color4 &a, const Color4 &b) { return ::min(a, b); }, Color4(0.f), roi);
}

Color4 HDRImage::max(Box2i roi) const
{
    return reduce([](const Color4 &a, const Color4 &b) { return ::max(a, b); },
                  Color4(-std::numeric_limits<float>::infinity()), roi);
}

HDRImage HDRImage::log10ed(const Box2i &roi) const
{
    // Taking a linear image im, transform to log10 scale.
    return apply_function([](const Color4 &c) { return log10(c); }, roi);
}

HDRImage HDRImage::exp10ed(const Box2i &roi) const
{
    // take an image in log10 domain and transform it back to linear domain.
    return apply_function([](const Color4 &c) { return pow(10.f, c); }, roi);
}

void HDRImage::copy_paste(const HDRImage &src, Box2i roi, int dst_x, int dst_y, bool raw_copy)
{
    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(src.box());
    else
        roi = src.box();

    if (!roi.has_volume())
        return;

    // clip roi to valid region in this image starting at dst_x and dst_y
    auto old_min = roi.min;
    roi.move_min_to({dst_x, dst_y});
    roi.intersect(box());
    roi.move_min_to(old_min);

    // for every pixel in the image
    if (raw_copy)
        parallel_for(roi.min.y(), roi.max.y(),
                     [this, &src, &roi, dst_x, dst_y](int y)
                     {
                         for (int x = roi.min.x(); x < roi.max.x(); ++x)
                             (*this)(dst_x + x - roi.min.x(), dst_y + y - roi.min.y()) = src(x, y);
                     });
    else
        parallel_for(roi.min.y(), roi.max.y(),
                     [this, &src, &roi, dst_x, dst_y](int y)
                     {
                         for (int x = roi.min.x(); x < roi.max.x(); ++x)
                         {
                             Color4 &dst = (*this)(dst_x + x - roi.min.x(), dst_y + y - roi.min.y());
                             dst         = src(x, y) + dst * (1.f - src(x, y).a);
                         }
                     });
}

void HDRImage::seamless_copy_paste(AtomicProgress progress, const HDRImage &src, Box2i padded_src_roi, int dst_x,
                                   int dst_y, bool log_domain)
{
    constexpr int max_iters = 300;

    progress.set_num_steps(max_iters + 2);

    // ensure valid ROI
    if (padded_src_roi.has_volume())
        padded_src_roi.intersect(src.box());
    else
        padded_src_roi = src.box();

    if (!padded_src_roi.has_volume())
        return;

    spdlog::trace("padded_src_roi: {}", padded_src_roi);

    // clip roi to valid region in this image starting at dst_x and dst_y
    auto old_min = padded_src_roi.min;
    padded_src_roi.move_min_to({dst_x, dst_y});
    auto padded_dst_roi = padded_src_roi; // save this as the destination roi
    padded_src_roi.intersect(box());
    padded_src_roi.move_min_to(old_min);

    auto padded_fg = HDRImage(src, padded_src_roi);
    auto padded_bg = HDRImage(*this, padded_dst_roi);

    // src.save(fmt::format("cg-{:03d}-src.exr", 0), 1.f, 2.2f, true, true);
    // padded_fg.save(fmt::format("cg-{:03d}-fg.exr", 0), 1.f, 2.2f, true, true);

    Color4 fg_offset, bg_offset;
    if (log_domain)
    {
        // first offset to remove zeros and negative values
        fg_offset = padded_fg.max_neg() - 1e-6f;
        padded_fg -= fg_offset;
        bg_offset = padded_bg.max_neg() - 1e-6f;
        padded_bg -= bg_offset;

        // transform to the log domain
        padded_fg = padded_fg.log10ed();
        padded_bg = padded_bg.log10ed();
    }
    else
    {
        padded_fg.apply_function([](const Color4 &c) { return convert_colorspace(c, CIELab_CS, LinearSRGB_CS); });
        padded_bg.apply_function([](const Color4 &c) { return convert_colorspace(c, CIELab_CS, LinearSRGB_CS); });
    }

    // float avg_color = (0.5f * padded_fg.mean() + 0.5f * padded_bg.mean()).average();

    auto padded_fg_roi = padded_fg.box();
    auto working_roi   = padded_fg_roi.expanded(-1);

    // zero out the mask at the boundary
    {
        // top & bottom
        parallel_for(0, padded_fg.width(),
                     [&padded_fg](int i) { padded_fg(i, 0).a = padded_fg(i, padded_fg.height() - 1).a = 0.f; });

        // left & right
        parallel_for(0, padded_fg.height(),
                     [&padded_fg](int i) { padded_fg(0, i).a = padded_fg(padded_fg.width() - 1, i).a = 0.f; });
    }

    //
    // Use conjugate gradients to solve the Poisson system
    //

    auto multiply_by_mask = [](HDRImage &img, const HDRImage &mask)
    {
        img.apply_function(mask,
                           [](const Color4 &c, const Color4 &m)
                           {
                               auto ret = c * (m.a <= 1e-5f ? 0.f : 1.f);
                               ret.a    = c.a;
                               return ret;
                           });
    };

    auto copy_outside_mask = [](HDRImage &to, const HDRImage &from, const HDRImage &mask)
    {
        parallel_for(0, to.size(),
                     [&to, &from, &mask](int i) { to(i) = to(i) * mask(i).a + from(i) * (1.f - mask(i).a); });
    };

    auto copy_outside_hard_mask = [](HDRImage &to, const HDRImage &from, const HDRImage &mask)
    {
        parallel_for(0, to.size(),
                     [&to, &from, &mask](int i)
                     {
                         float a = mask(i).a <= 1e-5f ? 0.f : 1.f;
                         to(i)   = to(i) * a + from(i) * (1.f - a);
                     });
    };

    HDRImage ti = padded_bg;
    ti.apply_function([&](const Color4 &c) { return Color4(0.f, 0.f, 0.f, 1.f); }, working_roi);
    // ti.copy_paste(padded_fg, working_roi, 1, 1);
    copy_outside_hard_mask(ti, padded_bg, padded_fg);
    ti.set_alpha(1.f);

    auto b = padded_fg.laplacian_filtered(AtomicProgress(progress, 1.f / (max_iters + 2)), HDRImage::EDGE,
                                          HDRImage::EDGE, working_roi);
    b.set_alpha(1.f);

    auto Ati = ti.laplacian_filtered(AtomicProgress(progress, 1.f / (max_iters + 2)), HDRImage::EDGE, HDRImage::EDGE,
                                     working_roi);
    // Ati.set_alpha(1.f);

    auto di = b - Ati;
    di.set_alpha(1.f);
    // zero_boundary(di);
    multiply_by_mask(di, padded_fg);

    auto ri = di;

    HDRImage ATdi(padded_fg);

    // padded_bg.save(fmt::format("cg-{:03d}-bg.exr", 0), 1.f, 2.2f, true, true);
    // ti.save(fmt::format("cg-{:03d}-ti.exr", 0), 1.f, 2.2f, true, true);
    // b.save(fmt::format("cg-{:03d}-b.exr", 0), 1.f, 2.2f, true, true);

    for (int iter = 1; iter < max_iters; ++iter)
    {
        auto riTri = ri.squared(working_roi).sum(working_roi);

        ATdi = di.laplacian_filtered(AtomicProgress(progress, 1.f / (max_iters + 2)), HDRImage::EDGE, HDRImage::EDGE,
                                     working_roi);
        ATdi.set_alpha(1.f);

        auto diTATdi = (di * ATdi).sum(working_roi);

        auto alpha = riTri / diTATdi;

        ti += alpha * di;
        copy_outside_hard_mask(ti, padded_bg, padded_fg);
        // copy_boundary(ti, padded_bg);
        ti.set_alpha(1.f);

        // ti.save(fmt::format("cg-ti-{:03d}.exr", iter), 1.f, 2.2f, true, true);
        // ri.save(fmt::format("cg-ri-{:03d}.exr", iter), 1.f, 2.2f, true, true);
        // di.save(fmt::format("cg-di-{:03d}.exr", iter), 1.f, 2.2f, true, true);
        // ATdi.save(fmt::format("cg-Ati-{:03d}.exr", iter), 1.f, 2.2f, true, true);

        // if we've done at least 50 iterations and the squared difference is less than .1% of the average intensity
        // if (iter > 50 && fabs(alpha).average() * di.mean().average() < avg_color * 0.1f)
        //     break;

        auto r2 = ri - alpha * ATdi;

        auto beta = r2.squared(working_roi).sum(working_roi) / riTri;

        di = r2 + beta * di;
        ri = r2;

        ri.set_alpha(1.f);
        // zero_boundary(ri);
        multiply_by_mask(ri, padded_fg);

        di.set_alpha(1.f);
        // zero_boundary(di);
        multiply_by_mask(di, padded_fg);
    }

    ti.set_alpha(1.f);
    copy_outside_mask(ti, padded_bg, padded_fg);

    if (log_domain)
        ti = ti.exp10ed() + fg_offset;
    else
        ti.apply_function([](const Color4 &c) { return convert_colorspace(c, LinearSRGB_CS, CIELab_CS); });

    copy_paste(ti, working_roi, dst_x + 1, dst_y + 1);

    apply_function([&](const Color4 &c) { return Color4(c.r, c.g, c.b, 1.f); }, padded_dst_roi);
}

const vector<string> &HDRImage::border_mode_names()
{
    static const vector<string> names = {"Black", "Edge", "Repeat", "Mirror"};
    return names;
}

const vector<string> &HDRImage::sampler_names()
{
    static const vector<string> names = {"Nearest neighbor", "Bilinear", "Bicubic"};
    return names;
}

const Color4 &HDRImage::pixel(int x, int y, BorderMode mx, BorderMode my) const
{
    x = wrap_coord(x, width(), mx);
    y = wrap_coord(y, height(), my);
    if (x < 0 || y < 0)
        return g_blackPixel;

    return (*this)(x, y);
}

Color4 &HDRImage::pixel(int x, int y, BorderMode mx, BorderMode my)
{
    x = wrap_coord(x, width(), mx);
    y = wrap_coord(y, height(), my);
    if (x < 0 || y < 0)
        throw out_of_range("Cannot assign to out-of-bounds pixel when BorderMode==BLACK.");

    return (*this)(x, y);
}

Color4 HDRImage::sample(float sx, float sy, Sampler s, BorderMode mx, BorderMode my) const
{
    switch (s)
    {
    case NEAREST: return nearest(sx, sy, mx, my);
    case BILINEAR: return bilinear(sx, sy, mx, my);
    case BICUBIC:
    default: return bicubic(sx, sy, mx, my);
    }
}

Color4 HDRImage::nearest(float sx, float sy, BorderMode mx, BorderMode my) const
{
    return pixel(std::floor(sx), std::floor(sy), mx, my);
}

Color4 HDRImage::bilinear(float sx, float sy, BorderMode mx, BorderMode my) const
{
    // shift so that pixels are defined at their centers
    sx -= 0.5f;
    sy -= 0.5f;

    int x0 = (int)std::floor(sx);
    int y0 = (int)std::floor(sy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    sx -= x0;
    sy -= y0;

    return lerp(lerp(pixel(x0, y0, mx, my), pixel(x1, y0, mx, my), sx),
                lerp(pixel(x0, y1, mx, my), pixel(x1, y1, mx, my), sx), sy);
}

// photoshop bicubic
Color4 HDRImage::bicubic(float sx, float sy, BorderMode mx, BorderMode my) const
{
    // shift so that pixels are defined at their centers
    sx -= 0.5f;
    sy -= 0.5f;

    int bx = (int)std::floor(sx);
    int by = (int)std::floor(sy);

    float  A           = -0.75f;
    float  totalweight = 0;
    Color4 val(0, 0, 0, 0);

    for (int y = by - 1; y < by + 3; y++)
    {
        float disty   = fabs(sy - y);
        float yweight = (disty <= 1) ? ((A + 2.0f) * disty - (A + 3.0f)) * disty * disty + 1.0f
                                     : ((A * disty - 5.0f * A) * disty + 8.0f * A) * disty - 4.0f * A;

        for (int x = bx - 1; x < bx + 3; x++)
        {
            float distx  = fabs(sx - x);
            float weight = (distx <= 1) ? (((A + 2.0f) * distx - (A + 3.0f)) * distx * distx + 1.0f) * yweight
                                        : (((A * distx - 5.0f * A) * distx + 8.0f * A) * distx - 4.0f * A) * yweight;

            val += pixel(x, y, mx, my) * weight;
            totalweight += weight;
        }
    }
    val *= 1.0f / totalweight;
    return val;
}

HDRImage HDRImage::resampled(int w, int h, AtomicProgress progress,
                             function<nanogui::Vector2f(const nanogui::Vector2f &)> warp_cb, int super_sample,
                             Sampler sampler, BorderMode mx, BorderMode my) const
{
    HDRImage result(w, h);

    Timer timer;
    progress.set_num_steps(result.height());
    // for every pixel in the image
    parallel_for(0, result.height(),
                 [this, w, h, &progress, &warp_cb, &result, super_sample, sampler, mx, my](int y)
                 {
                     for (int x = 0; x < result.width(); ++x)
                     {
                         Color4 sum(0, 0, 0, 0);
                         for (int yy = 0; yy < super_sample; ++yy)
                         {
                             float j = (yy + 0.5f) / super_sample;
                             for (int xx = 0; xx < super_sample; ++xx)
                             {
                                 float             i     = (xx + 0.5f) / super_sample;
                                 nanogui::Vector2f srcUV = warp_cb(nanogui::Vector2f((x + i) / w, (y + j) / h)) *
                                                           nanogui::Vector2f(width(), height());
                                 sum += sample(srcUV[0], srcUV[1], sampler, mx, my);
                             }
                         }
                         result(x, y) = sum / (super_sample * super_sample);
                     }
                     ++progress;
                 });
    spdlog::trace("Resampling took: {} seconds.", (timer.elapsed() / 1000.f));
    return result;
}

void HDRImage::swap_rows(int a, int b)
{
    for (int x = 0; x < width(); x++) std::swap(pixel(x, a), pixel(x, b));
}

void HDRImage::swap_columns(int a, int b)
{
    for (int y = 0; y < height(); y++) std::swap(pixel(a, y), pixel(b, y));
}

HDRImage HDRImage::flipped_vertical() const
{
    HDRImage flipped = *this;
    int      top     = height() - 1;
    int      bottom  = 0;
    while (top > bottom)
    {
        flipped.swap_rows(top, bottom);
        top--;
        bottom++;
    }
    return flipped;
}

HDRImage HDRImage::flipped_horizontal() const
{
    HDRImage flipped = *this;
    int      right   = width() - 1;
    int      left    = 0;
    while (right > left)
    {
        flipped.swap_columns(right, left);
        right--;
        left++;
    }
    return flipped;
}

HDRImage HDRImage::rotated_90_cw() const
{
    HDRImage rotated(height(), width());
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++) rotated(y, x) = (*this)(x, y);

    return rotated.flipped_horizontal();
}

HDRImage HDRImage::rotated_90_ccw() const
{
    HDRImage rotated(height(), width());
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++) rotated(y, x) = (*this)(x, y);

    return rotated.flipped_vertical();
}

HDRImage HDRImage::convolved(const Array2Df &kernel, AtomicProgress progress, BorderMode mx, BorderMode my,
                             Box2i roi) const
{
    HDRImage result = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return result;

    int centerX = int((kernel.width() - 1.0) / 2.0);
    int centerY = int((kernel.height() - 1.0) / 2.0);

    Timer timer;
    progress.set_num_steps(roi.size().x());
    // for every pixel in the image
    parallel_for(roi.min.x(), roi.max.x(),
                 [this, &roi, &progress, &kernel, mx, my, &result, centerX, centerY](int x)
                 {
                     for (int y = roi.min.y(); y < roi.max.y(); y++)
                     {
                         Color4 accum(0.0f, 0.0f, 0.0f, 0.0f);
                         float  weightSum = 0.0f;
                         // for every pixel in the kernel
                         for (int xFilter = 0; xFilter < kernel.width(); xFilter++)
                         {
                             int xx = x - xFilter + centerX;

                             for (int yFilter = 0; yFilter < kernel.height(); yFilter++)
                             {
                                 int yy = y - yFilter + centerY;
                                 accum += kernel(xFilter, yFilter) * pixel(xx, yy, mx, my);
                                 weightSum += kernel(xFilter, yFilter);
                             }
                         }

                         // assign the pixel the value from convolution
                         result(x, y) = accum / weightSum;
                     }
                     ++progress;
                 });
    spdlog::trace("Convolution took: {} seconds.", (timer.elapsed() / 1000.f));

    return result;
}

HDRImage HDRImage::laplacian_filtered(AtomicProgress progress, BorderMode mx, BorderMode my, Box2i roi) const
{
    HDRImage result = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return result;

    Timer timer;
    progress.set_num_steps(roi.size().x());
    // for every pixel in the image
    parallel_for(roi.min.x(), roi.max.x(),
                 [this, &roi, &progress, mx, my, &result](int x)
                 {
                     for (int y = roi.min.y(); y < roi.max.y(); y++)
                     {
                         result(x, y) = pixel(x - 1, y, mx, my) + pixel(x + 1, y, mx, my) + pixel(x, y - 1, mx, my) +
                                        pixel(x, y + 1, mx, my) + pixel(x - 1, y - 1, mx, my) +
                                        pixel(x + 1, y + 1, mx, my) + pixel(x + 1, y - 1, mx, my) +
                                        pixel(x - 1, y + 1, mx, my) - 8 * pixel(x, y, mx, my);
                         result(x, y).a = pixel(x, y, mx, my).a;
                     }
                     ++progress;
                 });
    spdlog::trace("Laplacian filter took: {} seconds.", (timer.elapsed() / 1000.f));

    return result;
}

HDRImage HDRImage::gaussian_blurred_x(float sigma_x, AtomicProgress progress, BorderMode mx, float truncate_x,
                                      Box2i roi) const
{
    return convolved(horizontal_gaussian_kernel(sigma_x, truncate_x), progress, mx, mx, roi);
}

HDRImage HDRImage::gaussian_blurred_y(float sigma_y, AtomicProgress progress, BorderMode my, float truncate_y,
                                      Box2i roi) const
{
    return convolved(horizontal_gaussian_kernel(sigma_y, truncate_y).swapped_dims(), progress, my, my, roi);
}

// Use principles of separability to blur an image using 2 1D Gaussian Filters
HDRImage HDRImage::gaussian_blurred(float sigma_x, float sigma_y, AtomicProgress progress, BorderMode mx, BorderMode my,
                                    float truncate_x, float truncate_y, Box2i roi) const
{
    // blur using 2, 1D filters in the x and y directions
    return gaussian_blurred_x(sigma_x, AtomicProgress(progress, .5f), mx, truncate_x, roi)
        .gaussian_blurred_y(sigma_y, AtomicProgress(progress, .5f), my, truncate_y, roi);
}

// sharpen an image
HDRImage HDRImage::unsharp_masked(float sigma, float strength, AtomicProgress progress, BorderMode mx, BorderMode my,
                                  Box2i roi) const
{
    Timer timer;
    spdlog::trace("Starting unsharp mask...");
    HDRImage result = fast_gaussian_blurred(sigma, sigma, progress, mx, my, roi);
    result *= -1.f;
    result += *this;
    result *= strength;
    result += *this;
    // HDRImage result = *this + strength * (*this - fast_gaussian_blurred(sigma, sigma, progress, mx, my, roi));
    spdlog::trace("Unsharp mask took: {} seconds.", (timer.elapsed() / 1000.f));
    return result;
}

HDRImage HDRImage::median_filtered(float radius, int channel, AtomicProgress progress, BorderMode mx, BorderMode my,
                                   bool round, Box2i roi) const
{
    int      radiusi    = int(std::ceil(radius));
    HDRImage tempBuffer = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return tempBuffer;

    Timer timer;
    progress.set_num_steps(roi.size().y());
    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &roi, &tempBuffer, &progress, radius, radiusi, channel, mx, my, round](int y)
                 {
                     vector<float> mBuffer;
                     mBuffer.reserve((2 * (radiusi + 1)) * (2 * (radiusi + 1)));
                     for (int x = roi.min.x(); x < roi.max.x(); x++)
                     {
                         mBuffer.clear();

                         int x_coord, y_coord;
                         // over all pixels in the neighborhood kernel
                         for (int i = -radiusi; i <= radiusi; i++)
                         {
                             x_coord = x + i;
                             for (int j = -radiusi; j <= radiusi; j++)
                             {
                                 if (round && i * i + j * j > radius * radius)
                                     continue;

                                 y_coord = y + j;
                                 mBuffer.push_back(pixel(x_coord, y_coord, mx, my)[channel]);
                             }
                         }

                         int num = mBuffer.size();
                         int med = (num - 1) / 2;

                         nth_element(mBuffer.begin() + 0, mBuffer.begin() + med, mBuffer.begin() + mBuffer.size());
                         tempBuffer(x, y)[channel] = mBuffer[med];
                     }
                     ++progress;
                 });
    spdlog::trace("Median filter took: {} seconds.", (timer.elapsed() / 1000.f));

    return tempBuffer;
}

HDRImage HDRImage::bilateral_filtered(float sigma_range, float sigma_domain, AtomicProgress progress, BorderMode mx,
                                      BorderMode my, float truncate_domain, Box2i roi) const
{
    HDRImage filtered = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return filtered;

    // calculate the filter size
    int radius = int(std::ceil(truncate_domain * sigma_domain));

    Timer timer;
    progress.set_num_steps(roi.size().y());
    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &roi, &filtered, &progress, radius, sigma_range, sigma_domain, mx, my](int y)
                 {
                     for (int x = roi.min.x(); x < roi.max.x(); x++)
                     {
                         // initilize normalizer and sum value to 0 for every pixel location
                         float  weightSum = 0.0f;
                         Color4 accum(0.0f, 0.0f, 0.0f, 0.0f);

                         for (int yFilter = -radius; yFilter <= radius; yFilter++)
                         {
                             int yy = y + yFilter;
                             for (int xFilter = -radius; xFilter <= radius; xFilter++)
                             {
                                 int xx = x + xFilter;
                                 // calculate the squared distance between the 2 pixels (in range)
                                 float rangeExp  = ::pow(pixel(xx, yy, mx, my) - (*this)(x, y), 2).sum();
                                 float domainExp = std::pow(xFilter, 2) + std::pow(yFilter, 2);

                                 // calculate the exponentiated weighting factor from the domain and range
                                 float factorDomain = std::exp(-domainExp / (2.0 * std::pow(sigma_domain, 2)));
                                 float factorRange  = std::exp(-rangeExp / (2.0 * std::pow(sigma_range, 2)));
                                 weightSum += factorDomain * factorRange;
                                 accum += factorDomain * factorRange * pixel(xx, yy, mx, my);
                             }
                         }

                         // set pixel in filtered image to weighted sum of values in the filter region
                         filtered(x, y) = accum / weightSum;
                     }
                     ++progress;
                 });
    spdlog::trace("Bilateral filter took: {} seconds.", (timer.elapsed() / 1000.f));

    return filtered;
}

static int nextOddInt(int i) { return (i % 2 == 0) ? i + 1 : i; }

HDRImage HDRImage::iterated_box_blurred(float sigma, int iterations, AtomicProgress progress, BorderMode mx,
                                        BorderMode my, Box2i roi) const
{
    // Compute box blur size for desired sigma and number of iterations:
    // The kernel resulting from repeated box blurs of the same width is the
    // Irwin–Hall distribution
    // (https://en.wikipedia.org/wiki/Irwin–Hall_distribution)
    //
    // The variance of the Irwin-Hall distribution with n unit-sized boxes:
    //
    //      V(1, n) = n/12.
    //
    // Since V[w * X] = w^2 V[X] where w is a constant, we know that the
    // variance will scale as follows using width-w boxes:
    //
    //      V(w, n) = w^2*n/12.
    //
    // To achieve a certain standard deviation sigma, we want to find solve:
    //
    //      sqrt(V(w, n)) = w*sqrt(n/12) = sigma
    //
    // for w, given n and sigma; which is:
    //
    //      w = sqrt(12/n)*sigma
    //

    int w = nextOddInt(std::round(std::sqrt(12.f / iterations) * sigma));

    // Now, if width is odd, then we can use a centered box and are good to go.
    // If width is even, then we can't use centered boxes, but must instead
    // use a symmetric pairs of off-centered boxes. For now, just always round
    // up to next odd width
    int hw = (w - 1) / 2;

    HDRImage result = *this;
    for (int i = 0; i < iterations; i++)
        result = result.box_blurred(hw, AtomicProgress(progress, 1.f / iterations), mx, my, roi);

    return result;
}

HDRImage HDRImage::fast_gaussian_blurred(float sigma_x, float sigma_y, AtomicProgress progress, BorderMode mx,
                                         BorderMode my, Box2i roi) const
{
    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return *this;

    Timer timer;
    // See comments in HDRImage::iterated_box_blurred for derivation of width
    int hw = std::round((std::sqrt(12.f / 6) * sigma_x - 1) / 2.f);
    int hh = std::round((std::sqrt(12.f / 6) * sigma_y - 1) / 2.f);

    HDRImage im;
    // do horizontal blurs
    if (hw < 3)
        // for small blurs, just use a separable Gaussian
        im = gaussian_blurred_x(sigma_x, AtomicProgress(progress, 0.5f), mx, 6.f, roi);
    else
        // for large blurs, approximate Gaussian with 6 box blurs
        im = box_blurred_x(hw, AtomicProgress(progress, .5f / 6.f), mx, roi.expanded(5 * hw))
                 .box_blurred_x(hw, AtomicProgress(progress, .5f / 6.f), mx, roi.expanded(4 * hw))
                 .box_blurred_x(hw, AtomicProgress(progress, .5f / 6.f), mx, roi.expanded(3 * hw))
                 .box_blurred_x(hw, AtomicProgress(progress, .5f / 6.f), mx, roi.expanded(2 * hw))
                 .box_blurred_x(hw, AtomicProgress(progress, .5f / 6.f), mx, roi.expanded(1 * hw))
                 .box_blurred_x(hw, AtomicProgress(progress, .5f / 6.f), mx, roi);

    // now do vertical blurs
    if (hh < 3)
        // for small blurs, just use a separable Gaussian
        im = im.gaussian_blurred_y(sigma_y, AtomicProgress(progress, 0.5f), my, 6.f, roi);
    else
        // for large blurs, approximate Gaussian with 6 box blurs
        im = im.box_blurred_y(hh, AtomicProgress(progress, .5f / 6.f), my, roi.expanded(5 * hh))
                 .box_blurred_y(hh, AtomicProgress(progress, .5f / 6.f), my, roi.expanded(4 * hh))
                 .box_blurred_y(hh, AtomicProgress(progress, .5f / 6.f), my, roi.expanded(3 * hh))
                 .box_blurred_y(hh, AtomicProgress(progress, .5f / 6.f), my, roi.expanded(2 * hh))
                 .box_blurred_y(hh, AtomicProgress(progress, .5f / 6.f), my, roi.expanded(1 * hh))
                 .box_blurred_y(hh, AtomicProgress(progress, .5f / 6.f), my, roi);

    // copy just the roi
    HDRImage im2 = *this;
    im2.copy_paste(im, roi, roi.min.x(), roi.min.y(), true);

    spdlog::trace("fast_gaussian_blurred filter took: {} seconds.", (timer.elapsed() / 1000.f));
    return im2;
}

HDRImage HDRImage::box_blurred_x(int l_size, int r_size, AtomicProgress progress, BorderMode mx, Box2i roi) const
{
    HDRImage filtered = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return filtered;

    Timer timer;
    progress.set_num_steps(roi.size().y());
    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &roi, &filtered, &progress, l_size, r_size, mx](int y)
                 {
                     // fill up the accumulator
                     int x          = roi.min.x();
                     filtered(x, y) = 0;
                     for (int dx = -l_size; dx <= r_size; ++dx) filtered(x, y) += pixel(x + dx, y, mx, mx);

                     // blur all other pixels
                     for (x = roi.min.x() + 1; x < roi.max.x(); ++x)
                         filtered(x, y) =
                             filtered(x - 1, y) - pixel(x - 1 - l_size, y, mx, mx) + pixel(x + r_size, y, mx, mx);
                     // normalize
                     for (x = roi.min.x(); x < roi.max.x(); ++x) filtered(x, y) *= 1.f / (l_size + r_size + 1);

                     ++progress;
                 });
    spdlog::trace("box_blurred_x filter took: {} seconds.", (timer.elapsed() / 1000.f));

    return filtered;
}

HDRImage HDRImage::box_blurred_y(int l_size, int r_size, AtomicProgress progress, BorderMode my, Box2i roi) const
{
    HDRImage filtered = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return filtered;

    Timer timer;
    progress.set_num_steps(roi.size().x());
    // for every pixel in the image
    parallel_for(roi.min.x(), roi.max.x(),
                 [this, &roi, &filtered, &progress, l_size, r_size, my](int x)
                 {
                     // fill up the accumulator
                     int y          = roi.min.y();
                     filtered(x, y) = 0;
                     for (int dy = -l_size; dy <= r_size; ++dy) filtered(x, y) += pixel(x, y + dy, my, my);

                     // blur all other pixels
                     for (y = roi.min.y() + 1; y < roi.max.y(); ++y)
                         filtered(x, y) =
                             filtered(x, y - 1) - pixel(x, y - 1 - l_size, my, my) + pixel(x, y + r_size, my, my);

                     // normalize
                     for (y = roi.min.y(); y < roi.max.y(); ++y) filtered(x, y) *= 1.f / (l_size + r_size + 1);

                     ++progress;
                 });
    spdlog::trace("box_blurred_y filter took: {} seconds.", (timer.elapsed() / 1000.f));

    return filtered;
}

HDRImage HDRImage::resized_canvas(int newW, int newH, CanvasAnchor anchor, const Color4 &bgColor) const
{
    int oldW = width();
    int oldH = height();

    // fill in new regions with border value
    HDRImage img = HDRImage(newW, newH, bgColor);

    nanogui::Vector2i tl_dst(0, 0);
    // find top-left corner
    switch (anchor)
    {
    case HDRImage::TOP_RIGHT:
    case HDRImage::MIDDLE_RIGHT:
    case HDRImage::BOTTOM_RIGHT: tl_dst.x() = newW - oldW; break;

    case HDRImage::TOP_CENTER:
    case HDRImage::MIDDLE_CENTER:
    case HDRImage::BOTTOM_CENTER: tl_dst.x() = (newW - oldW) / 2; break;

    case HDRImage::TOP_LEFT:
    case HDRImage::MIDDLE_LEFT:
    case HDRImage::BOTTOM_LEFT:
    default: tl_dst.x() = 0; break;
    }
    switch (anchor)
    {
    case HDRImage::BOTTOM_LEFT:
    case HDRImage::BOTTOM_CENTER:
    case HDRImage::BOTTOM_RIGHT: tl_dst.y() = newH - oldH; break;

    case HDRImage::MIDDLE_LEFT:
    case HDRImage::MIDDLE_CENTER:
    case HDRImage::MIDDLE_RIGHT: tl_dst.y() = (newH - oldH) / 2; break;

    case HDRImage::TOP_LEFT:
    case HDRImage::TOP_CENTER:
    case HDRImage::TOP_RIGHT:
    default: tl_dst.y() = 0; break;
    }

    nanogui::Vector2i tl_src(0, 0);
    if (tl_dst.x() < 0)
    {
        tl_src.x() = -tl_dst.x();
        tl_dst.x() = 0;
    }
    if (tl_dst.y() < 0)
    {
        tl_src.y() = -tl_dst.y();
        tl_dst.y() = 0;
    }

    nanogui::Vector2i bs(std::min(oldW, newW), std::min(oldH, newH));

    img.copy_paste(*this, Box2i(tl_src, tl_src + bs), tl_dst.x(), tl_dst.y(), true);
    return img;
}

HDRImage HDRImage::resized(int w, int h) const
{
    HDRImage newImage(w, h);

    if (!stbir_resize_float((const float *)data(), width(), height(), 0, (float *)newImage.data(), w, h, 0, 4))
        throw runtime_error("Failed to resize image.");

    return newImage;
}

/*!
 * @brief           Apply a global brightness+contrast adjustment to the RGB pixel values.
 *
 * @param b         The brightness in the range [-1,1] where 0 means no change.
 *                  Changes the brightness by shifting the midpoint by b/2.
 * @param c         The contrast in the range [-1,1] where 0 means no change.
 *                  c sets the slope of the mapping at the midpoint where
 *                   -1 -> all gray/no contrast; horizontal line;
 *                    0 -> no change; 45 degree diagonal line;
 *                    1 -> no gray/black & white; vertical line.
 *                  If linear is False, this changes the contrast by shifting the 0.25 value by c/4.
 * @param linear    Whether to use linear or non-linear remapping.
 *                  The non-linear mapping keeps values within [0,1], while
 *                  the linear mapping may produce negative values and values > 1.
 * @param channel   Apply the adjustment to the specified channel(s).
 *                  Valid values are RGB, LUMINANCE or CIE_L, and CIE_CHROMATICITY.
 *                  All other values result in a no-op.
 * @return          The adjusted image.
 */
HDRImage HDRImage::brightness_contrast(float b, float c, bool linear, EChannel channel, Box2i roi) const
{
    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return *this;

    float slope = float(std::tan(lerp(0.0, M_PI_2, c / 2.0 + 0.5)));

    if (linear)
    {
        float midpoint = (1.f - b) / 2.f;

        if (channel == RGB)
            return apply_function(
                [slope, midpoint](const Color4 &c)
                {
                    return Color4(brightness_contrast_linear(c.r, slope, midpoint),
                                  brightness_contrast_linear(c.g, slope, midpoint),
                                  brightness_contrast_linear(c.b, slope, midpoint), c.a);
                },
                roi);
        else if (channel == LUMINANCE || channel == CIE_L)
            return apply_function(
                [slope, midpoint](const Color4 &c)
                {
                    Color4 lab = convert_colorspace(c, CIELab_CS, LinearSRGB_CS);
                    return convert_colorspace(
                        Color4(brightness_contrast_linear(lab.r, slope, midpoint), lab.g, lab.b, c.a), LinearSRGB_CS,
                        CIELab_CS);
                },
                roi);
        else if (channel == CIE_CHROMATICITY)
            return apply_function(
                [slope, midpoint](const Color4 &c)
                {
                    Color4 lab = convert_colorspace(c, CIELab_CS, LinearSRGB_CS);
                    return convert_colorspace(Color4(lab.r, brightness_contrast_linear(lab.g, slope, midpoint),
                                                     brightness_contrast_linear(lab.b, slope, midpoint), c.a),
                                              LinearSRGB_CS, CIELab_CS);
                },
                roi);
        else
            return *this;
    }
    else
    {
        float aB = (b + 1.f) / 2.f;

        if (channel == RGB)
            return apply_function(
                [aB, slope](const Color4 &c)
                {
                    return Color4(brightness_contrast_nonlinear(c.r, slope, aB),
                                  brightness_contrast_nonlinear(c.g, slope, aB),
                                  brightness_contrast_nonlinear(c.b, slope, aB), c.a);
                },
                roi);
        else if (channel == LUMINANCE || channel == CIE_L)
            return apply_function(
                [aB, slope](const Color4 &c)
                {
                    Color4 lab = convert_colorspace(c, CIELab_CS, LinearSRGB_CS);
                    return convert_colorspace(
                        Color4(brightness_contrast_nonlinear(lab.r, slope, aB), lab.g, lab.b, c.a), LinearSRGB_CS,
                        CIELab_CS);
                },
                roi);
        else if (channel == CIE_CHROMATICITY)
            return apply_function(
                [aB, slope](const Color4 &c)
                {
                    Color4 lab = convert_colorspace(c, CIELab_CS, LinearSRGB_CS);
                    return convert_colorspace(Color4(lab.r, brightness_contrast_nonlinear(lab.g, slope, aB),
                                                     brightness_contrast_nonlinear(lab.b, slope, aB), c.a),
                                              LinearSRGB_CS, CIELab_CS);
                },
                roi);
        else
            return *this;
    }
}

HDRImage HDRImage::inverted(Box2i roi) const
{
    return apply_function([](const Color4 &c) { return Color4(1.f - c.r, 1.f - c.g, 1.f - c.b, c.a); }, roi);
}

HDRImage HDRImage::bump_to_normal_map(float scale, AtomicProgress progress, BorderMode mx, BorderMode my,
                                      Box2i roi) const
{
    HDRImage normal_map = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return normal_map;

    auto dx = 1.f / width(), dy = 1.f / height();

    Timer timer;
    progress.set_num_steps(roi.size().y());
    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(),
                 [this, &roi, &normal_map, &progress, scale, dx, dy, mx, my](int j)
                 {
                     for (int i = roi.min.x(); i < roi.max.x(); ++i)
                     {
                         auto i1 = i + 1, j1 = j + 1;
                         auto p00 = pixel(i, j, mx, my), p10 = pixel(i1, j, mx, my), p01 = pixel(i, j1, mx, my);

                         auto              g00 = (p00.r + p00.g + p00.b) / 3;
                         auto              g01 = (p01.r + p01.g + p01.b) / 3;
                         auto              g10 = (p10.r + p10.g + p10.b) / 3;
                         nanogui::Vector3f normal{scale * (g10 - g00) / dx, scale * (g01 - g00) / dy, 1.0f};
                         normal           = normalize(normal) * 0.5f + 0.5f;
                         normal_map(i, j) = Color4{normal.x(), normal.y(), normal.z(), 1};
                     }

                     ++progress;
                 });
    spdlog::trace("bump_to_normal_map filter took: {} seconds.", (timer.elapsed() / 1000.f));

    return normal_map;
}

HDRImage HDRImage::irradiance_envmap(AtomicProgress progress) const
{
    static constexpr float c1 = 0.429043f, c2 = 0.511664f, c3 = 0.743125f, c4 = 0.886227f, c5 = 0.247708f;

    HDRImage convolved = *this;
    auto     coeffs    = diffuse_convolve(*this);

    int w = width();
    int h = height();

    progress.set_num_steps(h);
    parallel_for(0, h,
                 [&convolved, &progress, &w, &h, &coeffs](int j)
                 {
                     for (int i = 0; i < w; i++)
                     {
                         // We now find the cartesian components for pixel (i,j)
                         auto xyz = angularMapToXYZ(nanogui::Vector2f((i + 0.5f) / w, (j + 0.5f) / h));

                         for (int k = 0; k < 3; k++)
                         {
                             convolved(i, j)[k] =
                                 (c1 * coeffs[8][k] * (xyz.x() * xyz.x() - xyz.y() * xyz.y()) +
                                  c3 * coeffs[6][k] * xyz.z() * xyz.z() + c4 * coeffs[0][k] - c5 * coeffs[6][k] +
                                  2 * c1 *
                                      (coeffs[4][k] * xyz.x() * xyz.y() + coeffs[7][k] * xyz.x() * xyz.z() +
                                       coeffs[5][k] * xyz.y() * xyz.z()) +
                                  2 * c2 * (coeffs[3][k] * xyz.x() + coeffs[1][k] * xyz.y() + coeffs[2][k] * xyz.z())) /
                                 M_PI;
                         }
                     }

                     ++progress;
                 });
    return convolved;
}

// local functions
namespace
{

// create a vector containing the normalized values of a 1D Gaussian filter
Array2Df horizontal_gaussian_kernel(float sigma, float truncate)
{
    // calculate the size of the filter
    int offset     = int(std::ceil(truncate * sigma));
    int filterSize = 2 * offset + 1;

    Array2Df fData(filterSize, 1);

    // compute the un-normalized value of the Gaussian
    float normalizer = 0.0f;
    for (int i = 0; i < filterSize; i++)
    {
        fData(i, 0) = std::exp(-pow(i - offset, 2) / (2.0f * pow(sigma, 2)));
        normalizer += fData(i, 0);
    }

    // normalize
    for (int i = 0; i < filterSize; i++) fData(i, 0) /= normalizer;

    return fData;
}

int wrap_coord(int p, int maxP, HDRImage::BorderMode m)
{
    if (p >= 0 && p < maxP)
        return p;

    switch (m)
    {
    case HDRImage::EDGE: return ::clamp(p, 0, maxP - 1);
    case HDRImage::REPEAT: return mod(p, maxP);
    case HDRImage::MIRROR:
    {
        int frac = mod(p, maxP);
        return (::abs(p) / maxP % 2 != 0) ? maxP - 1 - frac : frac;
    }
    case HDRImage::BLACK:
    default: return -1;
    }
}

void update_coeffs(vector<Color4> *coeffs, const Color4 &hdr, float d_omega, const nanogui::Vector3f &xyz)
{
    //
    // Update the coefficients (i.e. compute the next term in the
    // integral) based on the lighting value hdr[3], the differential
    // solid angle d_omega and cartesian components of surface normal x,y,z
    //
    // Inputs:  hdr = L(x,y,z) [note that x^2+y^2+z^2 = 1]
    // i.e. the illumination at position (x,y,z)
    //
    // d_omega = The solid angle at the pixel corresponding to
    // (x,y,z).  For and angular map this is sinc(theta)
    //
    // x,y,z  = Cartesian components of surface normal
    //

    for (int col = 0; col < 3; col++)
    {
        // A different constant for each coefficient
        float c;

        // L_{00}.  Note that Y_{00} = 0.282095
        c = 0.282095f;
        (*coeffs)[0][col] += hdr[col] * c * d_omega;

        // L_{1m}. -1 <= m <= 1.  The linear terms
        c = 0.488603f;
        (*coeffs)[1][col] += hdr[col] * (c * xyz.y()) * d_omega;
        (*coeffs)[2][col] += hdr[col] * (c * xyz.z()) * d_omega;
        (*coeffs)[3][col] += hdr[col] * (c * xyz.x()) * d_omega;

        // The Quadratic terms, L_{2m} -2 <= m <= 2

        // First, L_{2-2}, L_{2-1}, L_{21} corresponding to xy,yz,xz
        c = 1.092548f;
        (*coeffs)[4][col] += hdr[col] * (c * xyz.x() * xyz.y()) * d_omega;
        (*coeffs)[5][col] += hdr[col] * (c * xyz.y() * xyz.z()) * d_omega;
        (*coeffs)[7][col] += hdr[col] * (c * xyz.x() * xyz.z()) * d_omega;

        // L_{20}.  Note that Y_{20} = 0.315392 (3z^2 - 1)
        c = 0.315392f;
        (*coeffs)[6][col] += hdr[col] * (c * (3 * xyz.z() * xyz.z() - 1)) * d_omega;

        // L_{22}.  Note that Y_{22} = 0.546274 (x^2 - y^2)
        c = 0.546274f;
        (*coeffs)[8][col] += hdr[col] * (c * (xyz.x() * xyz.x() - xyz.y() * xyz.y())) * d_omega;
    }
}

vector<Color4> diffuse_convolve(const Array2D<Color4> &buffer)
{
    //
    // The main integration routine. Calls update_coeffs to
    // actually increment the integral.
    // This assumes angular map format.
    //

    auto sinc = [](float x)
    {
        if (std::fabs(x) < 1.0e-4f)
            return 1.f;
        else
            return (std::sin(x) / x);
    };

    vector<Color4> coeffs(9, Color4{0.f});
    int            w       = buffer.width();
    int            h       = buffer.height();
    float          d_pixel = (2 * M_PI / w) * (2 * M_PI / h);
    for (int i = 0; i < w; i++)
    {
        for (int j = 0; j < h; j++)
        {
            // We now find the cartesian components for the point (i,j)
            auto  uv = nanogui::Vector2f((i + 0.5f) / w, (j + 0.5f) / h);
            auto  xy = 2 * uv - nanogui::Vector2f(1.f);
            float r  = norm(xy);

            // Consider only circle with r<1
            if (r <= 1.0f)
            {
                auto xyz = angularMapToXYZ(uv);

                //
                // Computation of the solid angle.  This follows from some
                // elementary calculus converting sin(theta) d theta d phi into
                // coordinates in terms of r.  This calculation should be redone
                // if the form of the input changes
                //
                float theta   = M_PI * r; // theta parameter of (i,j)
                float d_omega = d_pixel * sinc(theta);

                // Update Integration
                update_coeffs(&coeffs, buffer(i, j), d_omega, xyz);
            }
        }
    }
    return coeffs;
}

} // namespace