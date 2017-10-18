//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include "fwd.h"

template <typename T>
inline T sign(T a) {return (a > 0) ? T (1) : (a < 0) ? T (-1) : 0;}

//! Clamps a double between two bounds.
/*!
    \param a The value to test.
    \param l The lower bound.
    \param h The upper bound.
    \return The value \a a clamped to the lower and upper bounds.

    This function has been specially crafted to prevent NaNs from propagating.
*/
template <typename T>
inline T clamp(T a, T l, T h)
{
    return (a >= l) ? ((a <= h) ? a : h) : l;
}


//! Linear interpolation.
/*!
    Linearly interpolates between \a a and \a b, using parameter t.
    \param a A value.
    \param b Another value.
    \param t A blending factor of \a a and \a b.
    \return Linear interpolation of \a a and \b -
            a value between a and b if \a t is between 0 and 1.
*/
template <typename T, typename S>
inline T lerp(T a, T b, S t)
{
    return T((S(1)-t) * a + t * b);
}

//!
/*!
 * @brief Inverse linear interpolation.
 *
 * Given three values \a a, \a b, \a m, determines the parameter value
 * \a t, such that m = lerp(a,b,lerpFactor(a,b,m))
 *
 * @param a     The start point
 * @param b     The end point
 * @param m     A third point (typically between \a a and \a b)
 * @return      The interpolation factor \a t such that m = lerp(a,b,lerpFactor(a,b,m))
 */
template <typename T>
inline T lerpFactor(T a, T b, T m)
{
    return (m - a) / (b - a);
}


//! Smoothly interpolates between a and b.
/*!
    Does a smooth s-curve (Hermite) interpolation between two values.
    \param a A value.
    \param b Another value.
    \param x A number between \a a and \a b.
    \return A value between 0.0 and 1.0.
*/
template <typename T>
inline T smoothStep(T a, T b, T x)
{
    T t = clamp(T(x - a) / (b - a), T(0), T(1));
    return t*t*(T(3) - T(2)*t);
}

//! The inverse of the smoothstep function.
template <typename T>
inline T inverseSmoothStep(T a, T b, T x)
{
    T t = clamp(T(x - a) / (b - a), T(0), T(1));
    return t + t - t*t*(T(3) - T(2)*t);
}

//! Smoothly interpolates between a and b.
/*!
    Does a smooth s-curve (6th order) interpolation between two values.
    \param a A value.
    \param b Another value.
    \param x A number between \a a and \a b.
    \return A value between 0.0 and 1.0.
*/
template <typename T>
inline T smoothStep6(T a, T b, T x)
{
    T t = clamp(T(x - a) / (b - a), T(0), T(1));
    return t*t*t*(t*(t*T(6) - T(15)) + T(10));
}

/*!
 * \brief  Evaluates Schlick's rational version of Perlin's bias function.
 *
 * As described in:
 * "Fast Alternatives to Perlin's Bias and Gain Functions"
 * Christophe Schlick: Graphics Gems IV, p379-382, April 1994.
 *
 * @tparam T The template parameter (typically float or double)
 * @param  t The percentage value (between 0 and 1)
 * @param  a The shape parameter (between 0 and 1)
 * @return
 */
template <typename T>
inline T bias(T t, T a)
{
    return t / ((((T(1)/a) - T(2)) * (T(1) - t)) + T(1));
}

/*!
 * \brief  Evaluates Schlick's rational version of Perlin's gain function.
 *
 * As described in:
 * "Fast Alternatives to Perlin's Bias and Gain Functions"
 * Christophe Schlick: Graphics Gems IV, p379-382, April 1994.
 *
 * @tparam T The template parameter (typically float or double)
 * @param  t The percentage value (between 0 and 1)
 * @param  a The shape parameter (between 0 and 1)
 * @return
 */
template <typename T>
inline T gain(T t, T a)
{
    if (t < T(0.5))
        return bias(t * T(2), a)/T(2);
    else
        return bias(t * T(2) - T(1), T(1) - a)/T(2) + T(0.5);
}


//! Returns a modulus b.
template <typename T>
inline T mod(T a, T b)
{
    int n = (int)(a/b);
    a -= n*b;
    if (a < 0)
        a += b;
    return a;
}


template <typename T>
inline T logScale(T val)
{
    static const T eps = T(0.001);
    static const T logeps = std::log(eps);

    return val > 0 ? (std::log(val + eps) - logeps) : -(std::log(-val + eps) - logeps);
}


template <typename T>
inline T normalizedLogScale(T val, T minLog, T diffLog)
{
    return (logScale(val) - minLog) / diffLog;
}


template <typename T>
inline T normalizedLogScale(T val)
{
    static const T minLog = logScale(T(0));
    static const T diffLog = logScale(T(1)) - minLog;
    return normalizedLogScale(val, minLog, diffLog);
}


template <typename T>
inline const T& min(const T& a, const T& b, const T& c)
{
    return std::min(std::min(a, b), c);
}

template <typename T>
inline const T& min(const T& a, const T& b, const T& c, const T& d)
{
    return std::min(min(a, b, c), d);
}

template <typename T>
inline const T& min(const T& a, const T& b, const T& c, const T& d, const T& e)
{
    return std::min(min(a, b, c, d), e);
}

template <typename T>
inline const T& max(const T& a, const T& b, const T& c)
{
    return std::max(std::max(a, b), c);
}

template <typename T>
inline const T& max(const T& a, const T& b, const T& c, const T& d)
{
    return std::max(max(a, b, c), d);
}

template <typename T>
inline const T& max(const T& a, const T& b, const T& c, const T& d, const T& e)
{
    return std::max(max(a, b, c, d), e);
}

template <typename T>
inline T square(T value)
{
    return value*value;
}

std::string getExtension(const std::string& filename);
std::string getBasename(const std::string& filename);


const std::vector<std::string> & channelNames();
const std::vector<std::string> & blendModeNames();
std::string channelToString(EChannel channel);
std::string blendModeToString(EBlendMode mode);
