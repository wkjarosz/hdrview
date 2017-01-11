/*!
    \file hdrimage.h
    \brief Contains the definition of a floating-point RGBA image class
    \author Wojciech Jarosz
*/
#pragma once

#include <string>
#include <Eigen/Core>
#include <nanogui/common.h>
#include "color.h"


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

    int width() const {return (int)rows();}
    int height() const {return (int)cols();}

    void setAlpha(float a)
    {
        *this = unaryExpr([a](const Color4 & c){return Color4(c.r,c.g,c.b,a);});
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
    enum BorderMode
    {
        BLACK,
        EDGE,
        REPEAT,
        MIRROR
    };
    Color4 & pixel(int x, int y, BorderMode mode = EDGE);
    const Color4 & pixel(int x, int y, BorderMode mode = EDGE) const;
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Pixel samplers.
    //-----------------------------------------------------------------------
    typedef Color4 (HDRImage::*PixelSamplerFn)(float, float, HDRImage::BorderMode) const;
    Color4 bilinear(float sx, float sy, BorderMode mode = EDGE) const;
    Color4 bicubic(float sx, float sy, BorderMode mode = EDGE) const;
    Color4 nearest(float sx, float sy, BorderMode mode = EDGE) const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Resizing.
    //-----------------------------------------------------------------------
    HDRImage halfSize() const;
    HDRImage doubleSize() const;
    HDRImage smoothScale(int width, int height) const;
    HDRImage resample(int width, int height,
                      std::function<Color4(const HDRImage &, float, float, BorderMode)> sampler =
                            [](const HDRImage & i, float x, float y, BorderMode m) {return i.bilinear(x,y,m);},
                      std::function<Eigen::Vector2f(const Eigen::Vector2f&)> warpFn =
                            [](const Eigen::Vector2f & uv) {return uv;},
                      int superSample = 1, BorderMode mode = REPEAT) const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Transformations.
    //-----------------------------------------------------------------------
    HDRImage flipVertical() const   {return colwise().reverse().eval();}
    HDRImage flipHorizontal() const {return rowwise().reverse().eval();}
    HDRImage rotate90CW() const     {return transpose().colwise().reverse().eval();}
    HDRImage rotate90CCW() const    {return transpose().rowwise().reverse().eval();}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Image filters.
    //-----------------------------------------------------------------------
    HDRImage convolve(const Eigen::ArrayXXf & kernel, BorderMode mode = EDGE) const;
    HDRImage gaussianBlur(float sigmaX, float sigmaY, BorderMode mode = EDGE,
                            float truncateX=6.0f, float truncateY=6.0f) const;
    HDRImage gaussianBlurX(float sigmaX, BorderMode mode = EDGE, float truncateX=6.0f) const;
    HDRImage gaussianBlurY(float sigmaY, BorderMode mode = EDGE, float truncateY=6.0f) const;
    HDRImage iteratedBoxBlur(float sigma, int iterations = 6, BorderMode mode = EDGE) const;
    HDRImage fastGaussianBlur(float sigmaX, float sigmaY, BorderMode mode = EDGE) const;
    HDRImage boxBlur(int w, BorderMode mode = EDGE) const{return boxBlur(w,w, mode);}
    HDRImage boxBlur(int hw, int hh, BorderMode mode = EDGE) const{return boxBlurX(hw,mode).boxBlurY(hh,mode);}
    HDRImage boxBlurX(int leftSize, int rightSize, BorderMode mode = EDGE) const;
    HDRImage boxBlurX(int halfSize, BorderMode mode = EDGE) const {return boxBlurX(halfSize, halfSize, mode);}
    HDRImage boxBlurY(int upSize, int downSize, BorderMode mode = EDGE) const;
    HDRImage boxBlurY(int halfSize, BorderMode mode = EDGE) const {return boxBlurY(halfSize, halfSize, mode);}
    HDRImage unsharpMask(float sigma, float strength, BorderMode mode = EDGE) const;
    HDRImage median(float radius, int channel, BorderMode mode = EDGE) const;
    HDRImage median(float r, BorderMode mode = EDGE) const
    {
        return median(r, 0, mode).median(r, 1, mode).median(r, 2, mode).median(r, 3, mode);
    }
    HDRImage bilateral(float sigmaRange = 0.1f,
                         float sigmaDomain = 1.0f,
                         BorderMode mode = EDGE,
                         float truncateDomain = 6.0f) const;
    //@}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;
};
