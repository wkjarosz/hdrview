/*!
    \file hdrimage.h
    \brief Contains the definition of a floating-point RGBA image class
    \author Wojciech Jarosz
*/
#pragma once

#include <Eigen/Core>            // for Array, CwiseUnaryOp, Dynamic, DenseC...
#include <functional>            // for function
#include <iosfwd>                // for string
#include "color.h"               // for Color4, max, min


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
        BLACK = 0,
        EDGE,
        REPEAT,
        MIRROR
    };
    Color4 & pixel(int x, int y, BorderMode mX = EDGE, BorderMode mY = EDGE);
    const Color4 & pixel(int x, int y, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Pixel samplers.
    //-----------------------------------------------------------------------
    enum Sampler
    {
        NEAREST = 0,
        BILINEAR,
        BICUBIC
    };
    Color4 sample(float sx, float sy, Sampler s, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    Color4 bilinear(float sx, float sy, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    Color4 bicubic(float sx, float sy, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    Color4 nearest(float sx, float sy, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Resizing.
    //-----------------------------------------------------------------------
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
    //@{ \name Image filters.
    //-----------------------------------------------------------------------
    HDRImage convolved(const Eigen::ArrayXXf &kernel, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage GaussianBlurred(float sigmaX, float sigmaY, BorderMode mX = EDGE, BorderMode mY = EDGE,
                             float truncateX = 6.0f, float truncateY = 6.0f) const;
    HDRImage GaussianBlurredX(float sigmaX, BorderMode mode = EDGE, float truncateX = 6.0f) const;
    HDRImage GaussianBlurredY(float sigmaY, BorderMode mode = EDGE, float truncateY = 6.0f) const;
    HDRImage iteratedBoxBlurred(float sigma, int iterations = 6, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage fastGaussianBlurred(float sigmaX, float sigmaY, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage boxBlurred(int w, BorderMode mX = EDGE, BorderMode mY = EDGE) const{return boxBlurred(w, w, mX, mY);}
    HDRImage boxBlurred(int hw, int hh, BorderMode mX = EDGE, BorderMode mY = EDGE) const{return boxBlurredX(hw, mX).boxBlurredY(hh, mY);}
    HDRImage boxBlurredX(int leftSize, int rightSize, BorderMode mode = EDGE) const;
    HDRImage boxBlurredX(int halfSize, BorderMode mode = EDGE) const {return boxBlurredX(halfSize, halfSize, mode);}
    HDRImage boxBlurredY(int upSize, int downSize, BorderMode mode = EDGE) const;
    HDRImage boxBlurredY(int halfSize, BorderMode mode = EDGE) const {return boxBlurredY(halfSize, halfSize, mode);}
    HDRImage unsharpMasked(float sigma, float strength, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage medianFiltered(float radius, int channel, BorderMode mX = EDGE, BorderMode mY = EDGE) const;
    HDRImage medianFiltered(float r, BorderMode mX = EDGE, BorderMode mY = EDGE) const
    {
        return medianFiltered(r, 0, mX, mY).medianFiltered(r, 1, mX, mY).medianFiltered(r, 2, mX, mY).medianFiltered(r, 3, mX, mY);
    }
    HDRImage bilateralFiltered(float sigmaRange = 0.1f,
                               float sigmaDomain = 1.0f,
                               BorderMode mX = EDGE, BorderMode mY = EDGE,
                               float truncateDomain = 6.0f) const;
    //@}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;
};
