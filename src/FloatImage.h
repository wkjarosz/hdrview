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
    template<typename OtherDerived>
    FloatImage(const Eigen::ArrayBase<OtherDerived>& other) : Base(other) { }
    
    //! This method allows you to assign Eigen expressions to a FloatImage
    template<typename OtherDerived>
    FloatImage& operator=(const Eigen::ArrayBase <OtherDerived>& other)
    {
        this->Base::operator=(other);
        return *this;
    }
    //@}

    int width() const {return (int)rows();}
    int height() const {return (int)cols();}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither);
};
