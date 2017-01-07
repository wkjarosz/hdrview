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
    FloatImage convolve(const Eigen::ArrayXXf & kernel) const;
    FloatImage gaussianBlur(float sigmaX, float sigmaY,
                            float truncateX=6.0f, float truncateY=6.0f) const;
    FloatImage gaussianBlurX(float sigmaX, float truncateX=6.0f) const;
    FloatImage gaussianBlurY(float sigmaY, float truncateY=6.0f) const;
    FloatImage iteratedBoxBlur(float sigma, int iterations = 6) const;
    FloatImage fastGaussianBlur(float sigmaX, float sigmaY) const;
    FloatImage boxBlur(int w) const{return boxBlur(w,w);}
    FloatImage boxBlur(int hw, int hh) const{return boxBlurX(hw).boxBlurY(hh);}
    FloatImage boxBlurX(int leftSize, int rightSize) const;
    FloatImage boxBlurX(int halfSize) const {return boxBlurX(halfSize, halfSize);}
    FloatImage boxBlurY(int upSize, int downSize) const;
    FloatImage boxBlurY(int halfSize) const {return boxBlurY(halfSize, halfSize);}
    FloatImage unsharpMask(float sigma, float strength) const;
    FloatImage median(float radius, int channel) const;
    FloatImage median(float r) const
    {
        return median(r, 0).median(r, 1).median(r, 2).median(r, 3);
    }
    FloatImage bilateral(float sigmaRange = 0.1f,
                         float sigmaDomain = 1.0f,
                         float truncateDomain = 3.0f);
    //@}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither);
};
