//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include "fwd.h"

template <typename T>
inline T sign(T a) {return (a > 0) ? T (1) : (a < 0) ? T (-1) : 0;}

/*!
 * @brief   Clamps a double between two bounds.
 *
 * This function has been specially crafted to prevent NaNs from propagating.
 *
 * @param a The value to clamp.
 * @param l The lower bound.
 * @param h The upper bound.
 * @return  The value \a a clamped to the lower and upper bounds.
 */
template <typename T>
inline T clamp(T a, T l, T h)
{
    return (a >= l) ? ((a <= h) ? a : h) : l;
}


template <typename T>
inline T clamp01(T a)
{
	return clamp(a, T(0), T(1));
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
    return T((S(1)-t) * a + t * b);
}


/*!
 * @brief Inverse linear interpolation.
 *
 * Given three values \a a, \a b, \a m, determines the parameter value
 * \a t, such that m = lerp(a,b,lerpFactor(a,b,m))
 *
 * @param a The start point
 * @param b The end point
 * @param m A third point (typically between \a a and \a b)
 * @return  The interpolation factor \a t such that m = lerp(a,b,lerpFactor(a,b,m))
 */
template <typename T>
inline T lerpFactor(T a, T b, T m)
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
inline T smoothStep(T a, T b, T x)
{
    T t = clamp(lerpFactor(a,b,x), T(0), T(1));
    return t*t*(T(3) - T(2)*t);
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
inline T smootherStep(T a, T b, T x)
{
    T t = clamp(lerpFactor(a,b,x), T(0), T(1));
    return t*t*t*(t*(t*T(6) - T(15)) + T(10));
}

/*!
 * @brief Cosine interpolation between between 0 and 1 as x moves between a and b.
 *
 * @param a A value.
 * @param b Another value.
 * @param x A number between \a a and \a b.
 * @return  A value between 0.0 and 1.0.
*/
template <typename T>
inline T cosStep(T a, T b, T x)
{
	T t = clamp(lerpFactor(a,b,x), T(0), T(1));
	return T(0.5)*(T(1)-cos(t*T(M_PI)));
}

//! The inverse of the cosStep function.
template <typename T>
inline T inverseCosStep(T a, T b, T x)
{
	T t = clamp(lerpFactor(a,b,x), T(0), T(1));
	return acos(T(1) - T(2)*t)*T(M_1_PI);
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
inline T biasPerlin(T t, T a)
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
inline T gainPerlin(T t, T P)
{
	if (t > T(0.5))
		return T(1) - T(0.5)*pow(T(2) - T(2)*t, P);
	else
		return T(0.5)*pow(T(2)*t, P);
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
inline T biasSchlick(T t, T a)
{
	return t / ((((T(1)/a) - T(2)) * (T(1) - t)) + T(1));
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
inline T gainSchlick(T t, T a)
{
	if (t < T(0.5))
		return biasSchlick(t * T(2), a)/T(2);
	else
		return biasSchlick(t * T(2) - T(1), T(1) - a)/T(2) + T(0.5);
}

template <typename T>
inline T brightnessContrastL(T v, T slope, T midpoint)
{
	return (v - midpoint) * slope + T(0.5);
}

template <typename T>
inline T brightnessContrastNL(T v, T slope, T bias)
{
	return gainPerlin(biasSchlick(clamp01(v), bias), slope);
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
