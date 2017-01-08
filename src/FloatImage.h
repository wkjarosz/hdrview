/*!
    \file FloatImage.h
    \brief Contains the definition of a floating-point RGBA image class
    \author Wojciech Jarosz
*/
#pragma once

#include <string>
#include <Eigen/Core>
#include <nanogui/common.h>
#include "Color.h"


//! Floating point image
class FloatImage : public Eigen::Array<Color4,Eigen::Dynamic,Eigen::Dynamic>
{
typedef Eigen::Array<Color4,Eigen::Dynamic,Eigen::Dynamic> Base;
public:
    //-----------------------------------------------------------------------
    //@{ \name Constructors, destructors, etc.
    //-----------------------------------------------------------------------
    FloatImage(void) : Base() {}
    FloatImage(int w, int h) : Base(w, h) {}
    FloatImage(const Base & other) : Base(other) {}

    //! This constructor allows you to construct a FloatImage from Eigen expressions
    template <typename OtherDerived>
    FloatImage(const Eigen::ArrayBase<OtherDerived>& other) : Base(other) { }

    //! This method allows you to assign Eigen expressions to a FloatImage
    template <typename OtherDerived>
    FloatImage& operator=(const Eigen::ArrayBase <OtherDerived>& other)
    {
        this->Base::operator=(other);
        return *this;
    }
    //@}

    int width() const {return (int)rows();}
    int height() const {return (int)cols();}

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
    typedef Color4 (FloatImage::*PixelSamplerFn)(float, float, FloatImage::BorderMode) const;
    Color4 bilinear(float sx, float sy, BorderMode mode = EDGE) const;
    Color4 bicubic(float sx, float sy, BorderMode mode = EDGE) const;
    Color4 nearest(float sx, float sy, BorderMode mode = EDGE) const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Resizing.
    //-----------------------------------------------------------------------
    FloatImage halfSize() const;
    FloatImage doubleSize() const;
    FloatImage smoothScale(int width, int height) const;

    typedef Eigen::Vector3f (*UV2XYZFn)(const Eigen::Vector2f &);
    typedef Eigen::Vector2f (*XYZ2UVFn)(const Eigen::Vector3f &);
    FloatImage resample(int width, int height,
                        UV2XYZFn dst2xyz,
                        XYZ2UVFn xyz2src,
                        PixelSamplerFn sampler,
                        int superSample = 1, BorderMode mode = REPEAT) const;
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Transformations.
    //-----------------------------------------------------------------------
    FloatImage flipVertical()   {return colwise().reverse().eval();}
    FloatImage flipHorizontal() {return rowwise().reverse().eval();}
    FloatImage rotate90CW()     {return transpose().colwise().reverse().eval();}
    FloatImage rotate90CCW()    {return transpose().rowwise().reverse().eval();}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Image filters.
    //-----------------------------------------------------------------------
    FloatImage convolve(const Eigen::ArrayXXf & kernel, BorderMode mode = EDGE) const;
    FloatImage gaussianBlur(float sigmaX, float sigmaY, BorderMode mode = EDGE,
                            float truncateX=6.0f, float truncateY=6.0f) const;
    FloatImage gaussianBlurX(float sigmaX, BorderMode mode = EDGE, float truncateX=6.0f) const;
    FloatImage gaussianBlurY(float sigmaY, BorderMode mode = EDGE, float truncateY=6.0f) const;
    FloatImage iteratedBoxBlur(float sigma, int iterations = 6, BorderMode mode = EDGE) const;
    FloatImage fastGaussianBlur(float sigmaX, float sigmaY, BorderMode mode = EDGE) const;
    FloatImage boxBlur(int w, BorderMode mode = EDGE) const{return boxBlur(w,w, mode);}
    FloatImage boxBlur(int hw, int hh, BorderMode mode = EDGE) const{return boxBlurX(hw,mode).boxBlurY(hh,mode);}
    FloatImage boxBlurX(int leftSize, int rightSize, BorderMode mode = EDGE) const;
    FloatImage boxBlurX(int halfSize, BorderMode mode = EDGE) const {return boxBlurX(halfSize, halfSize, mode);}
    FloatImage boxBlurY(int upSize, int downSize, BorderMode mode = EDGE) const;
    FloatImage boxBlurY(int halfSize, BorderMode mode = EDGE) const {return boxBlurY(halfSize, halfSize, mode);}
    FloatImage unsharpMask(float sigma, float strength, BorderMode mode = EDGE) const;
    FloatImage median(float radius, int channel, BorderMode mode = EDGE) const;
    FloatImage median(float r, BorderMode mode = EDGE) const
    {
        return median(r, 0, mode).median(r, 1, mode).median(r, 2, mode).median(r, 3, mode);
    }
    FloatImage bilateral(float sigmaRange = 0.1f,
                         float sigmaDomain = 1.0f,
                         BorderMode mode = EDGE,
                         float truncateDomain = 6.0f);
    //@}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither);
};
