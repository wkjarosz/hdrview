//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include "color.h" // for Color4, max, min
#include "parallelfor.h"
#include "progress.h"
#include <ImathMatrix.h>
#include <functional> // for function
#include <nanogui/vector.h>
#include <string> // for string
#include <vector> // for vector

//! Floating point image
class HDRImage : public Array2D<Color4>
{
    using Base = Array2D<Color4>;

public:
    //-----------------------------------------------------------------------
    //@{ \name Constructors, destructors, etc.
    //-----------------------------------------------------------------------
    HDRImage(void) : Base() {}
    HDRImage(int w, int h, const Color4 &c = Color4(0.f)) : Base(w, h, c) {}
    HDRImage(const HDRImage &img, const Box2i &roi);
    //@}

    bool  contains(int x, int y) const { return x >= 0 && y >= 0 && x < (int)width() && y < (int)height(); }
    Box2i box() const { return Box2i(0, nanogui::Vector2i(width(), height())); }
    bool  is_null() const { return width() == 0 || height() == 0; }

    void set_alpha(float a)
    {
        *this = apply_function([a](const Color4 &c) { return Color4(c.r, c.g, c.b, a); });
    }

    void set_channel_from(int c, const HDRImage &other)
    {
        *this = apply_function(other,
                               [c](const Color4 &a, const Color4 &b)
                               {
                                   Color4 ret = a;
                                   ret[c]     = b[c];
                                   return ret;
                               });
    }

    //-----------------------------------------------------------------------
    //@{ \name Pixel accessors.
    //-----------------------------------------------------------------------
    enum BorderMode : int
    {
        BLACK = 0,
        EDGE,
        REPEAT,
        MIRROR
    };
    static const std::vector<std::string> &border_mode_names();
    Color4 &                               pixel(int x, int y, BorderMode mX = EDGE, BorderMode mY = EDGE);
    const Color4 &                         pixel(int x, int y, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    //@}

    // makes a copy of the image, applies func for each pixel in roi of the copy, and returns the result
    HDRImage apply_function(std::function<Color4(const Color4 &)> func, Box2i roi = Box2i()) const;
    // applies func for each pixel in roi, and stores result back in this
    HDRImage &apply_function(std::function<Color4(const Color4 &)> func, Box2i roi = Box2i());
    // for images this and other, computes result = func(this, other) for each pixel in roi, and returns the result
    // useful for something like c = a+b
    HDRImage apply_function(const HDRImage &other, std::function<Color4(const Color4 &, const Color4 &)> func,
                            Box2i roi = Box2i()) const;
    // for images this and other, computes this = func(this, other) for each pixel in roi, and stores result back in
    // this useful for something like a += b
    HDRImage &apply_function(const HDRImage &other, std::function<Color4(const Color4 &, const Color4 &)> func,
                             Box2i roi = Box2i());
    HDRImage  squared(Box2i roi = Box2i()) const;
    HDRImage &square(Box2i roi = Box2i());
    HDRImage &abs(Box2i roi = Box2i());
    Color4    reduce(std::function<Color4(const Color4 &, const Color4 &)> func, const Color4 &initial,
                     Box2i roi = Box2i()) const;
    Color4    sum(Box2i roi = Box2i()) const;
    Color4    mean(Box2i roi = Box2i()) const;
    Color4    min(Box2i roi = Box2i()) const;
    Color4    max_neg(Box2i roi = Box2i()) const;
    Color4    max(Box2i roi = Box2i()) const;
    HDRImage  log10ed(const Box2i &roi = Box2i()) const;
    HDRImage  exp10ed(const Box2i &roi = Box2i()) const;
    void      copy_paste(const HDRImage &src, Box2i src_roi, int dst_x = 0, int dst_y = 0, bool raw_copy = false);
    void seamless_copy_paste(AtomicProgress progress, const HDRImage &src, Box2i src_roi, int dst_x = 0, int dst_y = 0,
                             bool log_domain = false);

    //-----------------------------------------------------------------------
    //@{ \name Component-wise arithmetic and assignment.
    //-----------------------------------------------------------------------
    HDRImage operator+(const HDRImage &b) const
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a + b; });
    }
    HDRImage operator-(const HDRImage &b) const
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a - b; });
    }
    HDRImage operator*(const HDRImage &b) const
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a * b; });
    }
    HDRImage operator/(const HDRImage &b) const
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a / b; });
    }

    HDRImage &operator+=(const HDRImage &b)
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a + b; });
    }
    HDRImage &operator-=(const HDRImage &b)
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a - b; });
    }
    HDRImage &operator*=(const HDRImage &b)
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a * b; });
    }
    HDRImage &operator/=(const HDRImage &b)
    {
        return apply_function(b, [](const Color4 &a, const Color4 &b) { return a / b; });
    }

    HDRImage &operator+=(const Color4 &s)
    {
        return apply_function([s](const Color4 &a) { return a + s; });
    }
    HDRImage &operator-=(const Color4 &s) { return *this += -s; }
    HDRImage &operator*=(const Color4 &s)
    {
        return apply_function([s](const Color4 &a) { return a * s; });
    }
    HDRImage &operator/=(const Color4 &s) { return *this *= 1.f / s; }

    HDRImage operator*(const Color4 &s) const
    {
        return apply_function([s](const Color4 &a) { return s * a; });
    }
    HDRImage operator/(const Color4 &s) const { return *this * (1.0f / s); }
    HDRImage operator+(const Color4 &s) const
    {
        return apply_function([s](const Color4 &a) { return s + a; });
    }
    HDRImage operator-(const Color4 &s) const { return *this + (-s); }

    HDRImage &operator+=(float s) { return *this += Color4(s); }
    HDRImage &operator-=(float s) { return *this += -s; }
    HDRImage &operator*=(float s) { return *this *= Color4(s); }
    HDRImage &operator/=(float s) { return *this *= 1.f / s; }

    HDRImage operator*(float s) const { return *this * Color4(s); }
    HDRImage operator/(float s) const { return *this * (1.0f / s); }
    HDRImage operator+(float s) const { return *this + Color4(s); }
    HDRImage operator-(float s) const { return *this + (-s); }

    friend HDRImage operator*(float s, const HDRImage &other)
    {
        HDRImage ret(other);
        return ret *= s;
    }

    friend HDRImage operator/(float s, const HDRImage &other)
    {
        HDRImage ret(other);
        return ret *= 1.f / s;
    }

    friend HDRImage operator+(float s, const HDRImage &other)
    {
        HDRImage ret(other);
        return ret += s;
    }

    friend HDRImage operator-(float s, const HDRImage &other)
    {
        HDRImage ret(other);
        for (int i = 0; i < ret.size(); ++i) ret(i) = s - ret(i);
        return ret;
    }

    friend HDRImage operator*(const Color4 &s, const HDRImage &other)
    {
        HDRImage ret(other);
        return ret *= s;
    }

    friend HDRImage operator/(const Color4 &s, const HDRImage &other)
    {
        HDRImage ret(other);
        return ret *= 1.f / s;
    }

    friend HDRImage operator+(const Color4 &s, const HDRImage &other)
    {
        HDRImage ret(other);
        return ret += s;
    }

    friend HDRImage operator-(const Color4 &s, const HDRImage &other)
    {
        HDRImage ret(other);
        for (int i = 0; i < ret.size(); ++i) ret(i) = s - ret(i);
        return ret;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Pixel samplers.
    //-----------------------------------------------------------------------
    enum Sampler : int
    {
        NEAREST = 0,
        BILINEAR,
        BICUBIC
    };
    static const std::vector<std::string> &sampler_names();
    Color4 sample(float sx, float sy, Sampler s, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    Color4 bilinear(float sx, float sy, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    Color4 bicubic(float sx, float sy, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    Color4 nearest(float sx, float sy, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Resizing.
    //-----------------------------------------------------------------------
    enum CanvasAnchor : int
    {
        TOP_LEFT = 0,
        TOP_CENTER,
        TOP_RIGHT,
        MIDDLE_LEFT,
        MIDDLE_CENTER,
        MIDDLE_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_CENTER,
        BOTTOM_RIGHT,
        NUM_CANVAS_ANCHORS
    };
    HDRImage resized_canvas(int width, int height, CanvasAnchor anchor, const Color4 &bgColor) const;
    HDRImage resized(int width, int height) const;
    HDRImage resampled(
        int width, int height, AtomicProgress progress = AtomicProgress(),
        std::function<nanogui::Vector2f(const nanogui::Vector2f &)> warpFn = [](const nanogui::Vector2f &uv)
        { return uv; },
        int superSample = 1, Sampler s = NEAREST, BorderMode mX = REPEAT, BorderMode mY = REPEAT) const;
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Transformations.
    //-----------------------------------------------------------------------
    void     swap_rows(int a, int b);
    void     swap_columns(int a, int b);
    HDRImage flipped_vertical() const;
    HDRImage flipped_horizontal() const;
    HDRImage rotated_90_cw() const;
    HDRImage rotated_90_ccw() const;
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Image filters.
    //-----------------------------------------------------------------------
    HDRImage inverted(Box2i roi = Box2i()) const;
    HDRImage brightness_contrast(float brightness, float contrast, bool linear, EChannel c, Box2i roi = Box2i()) const;
    HDRImage laplacian_filtered(AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE,
                                Box2i roi = Box2i()) const;
    HDRImage convolved(const Array2Df &kernel, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE,
                       Box2i roi = Box2i()) const;
    HDRImage gaussian_blurred(float sigmaX, float sigmaY, AtomicProgress progress, BorderMode mX = EDGE,
                              BorderMode mY = EDGE, float truncateX = 6.0f, float truncateY = 6.0f,
                              Box2i roi = Box2i()) const;
    HDRImage gaussian_blurred_x(float sigmaX, AtomicProgress progress, BorderMode mode = EDGE, float truncateX = 6.0f,
                                Box2i roi = Box2i()) const;
    HDRImage gaussian_blurred_y(float sigmaY, AtomicProgress progress, BorderMode mode = EDGE, float truncateY = 6.0f,
                                Box2i roi = Box2i()) const;
    HDRImage iterated_box_blurred(float sigma, int iterations = 6, AtomicProgress progress = AtomicProgress(),
                                  BorderMode mX = EDGE, BorderMode mY = EDGE, Box2i roi = Box2i()) const;
    HDRImage fast_gaussian_blurred(float sigmaX, float sigmaY, AtomicProgress progress, BorderMode mX = EDGE,
                                   BorderMode mY = EDGE, Box2i roi = Box2i()) const;
    HDRImage box_blurred(int w, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE,
                         Box2i roi = Box2i()) const
    {
        return box_blurred(w, w, progress, mX, mY, roi);
    }
    HDRImage box_blurred(int hw, int hh, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE,
                         Box2i roi = Box2i()) const
    {
        return box_blurred_x(hw, AtomicProgress(progress, 0.5f), mX, roi)
            .box_blurred_y(hh, AtomicProgress(progress, 0.5f), mY, roi);
    }
    HDRImage box_blurred_x(int leftSize, int rightSize, AtomicProgress progress, BorderMode mode = EDGE,
                           Box2i roi = Box2i()) const;
    HDRImage box_blurred_x(int halfSize, AtomicProgress progress, BorderMode mode = EDGE, Box2i roi = Box2i()) const
    {
        return box_blurred_x(halfSize, halfSize, progress, mode, roi);
    }
    HDRImage box_blurred_y(int upSize, int downSize, AtomicProgress progress, BorderMode mode = EDGE,
                           Box2i roi = Box2i()) const;
    HDRImage box_blurred_y(int halfSize, AtomicProgress progress, BorderMode mode = EDGE, Box2i roi = Box2i()) const
    {
        return box_blurred_y(halfSize, halfSize, progress, mode, roi);
    }
    HDRImage unsharp_masked(float sigma, float strength, AtomicProgress progress, BorderMode mX = EDGE,
                            BorderMode mY = EDGE, Box2i roi = Box2i()) const;
    HDRImage median_filtered(float radius, int channel, AtomicProgress progress, BorderMode mX = EDGE,
                             BorderMode mY = EDGE, bool round = false, Box2i roi = Box2i()) const;
    HDRImage median_filtered(float r, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE,
                             bool round = false, Box2i roi = Box2i()) const
    {
        return median_filtered(r, 0, AtomicProgress(progress, 0.25f), mX, mY, round, roi)
            .median_filtered(r, 1, AtomicProgress(progress, 0.25f), mX, mY, round, roi)
            .median_filtered(r, 2, AtomicProgress(progress, 0.25f), mX, mY, round, roi)
            .median_filtered(r, 3, AtomicProgress(progress, 0.25f), mX, mY, round, roi);
    }
    HDRImage bilateral_filtered(float sigma_range /* = 0.1f*/, float sigma_domain /* = 1.0f*/, AtomicProgress progress,
                                BorderMode mX = EDGE, BorderMode mY = EDGE, float truncateDomain = 6.0f,
                                Box2i roi = Box2i()) const;
    //@}

    bool load(const std::string &filename);
    void load_dng(const std::string &filename);
    /*!
     * @brief           Write the file to disk.
     *
     * The output image format is deduced from the filename extension.
     *
     * @param filename  Filename to save to on disk
     * @param gain      Multiply all pixel values by gain before saving
     * @param sRGB      If not saving to an HDR format, tonemap the image to sRGB
     * @param gamma     If not saving to an HDR format, tonemap the image using this gamma value
     * @param dither    If not saving to an HDR format, dither when tonemapping down to 8-bit
     * @return          True if writing was successful
     */
    bool save(const std::string &filename, float gain, float gamma, bool sRGB, bool dither) const;
};

HDRImagePtr load_image(const std::string &filename);
