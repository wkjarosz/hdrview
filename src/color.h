//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <cmath>
#include <iostream>
#include <nanogui/vector.h>
// #include "fwd.h"

class Color3
{
public:
    float r, g, b;

    //-----------------------------------------------------------------------
    //@{ \name Constructors and assignment
    //-----------------------------------------------------------------------
    Color3() = default;
    // Color3(const Color3 & c) : r(c.r), g(c.g), b(c.b) {}
    Color3(const Color3 &c) = default;
    Color3(float x, float y, float z) : r(x), g(y), b(z) {}
    explicit Color3(const float c) : r(c), g(c), b(c) {}
    explicit Color3(const float *c) : r(c[0]), g(c[1]), b(c[2]) {}
    Color3 &operator=(float c)
    {
        r = g = b = c;
        return *this;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Casting operators.
    //-----------------------------------------------------------------------
    explicit operator const float *() const { return (const float *)&r; }
    explicit operator float *() { return (float *)&r; }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Element access and manipulation.
    //-----------------------------------------------------------------------
    float &      operator[](int i) { return (&r)[i]; }
    const float &operator[](int i) const { return (&r)[i]; }
    void         set(float s) { r = g = b = s; }
    void         set(float x, float y, float z)
    {
        r = x;
        g = y;
        b = z;
    }
    void set(const Color3 &c)
    {
        r = c.r;
        g = c.g;
        b = c.b;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Addition.
    //-----------------------------------------------------------------------
    Color3        operator+(const Color3 &c) const { return {r + c.r, g + c.g, b + c.b}; }
    Color3        operator+(float f) const { return {r + f, g + f, b + f}; }
    const Color3 &operator+=(const Color3 &c)
    {
        r += c.r;
        g += c.g;
        b += c.b;
        return *this;
    }
    const Color3 &operator+=(float a)
    {
        r += a;
        g += a;
        b += a;
        return *this;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Subtraction.
    //-----------------------------------------------------------------------
    Color3        operator-(const Color3 &c) const { return {r - c.r, g - c.g, b - c.b}; }
    Color3        operator-(float f) const { return {r - f, g - f, b - f}; }
    const Color3 &operator-=(const Color3 &c)
    {
        r -= c.r;
        g -= c.g;
        b -= c.b;
        return *this;
    }
    const Color3 &operator-=(float a)
    {
        r -= a;
        g -= a;
        b -= a;
        return *this;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Multiplication.
    //-----------------------------------------------------------------------
    Color3        operator*(float a) const { return {r * a, g * a, b * a}; }
    Color3        operator*(const Color3 &c) const { return {r * c.r, g * c.g, b * c.b}; }
    const Color3 &operator*=(float a)
    {
        r *= a;
        g *= a;
        b *= a;
        return *this;
    }
    const Color3 &operator*=(const Color3 &c)
    {
        r *= c.r;
        g *= c.g;
        b *= c.b;
        return *this;
    }
    Color3 operator-() const { return Color3(-r, -g, -b); }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Division.
    //-----------------------------------------------------------------------
    Color3 operator/(float a) const
    {
        float inv = 1.0f / a;
        return {r * inv, g * inv, b * inv};
    }
    Color3        operator/(const Color3 &c) const { return {r / c.r, g / c.g, b / c.b}; }
    const Color3 &operator/=(float a)
    {
        float inv = 1.0f / a;
        r *= inv;
        g *= inv;
        b *= inv;
        return *this;
    }
    const Color3 &operator/=(const Color3 &c)
    {
        r /= c.r;
        g /= c.g;
        b /= c.b;
        return *this;
    }
    //@}

    float  sum() const { return r + g + b; }
    float  average() const { return sum() / 3.0f; }
    float  luminance() const { return 0.212671f * r + 0.715160f * g + 0.072169f * b; }
    float  min() const { return std::min(std::min(r, g), b); }
    Color3 min(const Color3 &m) const { return {std::min(r, m.r), std::min(g, m.g), std::min(b, m.b)}; }
    Color3 min(float m) const { return {std::min(r, m), std::min(g, m), std::min(b, m)}; }
    float  max() const { return std::max(std::max(r, g), b); }
    Color3 max(const Color3 &m) const { return {std::max(r, m.r), std::max(g, m.g), std::max(b, m.b)}; }
    Color3 max(float m) const { return {std::max(r, m), std::max(g, m), std::max(b, m)}; }
    Color3 pow(const Color3 &exp) const
    {
        Color3 res;
        for (int i = 0; i < 3; ++i) res[i] = (*this)[i] > 0.0f ? powf((*this)[i], exp[i]) : 0.0f;
        return res;
    }

    friend std::ostream &operator<<(std::ostream &out, const Color3 &c)
    {
        return (out << c.r << " " << c.g << " " << c.b);
    }
    friend std::istream &operator>>(std::istream &in, Color3 &c) { return (in >> c.r >> c.g >> c.b); }
    friend Color3        operator*(float s, const Color3 &c) { return {c.r * s, c.g * s, c.b * s}; }
    friend Color3        operator/(float s, const Color3 &c) { return {s / c.r, s / c.g, s / c.b}; }
    friend Color3        operator+(float s, const Color3 &c) { return {s + c.r, s + c.g, s + c.b}; }
    friend Color3        operator-(float s, const Color3 &c) { return {s - c.r, s - c.g, s - c.b}; }

    /// Allows for conversion between this Color3 and nanogui's representation.
    operator const nanogui::Color() const { return nanogui::Color(r, g, b, 1.f); }
};

class Color4 : public Color3
{
public:
    float a;

    //-----------------------------------------------------------------------
    //@{ \name Constructors and assignment
    //-----------------------------------------------------------------------
    Color4()                = default;
    Color4(const Color4 &c) = default;
    Color4(float x, float y, float z, float w) : Color3(x, y, z), a(w) {}
    Color4(float g, float a) : Color3(g), a(a) {}
    Color4(const Color3 &c, float a) : Color3(c), a(a) {}
    explicit Color4(const float x) : Color3(x), a(x) {}
    explicit Color4(const float *c) : Color3(c), a(c[3]) {}
    const Color4 &operator=(float c)
    {
        r = g = b = a = c;
        return *this;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Casting operators.
    //-----------------------------------------------------------------------
    explicit operator const float *() const { return (const float *)&r; }
    explicit operator float *() { return (float *)&r; }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Element access and manipulation.
    //-----------------------------------------------------------------------
    float &      operator[](int i) { return (&r)[i]; }
    const float &operator[](int i) const { return (&r)[i]; }
    void         set(float x) { r = g = b = a = x; }
    void         set(float x, float y, float z, float w)
    {
        r = x;
        g = y;
        b = z;
        a = w;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Addition.
    //-----------------------------------------------------------------------
    Color4        operator+(const Color4 &v) const { return {r + v.r, g + v.g, b + v.b, a + v.a}; }
    Color4        operator+(float f) const { return {r + f, g + f, b + f, a + f}; }
    const Color4 &operator+=(const Color4 &v)
    {
        r += v.r;
        g += v.g;
        b += v.b;
        a += v.a;
        return *this;
    }
    const Color4 &operator+=(float c)
    {
        r += c;
        g += c;
        b += c;
        a += c;
        return *this;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Subtraction.
    //-----------------------------------------------------------------------
    Color4        operator-(const Color4 &v) const { return {r - v.r, g - v.g, b - v.b, a - v.a}; }
    Color4        operator-(float f) const { return {r - f, g - f, b - f, a - f}; }
    const Color4 &operator-=(const Color4 &v)
    {
        r -= v.r;
        g -= v.g;
        b -= v.b;
        a -= v.a;
        return *this;
    }
    const Color4 &operator-=(float c)
    {
        r -= c;
        g -= c;
        b -= c;
        a -= c;
        return *this;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Multiplication.
    //-----------------------------------------------------------------------
    Color4        operator*(float c) const { return {r * c, g * c, b * c, a * c}; }
    Color4        operator*(const Color4 &v) const { return {r * v.r, g * v.g, b * v.b, a * v.a}; }
    const Color4 &operator*=(float c)
    {
        r *= c;
        g *= c;
        b *= c;
        a *= c;
        return *this;
    }
    const Color4 &operator*=(const Color4 &v)
    {
        r *= v.r;
        g *= v.g;
        b *= v.b;
        a *= v.a;
        return *this;
    }
    Color4 operator-() const { return {-r, -g, -b, -a}; }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Division.
    //-----------------------------------------------------------------------
    Color4 operator/(float c) const
    {
        float inv = 1.0f / c;
        return {r * inv, g * inv, b * inv, a * inv};
    }
    Color4        operator/(const Color4 &v) const { return {r / v.r, g / v.g, b / v.b, a / v.a}; }
    const Color4 &operator/=(float c)
    {
        float inv = 1.0f / c;
        r *= inv;
        g *= inv;
        b *= inv;
        a *= inv;
        return *this;
    }
    const Color4 &operator/=(const Color4 &v)
    {
        r /= v.r;
        g /= v.g;
        b /= v.b;
        a /= v.a;
        return *this;
    }
    //@}

    float  sum() const { return r + g + b + a; }
    float  average() const { return sum() / 4.0f; }
    float  min() const { return std::min(Color3::min(), a); }
    Color4 min(const Color4 &m) const { return Color4(Color3::min(m), std::min(a, m.a)); }
    Color4 min(float m) const { return Color4(Color3::min(m), std::min(a, m)); }
    float  max() const { return std::max(Color3::max(), a); }
    Color4 max(const Color4 &m) const { return Color4(Color3::max(m), std::max(a, m.a)); }
    Color4 max(float m) const { return Color4(Color3::max(m), std::max(a, m)); }
    Color4 pow(const Color3 &exp) const
    {
        Color4 res;
        for (int i = 0; i < 4; ++i) res[i] = (*this)[i] > 0.0f ? powf((*this)[i], exp[i]) : 0.0f;
        return res;
    }

    friend std::ostream &operator<<(std::ostream &out, const Color4 &c)
    {
        return (out << c.r << " " << c.g << " " << c.b << " " << c.a);
    }
    friend std::istream &operator>>(std::istream &in, Color4 &c) { return (in >> c.r >> c.g >> c.b >> c.a); }
    friend Color4        operator*(float s, const Color4 &c) { return {c.r * s, c.g * s, c.b * s, c.a * s}; }
    friend Color4        operator/(float s, const Color4 &c) { return {s / c.r, s / c.g, s / c.b, s / c.a}; }
    friend Color4        operator+(float s, const Color4 &c) { return {s + c.r, s + c.g, s + c.b, c.a}; }
    friend Color4        operator-(float s, const Color4 &c) { return {s - c.r, s - c.g, s - c.b, c.a}; }

    /// Allows for conversion between this Color3 and nanogui's representation.
    operator const nanogui::Color &() const { return *(reinterpret_cast<const nanogui::Color *>(&r)); }
};

#define COLOR_FUNCTION_WRAPPER(FUNC)                                                                                   \
    inline Color3 FUNC(const Color3 &c) { return Color3(std::FUNC(c[0]), std::FUNC(c[1]), std::FUNC(c[2])); }          \
    inline Color4 FUNC(const Color4 &c)                                                                                \
    {                                                                                                                  \
        return Color4(std::FUNC(c[0]), std::FUNC(c[1]), std::FUNC(c[2]), std::FUNC(c[3]));                             \
    }

#define COLOR_FUNCTION_WRAPPER2(FUNC)                                                                                  \
    inline Color3 FUNC(const Color3 &c, const Color3 &e)                                                               \
    {                                                                                                                  \
        return Color3(std::FUNC(c[0], e[0]), std::FUNC(c[1], e[1]), std::FUNC(c[2], e[2]));                            \
    }                                                                                                                  \
    template <typename T>                                                                                              \
    inline Color3 FUNC(const Color3 &c, T e)                                                                           \
    {                                                                                                                  \
        return Color3(std::FUNC(c[0], e), std::FUNC(c[1], e), std::FUNC(c[2], e));                                     \
    }                                                                                                                  \
    inline Color4 FUNC(const Color4 &c, const Color4 &e)                                                               \
    {                                                                                                                  \
        return Color4(std::FUNC(c[0], e[0]), std::FUNC(c[1], e[1]), std::FUNC(c[2], e[2]), std::FUNC(c[3], e[3]));     \
    }                                                                                                                  \
    template <typename T>                                                                                              \
    inline Color4 FUNC(const Color4 &c, T e)                                                                           \
    {                                                                                                                  \
        return Color4(std::FUNC(c[0], e), std::FUNC(c[1], e), std::FUNC(c[2], e), std::FUNC(c[3], e));                 \
    }

//
// create vectorized versions of the math functions across the elements of
// a Color3 or Color4
//
COLOR_FUNCTION_WRAPPER(exp)
COLOR_FUNCTION_WRAPPER(exp2)
COLOR_FUNCTION_WRAPPER(expm1)
COLOR_FUNCTION_WRAPPER(log)
COLOR_FUNCTION_WRAPPER(log10)
COLOR_FUNCTION_WRAPPER(log2)
COLOR_FUNCTION_WRAPPER(log1p)
COLOR_FUNCTION_WRAPPER(fabs)
COLOR_FUNCTION_WRAPPER(abs)
COLOR_FUNCTION_WRAPPER(sqrt)
COLOR_FUNCTION_WRAPPER(cbrt)
COLOR_FUNCTION_WRAPPER(sin)
COLOR_FUNCTION_WRAPPER(cos)
COLOR_FUNCTION_WRAPPER(tan)
COLOR_FUNCTION_WRAPPER(asin)
COLOR_FUNCTION_WRAPPER(acos)
COLOR_FUNCTION_WRAPPER(atan)
COLOR_FUNCTION_WRAPPER(erf)
COLOR_FUNCTION_WRAPPER(erfc)
COLOR_FUNCTION_WRAPPER(tgamma)
COLOR_FUNCTION_WRAPPER(lgamma)
COLOR_FUNCTION_WRAPPER(ceil)
COLOR_FUNCTION_WRAPPER(floor)
COLOR_FUNCTION_WRAPPER(trunc)
COLOR_FUNCTION_WRAPPER(round)
COLOR_FUNCTION_WRAPPER2(pow)
COLOR_FUNCTION_WRAPPER2(fmin)
COLOR_FUNCTION_WRAPPER2(fmax)
COLOR_FUNCTION_WRAPPER2(min)
COLOR_FUNCTION_WRAPPER2(max)
