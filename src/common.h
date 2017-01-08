/*!
    \file common.h
    \author Wojciech Jarosz
*/
#pragma once

#include <string>

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

std::string getExtension(const std::string& filename);
std::string getBasename(const std::string& filename);
