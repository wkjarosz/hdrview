/*!
    \file common.h
    \author Wojciech Jarosz
*/
#pragma once

#include <string>
#include <vector>
#include <algorithm>
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
inline T
smoothStep(T a, T b, T x)
{
    T t = clamp(T(x - a) / (b - a), T(0), T(1));
    return t*t*(T(3) - T(2)*t);
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
inline T
smoothStep6(T a, T b, T x)
{
    T t = clamp(T(x - a) / (b - a), T(0), T(1));
    return t*t*t*(t*(t*T(6) - T(15)) + T(10));
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
