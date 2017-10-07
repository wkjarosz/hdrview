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
typedef Eigen::Array<Color4,Eigen::Dynamic,Eigen::Dynamic> Base;
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

    int width() const       { return (int)rows(); }
    int height() const      { return (int)cols(); }
    bool isNull() const     { return rows() == 0 || cols() == 0; }

    void setAlpha(float a)
    {
        *this = unaryExpr([a](const Color4 & c){return Color4(c.r,c.g,c.b,a);});
    }

    void setChannelFrom(int c, const HDRImage & other)
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
    static const std::vector<std::string> & borderModeNames();
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
    static const std::vector<std::string> & samplerNames();
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
    HDRImage resizedCanvas(int width, int height, CanvasAnchor anchor, const Color4 & bgColor) const;
    HDRImage resized(int width, int height) const;
    HDRImage resampled(int width, int height,
                       std::function<Eigen::Vector2f(const Eigen::Vector2f &)> warpFn =
                       [](const Eigen::Vector2f &uv) { return uv; },
                       int superSample = 1, Sampler s = NEAREST, BorderMode mX = REPEAT, BorderMode mY = REPEAT) const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Transformations.
    //-----------------------------------------------------------------------
    HDRImage flippedVertical() const    {return rowwise().reverse().eval();}
    HDRImage flippedHorizontal() const  {return colwise().reverse().eval();}
    HDRImage rotated90CW() const        {return transpose().colwise().reverse().eval();}
    HDRImage rotated90CCW() const       {return transpose().rowwise().reverse().eval();}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Bayer demosaicing.
    //-----------------------------------------------------------------------
    void bayerMosaic(const Eigen::Vector2i &redOffset);

    void demosaicLinear(const Eigen::Vector2i &redOffset)
    {
        demosaicGreenLinear(redOffset);
        demosaicRedBlueLinear(redOffset);
    }
    void demosaicGreenGuidedLinear(const Eigen::Vector2i &redOffset)
    {
        demosaicGreenLinear(redOffset);
        demosaicRedBlueGreenGuidedLinear(redOffset);
    }
    void demosaicMalvar(const Eigen::Vector2i &redOffset)
    {
        demosaicGreenMalvar(redOffset);
        demosaicRedBlueMalvar(redOffset);
    }
    void demosaicAHD(const Eigen::Vector2i &redOffset, const Eigen::Matrix3f &cameraToXYZ);

    // green channel
    void demosaicGreenLinear(const Eigen::Vector2i &redOffset);
    void demosaicGreenHorizontal(const HDRImage &raw, const Eigen::Vector2i &redOffset);
    void demosaicGreenVertical(const HDRImage &raw, const Eigen::Vector2i &redOffset);
    void demosaicGreenMalvar(const Eigen::Vector2i &redOffset);
    void demosaicGreenPhelippeau(const Eigen::Vector2i &redOffset);

    // red/blue channels
    void demosaicRedBlueLinear(const Eigen::Vector2i &redOffset);
    void demosaicRedBlueGreenGuidedLinear(const Eigen::Vector2i &redOffset);
    void demosaicRedBlueMalvar(const Eigen::Vector2i &redOffset);

    void demosaicBorder(size_t border);

    HDRImage medianFilterBayerArtifacts() const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Image filters.
    //-----------------------------------------------------------------------
    HDRImage convolved(const Eigen::ArrayXXf &kernel,
                       AtomicProgress progress,
                       BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage GaussianBlurred(float sigmaX, float sigmaY,
                             AtomicProgress progress,
                             BorderMode mX = EDGE, BorderMode mY = EDGE,
                             float truncateX = 6.0f, float truncateY = 6.0f) const;
    HDRImage GaussianBlurredX(float sigmaX,
                              AtomicProgress progress,
                              BorderMode mode = EDGE, float truncateX = 6.0f) const;
    HDRImage GaussianBlurredY(float sigmaY,
                              AtomicProgress progress,
                              BorderMode mode = EDGE, float truncateY = 6.0f) const;
    HDRImage iteratedBoxBlurred(float sigma, int iterations = 6, AtomicProgress progress = AtomicProgress(), BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage fastGaussianBlurred(float sigmaX, float sigmaY,
                                 AtomicProgress progress,
                                 BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage boxBlurred(int w, AtomicProgress progress,
                        BorderMode mX = EDGE, BorderMode mY = EDGE) const
    {
        return boxBlurred(w, w, progress, mX, mY);
    }
    HDRImage boxBlurred(int hw, int hh, AtomicProgress progress,
                        BorderMode mX = EDGE, BorderMode mY = EDGE) const
    {
        return boxBlurredX(hw, AtomicProgress(progress, 0.5f), mX).boxBlurredY(hh, AtomicProgress(progress, 0.5f), mY);
    }
    HDRImage boxBlurredX(int leftSize, int rightSize, AtomicProgress progress, BorderMode mode = EDGE) const;
    HDRImage boxBlurredX(int halfSize, AtomicProgress progress,
                         BorderMode mode = EDGE) const {return boxBlurredX(halfSize, halfSize, progress, mode);}
    HDRImage boxBlurredY(int upSize, int downSize, AtomicProgress progress, BorderMode mode = EDGE) const;
    HDRImage boxBlurredY(int halfSize, AtomicProgress progress,
                         BorderMode mode = EDGE) const {return boxBlurredY(halfSize, halfSize, progress, mode);}
    HDRImage unsharpMasked(float sigma, float strength, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage medianFiltered(float radius, int channel, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE, bool round = false) const;
    HDRImage medianFiltered(float r, AtomicProgress progress, BorderMode mX = EDGE, BorderMode mY = EDGE, bool round = false) const
    {
        return medianFiltered(r, 0, AtomicProgress(progress, 0.25f), mX, mY, round)
            .medianFiltered(r, 1, AtomicProgress(progress, 0.25f), mX, mY, round)
            .medianFiltered(r, 2, AtomicProgress(progress, 0.25f), mX, mY, round)
            .medianFiltered(r, 3, AtomicProgress(progress, 0.25f), mX, mY, round);
    }
    HDRImage bilateralFiltered(float sigmaRange/* = 0.1f*/,
                               float sigmaDomain/* = 1.0f*/,
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


std::shared_ptr<HDRImage> loadImage(const std::string & filename);
