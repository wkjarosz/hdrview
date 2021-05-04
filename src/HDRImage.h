//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <Eigen/Core>            // for Array, CwiseUnaryOp, Dynamic, DenseC...
#include <functional>            // for function
#include <vector>                // for vector
#include <string>                // for string
#include "color.h"               // for Color4, max, min
#include "progress.h"


//! Floating point image
class HDRImage : public Eigen::Array<Color4,Eigen::Dynamic,Eigen::Dynamic>
{
using Base = Eigen::Array<Color4,Eigen::Dynamic,Eigen::Dynamic>;
public:
    //-----------------------------------------------------------------------
    //@{ \name Constructors, destructors, etc.
    //-----------------------------------------------------------------------
    HDRImage(void) : Base() {}
    HDRImage(int w, int h) : Base(w, h) {}
    HDRImage(const Base & other) : Base(other) {}

    //! This constructor allows you to construct a HDRImage from Eigen expressions
    template <typename OtherDerived>
    HDRImage(const Eigen::ArrayBase<OtherDerived>& other) : Base(other) { }

    //! This method allows you to assign Eigen expressions to a HDRImage
    template <typename OtherDerived>
    HDRImage& operator=(const Eigen::ArrayBase <OtherDerived>& other)
    {
        this->Base::operator=(other);
        return *this;
    }
    //@}

    bool contains(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < width() && y < height();
	}
    int width() const       { return (int)rows(); }
    int height() const      { return (int)cols(); }
    bool is_null() const     { return rows() == 0 || cols() == 0; }

    void set_alpha(float a)
    {
        *this = unaryExpr([a](const Color4 & c){return Color4(c.r,c.g,c.b,a);});
    }

    void set_channel_from(int c, const HDRImage & other)
    {
        *this = binaryExpr(other, [c](const Color4 & a, const Color4 & b){Color4 ret = a; ret[c] = b[c]; return ret;});
    }

    Color4 min() const
    {
        Color4 m = (*this)(0,0);
        for (int y = 0; y < height(); ++y)
            for (int x = 0; x < width(); ++x)
                m = ::min(m, (*this)(x,y));
        return m;
    }

    Color4 max() const
    {
        Color4 m = (*this)(0,0);
        for (int y = 0; y < height(); ++y)
            for (int x = 0; x < width(); ++x)
                m = ::max(m, (*this)(x,y));
        return m;
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
    static const std::vector<std::string> & border_mode_names();
    Color4 & pixel(int x, int y, BorderMode mX = EDGE, BorderMode mY = EDGE);
    const Color4 & pixel(int x, int y, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
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
    static const std::vector<std::string> & sampler_names();
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
    HDRImage resized_canvas(int width, int height, CanvasAnchor anchor, const Color4 & bgColor) const;
    HDRImage resized(int width, int height) const;
    HDRImage resampled(int width, int height,
                       AtomicProgress progress = AtomicProgress(),
                       std::function<Eigen::Vector2f(const Eigen::Vector2f &)> warpFn =
                       [](const Eigen::Vector2f &uv) { return uv; },
                       int superSample = 1, Sampler s = NEAREST, BorderMode mX = REPEAT, BorderMode mY = REPEAT) const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Transformations.
    //-----------------------------------------------------------------------
    HDRImage flipped_vertical() const    {return rowwise().reverse().eval();}
    HDRImage flipped_horizontal() const  {return colwise().reverse().eval();}
    HDRImage rotated_90_cw() const       {return transpose().colwise().reverse().eval();}
    HDRImage rotated_90_ccw() const      {return transpose().rowwise().reverse().eval();}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Bayer demosaicing.
    //-----------------------------------------------------------------------
    void bayer_mosaic(const Eigen::Vector2i &redOffset);

    void demosaic_linear(const Eigen::Vector2i &redOffset)
    {
        demosaic_green_linear(redOffset);
        demosaic_red_blue_linear(redOffset);
    }
    void demosaic_green_guided_linear(const Eigen::Vector2i &redOffset)
    {
        demosaic_green_linear(redOffset);
        demosaic_red_blue_green_guided_linear(redOffset);
    }
    void demosaic_malvar(const Eigen::Vector2i &redOffset)
    {
        demosaic_green_malvar(redOffset);
        demosaic_red_blue_malvar(redOffset);
    }
    void demosaicAHD(const Eigen::Vector2i &redOffset, const Eigen::Matrix3f &cameraToXYZ);

    // green channel
    void demosaic_green_linear(const Eigen::Vector2i &redOffset);
    void demosaic_green_horizontal(const HDRImage &raw, const Eigen::Vector2i &redOffset);
    void demosaic_green_vertical(const HDRImage &raw, const Eigen::Vector2i &redOffset);
    void demosaic_green_malvar(const Eigen::Vector2i &redOffset);
    void demosaic_green_phelippeau(const Eigen::Vector2i &redOffset);

    // red/blue channels
    void demosaic_red_blue_linear(const Eigen::Vector2i &redOffset);
    void demosaic_red_blue_green_guided_linear(const Eigen::Vector2i &redOffset);
    void demosaic_red_blue_malvar(const Eigen::Vector2i &redOffset);

    void demosaic_border(size_t border);

    HDRImage median_filter_bayer_artifacts() const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Image filters.
    //-----------------------------------------------------------------------
    HDRImage inverted() const;
	HDRImage brightness_contrast(float brightness, float contrast, bool linear, EChannel c) const;
    HDRImage convolved(const Eigen::ArrayXXf &kernel,
                       AtomicProgress progress,
                       BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage gaussian_blurred(float sigmaX, float sigmaY,
                              AtomicProgress progress,
                              BorderMode mX = EDGE, BorderMode mY = EDGE,
                              float truncateX = 6.0f, float truncateY = 6.0f) const;
    HDRImage gaussian_blurred_x(float sigmaX,
                                AtomicProgress progress,
                                BorderMode mode = EDGE, float truncateX = 6.0f) const;
    HDRImage gaussian_blurred_y(float sigmaY,
                                AtomicProgress progress,
                                BorderMode mode = EDGE, float truncateY = 6.0f) const;
    HDRImage iterated_box_blurred(float sigma, int iterations = 6, AtomicProgress progress = AtomicProgress(), BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage fast_gaussian_blurred(float sigmaX, float sigmaY,
                                 AtomicProgress progress,
                                 BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage box_blurred(int w, AtomicProgress progress,
                        BorderMode mX = EDGE, BorderMode mY = EDGE) const
    {
        return box_blurred(w, w, progress, mX, mY);
    }
    HDRImage box_blurred(int hw, int hh, AtomicProgress progress,
                        BorderMode mX = EDGE, BorderMode mY = EDGE) const
    {
        return box_blurred_x(hw, AtomicProgress(progress, 0.5f), mX).box_blurred_y(hh, AtomicProgress(progress, 0.5f), mY);
    }
    HDRImage box_blurred_x(int leftSize, int rightSize, AtomicProgress progress, BorderMode mode = EDGE) const;
    HDRImage box_blurred_x(int halfSize, AtomicProgress progress,
                         BorderMode mode = EDGE) const {return box_blurred_x(halfSize, halfSize, progress, mode);}
    HDRImage box_blurred_y(int upSize, int downSize, AtomicProgress progress, BorderMode mode = EDGE) const;
    HDRImage box_blurred_y(int halfSize, AtomicProgress progress,
                         BorderMode mode = EDGE) const {return box_blurred_y(halfSize, halfSize, progress, mode);}
    HDRImage unsharp_masked(float sigma, float strength, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage median_filtered(float radius, int channel, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE, bool round = false) const;
    HDRImage median_filtered(float r, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE, bool round = false) const
    {
        return median_filtered(r, 0, AtomicProgress(progress, 0.25f), mX, mY, round)
            .median_filtered(r, 1, AtomicProgress(progress, 0.25f), mX, mY, round)
            .median_filtered(r, 2, AtomicProgress(progress, 0.25f), mX, mY, round)
            .median_filtered(r, 3, AtomicProgress(progress, 0.25f), mX, mY, round);
    }
    HDRImage bilateral_filtered(float sigma_range/* = 0.1f*/,
                               float sigma_domain/* = 1.0f*/,
                               AtomicProgress progress,
                               BorderMode mX = EDGE, BorderMode mY = EDGE,
                               float truncateDomain = 6.0f) const;
    //@}

    bool load(const std::string & filename);
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
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;
};


std::shared_ptr<HDRImage> load_image(const std::string & filename);
