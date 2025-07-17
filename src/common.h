//
// Copyright (C) Wojciech Jarosz. All rights reserved.
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
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#define MY_ASSERT(cond, description, ...)                                                                              \
    if (!(cond))                                                                                                       \
        throw std::runtime_error{fmt::format(description, ##__VA_ARGS__)};

// From: https://github.com/fmtlib/fmt/issues/1260#issuecomment-1404324163
template <typename... Args>
std::string format_indented(int indent, fmt::format_string<Args...> format_str, Args &&...args)
{
    return fmt::format("{:{}}", "", indent) + fmt::format(format_str, std::forward<Args>(args)...);
}

//! Returns 1 if architecture is little endian, 0 in case of big endian.
inline bool is_little_endian()
{
    unsigned int x = 1;
    char        *c = (char *)&x;
    return bool((int)*c);
}

template <typename T>
T swap_bytes(T value)
{
    T    result;
    auto value_bytes  = reinterpret_cast<unsigned char *>(&value);
    auto result_bytes = reinterpret_cast<unsigned char *>(&result);

    for (size_t i = 0; i < sizeof(T); ++i) result_bytes[i] = value_bytes[sizeof(T) - 1 - i];

    return result;
}

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
inline T saturate(T a)
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
    for (size_t i = 0; i < num; ++i) retVal[i] = lerp(a, b, T(i) / (num - 1));

    return retVal;
}

template <size_t Num, typename T>
std::array<T, Num> linspaced(T a, T b)
{
    std::array<T, Num> ret;
    for (size_t i = 0; i < Num; ++i) ret[i] = lerp(a, b, T(i) / (Num - 1));

    return ret;
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
    T t = saturate(lerp_factor(a, b, x));
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
    return gain_Perlin(bias_Schlick(saturate(v), bias), slope);
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
inline T square(T value)
{
    return value * value;
}

bool                          starts_with(std::string_view s, std::string_view prefix);
bool                          ends_with(std::string_view s, std::string_view suffix);
std::string_view              get_extension(std::string_view path);
std::string_view              get_filename(std::string_view path);
std::string_view              get_basename(std::string_view path);
std::vector<std::string_view> split(std::string_view text, std::string_view delim);
std::string                   to_lower(std::string_view str);
std::string                   to_upper(std::string_view str);
std::pair<float, std::string> human_readable_size(size_t bytes);
/// Run func on each line of the input string
void process_lines(std::string_view input, std::function<void(std::string_view &)> op);
/// Indent the input string by amount spaces. Skips the first line by default, unless also_indent_first is true
std::string                     indent(std::string_view input, bool also_indent_first = false, int amount = 2);
std::string                     add_line_numbers(std::string_view input);
const std::vector<std::string> &tonemap_names();
const std::vector<std::string> &channel_names();
const std::vector<std::string> &blend_mode_names();
std::string                     channel_to_string(EChannel channel);
std::string                     blend_mode_to_string(EBlendMode mode);

/**
    @brief Finds the index of the next element matching a given criterion in a vector.

    This function searches for the next element in the vector that matches the criterion
    starting from the current index and proceeding in the specified direction.
    If no matching element is found after the current index, the search wraps around to the
    beginning or end of the vector based on the specified direction.
    The function stops at the current index if no matching element is found.

    \tparam T The type of elements in the vector.
    \param vec The vector to search in.
    \param current_index The index to start the search from.
    \param criterion The function object that returns true if an index-element pair matches the criterion.
        The function should take two parameters: the index of the element and a const reference to the element.
    \param direction The direction in which to search for the next matching element.
    \return int The index of the next matching element if found, or current_index if not found.
*/

template <typename T, typename Criterion>
int next_matching_index(const std::vector<T> &vec, int current_index, Criterion criterion,
                        EDirection direction = Forward)
{
    if (vec.empty())
        return current_index; // Return current index if vector is empty

    const size_t size = vec.size();

    size_t index_increment =
        (direction == EDirection::Forward) ? 1 : (size - 1); // Increment/decrement based on direction

    for (size_t i = (current_index + index_increment) % size, count = 0; count < size;
         i = (i + index_increment) % size, ++count)
        if (criterion(i, vec[i]))
            return (int)i; // Found the next matching element

    return current_index; // Nothing matched, return current index
}

/**
    Finds the index of the nth element matching a given criterion in a vector.

    \tparam T The type of elements in the vector.
    \param vec The vector to search in.
    \param n The index of the element to find.
    \param criterion The function object that returns true if an index-element pair matches the criterion.
        The function should take two parameters: the index of the element and a const reference to the element.
    \return size_t The index of the nth matching element if found, or vec.size() if not found.
*/
template <typename T, typename Criterion>
size_t nth_matching_index(const std::vector<T> &vec, size_t n, Criterion criterion)
{
    size_t match_count = 0;
    for (size_t i = 0; i < vec.size(); ++i)
        if (criterion(i, vec[i]))
            if (match_count++ == n)
                return i; // Found the nth matching element

    return vec.size(); // Return vec.size() if the nth matching element is not found
}

//! Given a collection of strings (e.g. file names) that might share a common prefix and suffix, determine the character
//! range that is unique across the strings
std::pair<int, int> find_common_prefix_suffix(const std::vector<std::string> &names);

// Compare two strings in "natural" order (e.g. file2 < file10)
bool natural_less(const std::string_view a, const std::string_view b);
