/*! \file Color.h
    \brief Contains the definition of Color classes
    \author Wojciech Jarosz
*/
#pragma once

#include <iostream>
#include <math.h>

class Color3
{
protected:
    float r, g, b;

public:

    //-----------------------------------------------------------------------
    //@{ \name Constructors and assignment
    //-----------------------------------------------------------------------
    Color3() {}
    Color3(const Color3 & c) : r(c.r), g(c.g), b(c.b) {}
    Color3(float x, float y, float z) : r(x), g(y), b(z) {}
    Color3(float c) : r(c), g(c), b(c) {}
    Color3(const float* c) : r(c[0]), g(c[1]), b(c[2]) {}
    const Color3 & operator=(float c) {r = g = b = c; return *this;}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Casting operators.
    //-----------------------------------------------------------------------
    operator const float*() const {return(const float*)&r;}
    operator float*() {return(float*)&r;}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Element access and manipulation.
    //-----------------------------------------------------------------------
    float& operator[](int i) {return(&r)[i];}
    const float & operator[](int i) const {return(&r)[i];}
    void set(float s) {r = g = b = s;}
    void set(float x, float y, float z) {r = x; g = y; b = z;}
    void set(const Color3& c) {r = c.r; g = c.g; b = c.b;}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Addition.
    //-----------------------------------------------------------------------
    Color3 operator+(const Color3& c) const
    {
        return Color3(r + c.r, g + c.g, b + c.b);
    }
    const Color3 & operator+=(const Color3& c)
    {
        r += c.r; g += c.g; b += c.b; return *this;
    }
    const Color3 & operator+=(float a)
    {
        r += a; g += a; b += a; return *this;
    }
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Subtraction.
    //-----------------------------------------------------------------------
    Color3 operator-(const Color3& c) const
    {
        return Color3(r - c.r, g - c.g, b - c.b);
    }
    const Color3 & operator-=(const Color3& c)
    {
        r -= c.r; g -= c.g; b -= c.b; return *this;
    }
    const Color3 & operator-=(float a)
    {
        r -= a; g -= a; b -= a; return *this;
    }
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Multiplication.
    //-----------------------------------------------------------------------
    Color3 operator*(float a) const
    {
        return Color3(r * a, g * a, b * a);
    }
    Color3 operator*(const Color3& c) const
    {
        return Color3(r * c.r, g * c.g, b * c.b);
    }
    const Color3 & operator*=(float a)
    {
        r *= a; g *= a; b *= a; return *this;
    }
    const Color3 & operator*=(const Color3& c)
    {
        r *= c.r; g *= c.g; b *= c.b; return *this;
    }
    Color3 operator-() const {return Color3(-r, -g, -b);}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Division.
    //-----------------------------------------------------------------------
    Color3 operator/(float a) const
    {
        float inv = 1.0f / a;
        return Color3(r * inv, g * inv, b * inv);
    }
    Color3 operator/(const Color3 & c) const
    {
        return Color3(r / c.r, g / c.g, b / c.b);
    }
    const Color3 & operator/=(float a)
    {
        float inv = 1.0f / a;
        r *= inv; g *= inv; b *= inv; return *this;
    }
    const Color3 & operator/=(const Color3 & c)
    {
        r /= c.r; g /= c.g; b /= c.b; return *this;
    }
    //@}
    
    float average() const {return(r + g + b) / 3.0f;}
    float luminance() const
    {
        return 0.212671f * r + 0.715160f * g + 0.072169f * b;
    }
    float min() const {return std::min(std::min(r, g), b);}
    Color3 min(float m) const
    {
        return Color3(std::min(r,m), std::min(g,m), std::min(b,m));
    }
    float max() const {return std::max(std::max(r, g), b);}
    Color3 max(float m) const
    {
        return Color3(std::max(r,m), std::max(g,m), std::max(b,m));
    }
    Color3 pow(const Color3& exp) const
    {
        Color3 res;
        for (int i = 0; i < 3; ++i)
            res[i] = (*this)[i] > 0.0f ? powf((*this)[i], exp[i]) : 0.0f;
        return res;
    }
    
    friend std::ostream& operator<<(std::ostream& out, const Color3& c)
    {
	   return(out << c.r << " " << c.g << " " << c.b);
    }
    friend std::istream& operator>>(std::istream& in, Color3& c)
    {
	   return(in >> c.r >> c.g >> c.b);
    }
    friend Color3 operator*(float s, const Color3& c)
    {
	   return Color3(c.r * s, c.g * s, c.b * s);
    }
    friend Color3 operator+(float s, const Color3& c)
    {
       return Color3(s + c.r, s + c.g, s + c.b);
    }
    friend Color3 operator-(float s, const Color3& c)
    {
       return Color3(s - c.r, s - c.g, s - c.b);
    }
};


inline Color3
expf(const Color3 & c)
{
    return Color3(::expf(c[0]), ::expf(c[1]), ::expf(c[2]));
}


inline Color3
logf(const Color3 & c)
{
    return Color3(::logf(c[0]), ::logf(c[1]), ::logf(c[2]));
}


inline Color3
powf(const Color3 & c, float e)
{
    return Color3(::powf(c[0], e), ::powf(c[1], e), ::powf(c[2], e));
}


class Color4 : public Color3
{
protected:
    float a;

public:
    
    //-----------------------------------------------------------------------
    //@{ \name Constructors and assignment
    //-----------------------------------------------------------------------
    Color4() {}
    Color4(float x, float y, float z, float w) : Color3(x, y, z), a(w) {}
    Color4(const Color3 &c, float a) : Color3(c), a(a) {}
    Color4(float x) : Color3(x), a(1.0f) {}
    Color4(const float* c) : Color3(c), a(c[3]) {}
    const Color4 & operator=(float c) {r = g = b = a = c; return *this;}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Casting operators.
    //-----------------------------------------------------------------------
    operator const float*() const {return(const float*)&r;}
    operator float*() {return(float*)&r;}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Element access and manipulation.
    //-----------------------------------------------------------------------
    float & operator[](int i) {return(&r)[i];}
    const float & operator[](int i) const {return(&r)[i];}
    void set(float x) {r = g = b = a = x;}
    void set(float x, float y, float z, float w) {r = x; g = y; b = z; a = w;}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Addition.
    //-----------------------------------------------------------------------
    Color4 operator+(const Color4& v) const
    {
        return Color4(r + v.r, g + v.g, b + v.b, a + v.a);
    }
    const Color4 & operator+=(const Color4& v)
    {
        r += v.r; g += v.g; b += v.b; a += v.a; return *this;
    }
    const Color4 & operator+=(float c)
    {
        r += c; g += c; b += c; a += c; return *this;
    }
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Subtraction.
    //-----------------------------------------------------------------------
    Color4 operator-(const Color4& v) const
    {
        return Color4(r - v.r, g - v.g, b - v.b, a - v.a);
    }
    const Color4 & operator-=(const Color4& v)
    {
        r -= v.r; g -= v.g; b -= v.b; a -= v.a; return *this;
    }
    const Color4 & operator-=(float c)
    {
        r -= c; g -= c; b -= c; a -= c; return *this;
    }
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Multiplication.
    //-----------------------------------------------------------------------
    Color4 operator*(float c) const
    {
        return Color4(r * c, g * c, b * c, a * c);
    }
    Color4 operator*(const Color4& v) const
    {
        return Color4(r * v.r, g * v.g, b * v.b, a * v.a);
    }
    const Color4 & operator*=(float c)
    {
        r *= c; g *= c; b *= c; a *= c; return *this;
    }
    const Color4 & operator*=(const Color4& v)
    {
        r *= v.r; g *= v.g; b *= v.b; a *= v.a; return *this;
    }
    Color4 operator-() const {return Color4(-r, -g, -b, -a);}
    //@}


    //-----------------------------------------------------------------------
    //@{ \name Division.
    //-----------------------------------------------------------------------
    Color4 operator/(float c) const
    {
        float inv = 1.0f / c;
        return Color4(r * inv, g * inv, b * inv, a * inv);
    }
    Color4 operator/(const Color4 & v) const
    {
        return Color4(r / v.r, g / v.g, b / v.b, a / v.a);
    }
    const Color4 & operator/=(float c)
    {
        float inv = 1.0f / c;
        r *= inv; g *= inv; b *= inv; a *= inv;
        return *this;
    }
    const Color4 & operator/=(const Color4 & v)
    {
        r /= v.r; g /= v.g; b /= v.b; a /= v.a; return *this;
    }
    //@}
    

    float average() const
    {
        return(r + g + b + a) / 4.0f;
    }
    float min() const {return std::min(Color3::min(), a);}
    Color4 min(float m) const
    {
        return Color4(Color3::min(m), std::min(a,m));
    }
    float max() const {return std::max(Color3::max(), a);}
    Color4 max(float m) const
    {
        return Color4(Color3::max(m), std::max(a,m));
    }


    friend std::ostream& operator<<(std::ostream& out, const Color4& c)
    {
	   return(out << c.r << " " << c.g << " " << c.b << " " << c.a);
    }
    friend std::istream& operator>>(std::istream& in, Color4& c)
    {
	   return(in >> c.r >> c.g >> c.b >> c.a);
    }
    friend Color4 operator*(float s, const Color4& c)
    {
	   return Color4(c.r * s, c.g * s, c.b * s, c.a * s);
    }
    friend Color4 operator+(float s, const Color4& c)
    {
       return Color4(s + c.r, s + c.g, s + c.b, c.a);
    }
    friend Color4 operator-(float s, const Color4& c)
    {
       return Color4(s - c.r, s - c.g, s - c.b, c.a);
    }
};


namespace Eigen
{

template<> struct NumTraits<Color4>
 : NumTraits<float> // permits to get the epsilon, dummy_precision, lowest, highest functions
{
    typedef Color4 Real;
    typedef Color4 NonInteger;
    typedef Color4 Nested;
    enum {
        IsComplex = 0,
        IsInteger = 0,
        IsSigned = 1,
        RequireInitialization = 1,
        ReadCost = 1,
        AddCost = 3,
        MulCost = 3
    };
};

} // namespace Eigen
