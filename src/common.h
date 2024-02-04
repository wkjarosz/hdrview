//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#if defined(_MSC_VER)
// Make MS cmath define M_PI but not the min/max macros
#define _USE_MATH_DEFINES
#define NOMINMAX
#endif

#include "fwd.h"
#include <string>
#include <vector>

// #include <fmt/core.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

#define MY_ASSERT(cond, description, ...)                                                                              \
    if (!(cond))                                                                                                       \
        throw std::runtime_error{fmt::format(description, ##__VA_ARGS__)};

template <typename T>
inline T sqr(T x)
{
    return x * x;
}

template <typename T>
inline T sign(T a)
{
    return (a > 0) ? T(1) : (a < 0) ? T(-1) : 0;
}

template <typename T>
inline T clamp01(T a)
{
    return std::clamp(a, T(0), T(1));
}

/*!
 * @brief  Linear interpolation.
 *
 * Linearly interpolates between \a a and \a b, using parameter t.
 *
 * @param a A value.
 * @param b Another value.
 * @param t A blending factor of \a a and \a b.
 * @return  Linear interpolation of \a a and \b -
 *          a value between a and b if \a t is between 0 and 1.
 */
template <typename T, typename S>
inline T lerp(T a, T b, S t)
{
    return T((S(1) - t) * a + t * b);
}

template <typename T>
std::vector<T> linspaced(size_t num, T a, T b)
{
    std::vector<T> retVal(num);
    for (size_t i = 0; i < num; ++i)
        retVal[i] = lerp(a, b, T(i) / (num - 1));

    return retVal;
}

/*!
 * @brief Inverse linear interpolation.
 *
 * Given three values \a a, \a b, \a m, determines the parameter value
 * \a t, such that m = lerp(a,b,lerp_factor(a,b,m))
 *
 * @param a The start point
 * @param b The end point
 * @param m A third point (typically between \a a and \a b)
 * @return  The interpolation factor \a t such that m = lerp(a,b,lerp_factor(a,b,m))
 */
template <typename T>
inline T lerp_factor(T a, T b, T m)
{
    return (m - a) / (b - a);
}

/*!
 * @brief Smoothly interpolates between 0 and 1 as x moves between a and b.
 *
 * Does a smooth s-curve (Hermite) interpolation between two values.
 *
 * @param a A value.
 * @param b Another value.
 * @param x A number between \a a and \a b.
 * @return  A value between 0.0 and 1.0.
 */
template <typename T>
inline T smoothstep(T a, T b, T x)
{
    T t = clamp01(lerp_factor(a, b, x));
    return t * t * (T(3) - T(2) * t);
}

/*!
 * @brief Smoothly interpolates between 0 and 1 as x moves between a and b.
 *
 * Does a smooth s-curve interpolation between two values using the
 * 6th-order polynomial proposed by Perlin.
 *
 * @param a A value.
 * @param b Another value.
 * @param x A number between \a a and \a b.
 * @return  A value between 0.0 and 1.0.
 */
template <typename T>
inline T smootherstep(T a, T b, T x)
{
    T t = std::clamp(lerp_factor(a, b, x), T(0), T(1));
    return t * t * t * (t * (t * T(6) - T(15)) + T(10));
}

/*!
 * @brief  Evaluates Perlin's bias function to control the mean/midpoint of a function.
 *
 * Remaps the value t to increase/decrease the midpoint while preserving the values at t=0 and t=1.
 *
 * As described in:
 * "Hypertexture"
 * Ken Perlin and Eric M. Hoffert: Computer Graphics, v23, n3, p287-296, 1989.
 *
 * Properties:
 *    bias(0.0, a) = 0,
 *    bias(0.5, a) = a,
 *    bias(1.0, a) = 1, and
 *    bias(t  , a) remaps the value t using a power curve.
 *
 * @tparam T The template parameter (typically float or double)
 * @param  t The percentage value in [0,1]
 * @param  a The shape parameter in [0,1]
 * @return   The remapped result in [0,1]
 */
template <typename T>
inline T bias_Perlin(T t, T a)
{
    return pow(t, -log2(a));
}

/*!
 * @brief  Perlin's gain function to increase/decrease the gradient/slope of the input at the midpoint.
 *
 * Remaps the value t to increase or decrease contrast using an s-curve (or inverse s-curve) function.
 *
 * As described in:
 * "Hypertexture"
 * Ken Perlin and Eric M. Hoffert: Computer Graphics, v23, n3, p287-296, 1989.
 *
 * Properties:
 *    gain(0.0, P) = 0.0,
 *    gain(0.5, P) = 0.5,
 *    gain(1.0, P) = 1.0,
 *    gain(t  , 1) = t.
 *    gain(gain(t, P, 1/P) = t.
 *
 * @tparam T The template parameter (typically float or double)
 * @param  t The percentage value in [0,1]
 * @param  P The shape exponent. In Perlin's original version the exponent P = -log2(a).
 * 			 In this version we pass the exponent directly to avoid the logarithm.
 * 			 P > 1 creates an s-curve, and P < 1 an inverse s-curve.
 * 			 If the input is a linear ramp, the slope of the output at the midpoint 0.5 becomes P.
 * @return   The remapped result in [0,1]
 */
template <typename T>
inline T gain_Perlin(T t, T P)
{
    if (t > T(0.5))
        return T(1) - T(0.5) * pow(T(2) - T(2) * t, P);
    else
        return T(0.5) * pow(T(2) * t, P);
}

/*!
 * @brief  Evaluates Schlick's rational version of Perlin's bias function.
 *
 * As described in:
 * "Fast Alternatives to Perlin's Bias and Gain Functions"
 * Christophe Schlick: Graphics Gems IV, p379-382, April 1994.
 *
 * @tparam T The template parameter (typically float or double)
 * @param  t The percentage value (between 0 and 1)
 * @param  a The shape parameter (between 0 and 1)
 * @return   The remapped result
 */
template <typename T>
inline T bias_Schlick(T t, T a)
{
    return t / ((((T(1) / a) - T(2)) * (T(1) - t)) + T(1));
}

/*!
 * @brief  Evaluates Schlick's rational version of Perlin's gain function.
 *
 * As described in:
 * "Fast Alternatives to Perlin's Bias and Gain Functions"
 * Christophe Schlick: Graphics Gems IV, p379-382, April 1994.
 *
 * @tparam T The template parameter (typically float or double)
 * @param  t The percentage value (between 0 and 1)
 * @param  a The shape parameter (between 0 and 1)
 * @return   The remapped result
 */
template <typename T>
inline T gain_Schlick(T t, T a)
{
    if (t < T(0.5))
        return bias_Schlick(t * T(2), a) / T(2);
    else
        return bias_Schlick(t * T(2) - T(1), T(1) - a) / T(2) + T(0.5);
}

template <typename T>
inline T brightness_contrast_linear(T v, T slope, T midpoint)
{
    return (v - midpoint) * slope + T(0.5);
}

template <typename T>
inline T brightness_contrast_nonlinear(T v, T slope, T bias)
{
    return gain_Perlin(bias_Schlick(clamp01(v), bias), slope);
}

//! Returns a modulus b.
template <typename T>
inline T mod(T a, T b)
{
    int n = (int)(a / b);
    a -= n * b;
    if (a < 0)
        a += b;
    return a;
}

template <typename T>
inline T log_scale(T val)
{
    static const T eps    = T(0.001);
    static const T logeps = std::log(eps);

    return val > 0 ? (std::log(val + eps) - logeps) : -(std::log(-val + eps) - logeps);
}

template <typename T>
inline T normalized_log_scale(T val, T minLog, T diffLog)
{
    return (log_scale(val) - minLog) / diffLog;
}

template <typename T>
inline T normalized_log_scale(T val)
{
    static const T minLog  = log_scale(T(0));
    static const T diffLog = log_scale(T(1)) - minLog;
    return normalized_log_scale(val, minLog, diffLog);
}

template <typename T>
inline T square(T value)
{
    return value * value;
}

//
// The code below allows us to use the fmt format/print, spdlog logging functions, and
// standard C++ iostreams like cout with linalg vectors, colors and matrices.
//

#ifndef DOXYGEN_SHOULD_SKIP_THIS
//
// Base class for both vec and mat fmtlib formatters.
//
// Based on the great blog tutorial: https://wgml.pl/blog/formatting-user-defined-types-fmt.html
//
template <typename V, typename T, bool Newline>
struct vecmat_formatter
{
    using underlying_formatter_type = fmt::formatter<T>;

    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx)
    {
        return underlying_formatter.parse(ctx);
    }

    template <typename FormatContext>
    auto format(const V &v, FormatContext &ctx)
    {
        fmt::format_to(ctx.out(), "{{");
        auto it = begin(v);
        while (true)
        {
            ctx.advance_to(underlying_formatter.format(*it, ctx));
            if (++it == end(v))
            {
                fmt::format_to(ctx.out(), "}}");
                break;
            }
            else
                fmt::format_to(ctx.out(), ",{} ", Newline ? "\n" : "");
        }
        return ctx.out();
    }

protected:
    underlying_formatter_type underlying_formatter;
};

template <typename T, int N>
struct fmt::formatter<la::vec<T, N>> : public vecmat_formatter<la::vec<T, N>, T, false>
{
};

template <typename T, int M, int N>
struct fmt::formatter<la::mat<T, M, N>> : public vecmat_formatter<la::mat<T, M, N>, la::vec<T, N>, true>
{
};

#ifdef HDRVIEW_IOSTREAMS
#include <iomanip>
#include <iostream>
template <class C, int N, class T>
std::basic_ostream<C> &operator<<(std::basic_ostream<C> &out, const Vec<N, T> &v)
{
    std::ios_base::fmtflags oldFlags = out.flags();
    auto                    width    = out.precision() + 2;

    out.setf(std::ios_base::right);
    if (!(out.flags() & std::ios_base::scientific))
        out.setf(std::ios_base::fixed);
    width += 5;

    out << '{';
    for (size_t i = 0; i < N - 1; ++i)
        out << std::setw(width) << v[i] << ',';
    out << std::setw(width) << v[N - 1] << '}';

    out.flags(oldFlags);
    return out;
}

template <class C, class T>
std::basic_ostream<C> &operator<<(std::basic_ostream<C> &s, const Mat44<T> &m)
{
    return s << "{" << m[0] << ",\n " << m[1] << ",\n " << m[2] << ",\n " << m[3] << "}";
}
#endif // HDRVIEW_IOSTREAMS

#endif // DOXYGEN_SHOULD_SKIP_THIS

bool        starts_with(const std::string &s, const std::string &prefix);
bool        ends_with(const std::string &s, const std::string &suffix);
std::string get_extension(const std::string &filename);
std::string get_basename(const std::string &filename);
std::string to_lower(std::string str);
std::string to_upper(std::string str);
/// Run func on each line of the input string
void process_lines(std::string_view input, std::function<void(std::string_view &)> op);
/// Indent the input string by amount spaces. Skips the first line by default, unless also_indent_first is true
std::string                     indent(std::string_view input, bool also_indent_first = false, int amount = 2);
std::string                     add_line_numbers(std::string_view input);
const std::vector<std::string> &channel_names();
const std::vector<std::string> &blend_mode_names();
std::string                     channel_to_string(EChannel channel);
std::string                     blend_mode_to_string(EBlendMode mode);