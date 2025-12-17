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

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/mdc.h>
#include <spdlog/spdlog.h>

/**
    Manages a scoped entry in the spdlog MDC (Mapped Diagnostic Context).

    Upon construction, adds a key-value pair to the MDC for the current scope.
    The key is automatically removed from the MDC when the object is destroyed.

    Useful for associating contextual information with log messages within a scope.
 */
class ScopedMDC
{
public:
    ScopedMDC(const std::string &key, const std::string &value) : m_key(key) { spdlog::mdc::put(m_key, value); }
    ~ScopedMDC() { spdlog::mdc::remove(m_key); }

private:
    std::string m_key;
};

#define MY_ASSERT(cond, description, ...)                                                                              \
    if (!(cond))                                                                                                       \
        throw std::runtime_error{fmt::format(description, ##__VA_ARGS__)};

// From: https://github.com/fmtlib/fmt/issues/1260#issuecomment-1404324163
template <typename... Args>
std::string format_indented(int indent, fmt::format_string<Args...> format_str, Args &&...args)
{
    return fmt::format("{:{}}", "", indent) + fmt::format(format_str, std::forward<Args>(args)...);
}

/**
    @brief A struct for formatting byte sizes in a human-readable way using fmt.

    This type wraps a byte count and provides a custom fmt formatter that converts it to
    human-readable units (B, KB, MB, GB, etc.) with full control over formatting.

    @par Format Specification
    The format specification grammar is:
    @code
    [[fill]align][width][.precision][mode]
    @endcode

    Where:
    - `fill` (optional): Any character to use for padding (default is space)
    - `align` (optional): Text alignment within the field width
      - `<` : Left align (e.g., "32.5 MB    ")
      - `>` : Right align (e.g., "    32.5 MB")
      - `^` : Center align (e.g., "  32.5 MB  ")
    - `width` (optional): Minimum field width for the entire output (number + space + unit)
    - `.precision` (optional): Number of decimal places for the numeric value
    - `mode` (required for human-readable output):
      - `H` : Binary units (B, KiB, MiB, GiB, TiB, PiB) using 1024 divisor
      - `h` : Decimal SI units (B, kB, MB, GB, TB, PB) using 1000 divisor
      - (omit): Raw mode - prints the raw byte count without conversion

    @par Examples
    @code
    human_readible m{32567542};
    fmt::format("{:h}", m)       // "32.567542 MB"
    fmt::format("{:.2h}", m)     // "32.57 MB"
    fmt::format("{:H}", m)       // "31.068089 MiB"
    fmt::format("{:.2H}", m)     // "31.07 MiB"
    fmt::format("{:20h}", m)     // "        32.567542 MB"
    fmt::format("{:<20h}", m)    // "32.567542 MB        "
    fmt::format("{:^20h}", m)    // "    32.567542 MB    "
    fmt::format("{:*<20h}", m)   // "32.567542 MB********"
    fmt::format("{:0>20.2h}", m) // "000000000032.57 MB"
    fmt::format("{}", m)         // "32567542" (raw mode)
    @endcode

    @note The width applies to the entire formatted output (number + space + suffix), ensuring
          the suffix stays attached to the number with proper alignment.
 */
struct human_readible
{
    std::size_t bytes;
};

/// Custom fmt::formatter specialization for human_readible
template <>
struct fmt::formatter<human_readible>
{
    fmt::formatter<double> float_fmt;

    enum class Mode
    {
        Raw,
        Binary,
        Decimal
    };
    Mode mode = Mode::Raw;

    // Store width, alignment, and fill for manual application to the complete output
    int  width = 0;
    char fill  = ' ';
    char align = '\0'; // '<', '>', '^', or '\0' for default (right)

    constexpr auto parse(format_parse_context &ctx)
    {
        auto it  = ctx.begin();
        auto end = ctx.end();

        // Parse fill and align (must come before width)
        if (it != end && it + 1 != end && (*(it + 1) == '<' || *(it + 1) == '>' || *(it + 1) == '^'))
        {
            fill  = *it;
            align = *(it + 1);
            it += 2;
        }
        else if (it != end && (*it == '<' || *it == '>' || *it == '^'))
        {
            align = *it;
            ++it;
        }

        // Parse width
        if (it != end && *it >= '0' && *it <= '9')
        {
            width = 0;
            while (it != end && *it >= '0' && *it <= '9')
            {
                width = width * 10 + (*it - '0');
                ++it;
            }
        }

        // Scan for H or h in the format string
        auto mode_it = it;
        while (mode_it != end && *mode_it != 'H' && *mode_it != 'h') ++mode_it;

        if (mode_it != end)
        {
            // Found H or h, set the mode
            mode = (*mode_it == 'H') ? Mode::Binary : Mode::Decimal;

            // Build format string for float (precision, etc., but no width/alignment)
            // Everything between current position and H/h goes to float formatter
            std::size_t len = mode_it - it;
            if (len >= 31) // Reserve 1 byte for 'f' + null terminator
                throw fmt::format_error("Format specification too long");

            if (len > 0)
            {
                char buf[32] = {};
                std::copy(it, mode_it, buf);
                buf[len]      = 'f';
                buf[len + 1]  = '\0';
                auto temp_ctx = fmt::format_parse_context(std::string_view(buf, len + 1));
                float_fmt.parse(temp_ctx);
            }

            return ++mode_it;
        }

        // No H or h found, parse as-is (raw mode or float format)
        return float_fmt.parse(ctx);
    }

    template <typename FormatContext>
    auto format(const human_readible &hs, FormatContext &ctx) const
    {
        if (mode == Mode::Raw)
            return fmt::format_to(ctx.out(), "{}", hs.bytes);

        // Calculate value and select suffix
        double      value = static_cast<double>(hs.bytes);
        const char *suffix;
        int         idx = 0;

        if (mode == Mode::Binary)
        {
            static constexpr const char *suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
            while (value >= 1024.0 && idx < 5)
            {
                value /= 1024.0;
                ++idx;
            }
            suffix = suffixes[idx];
        }
        else
        {
            static constexpr const char *suffixes[] = {"B", "kB", "MB", "GB", "TB", "PB"};
            while (value >= 1000.0 && idx < 5)
            {
                value /= 1000.0;
                ++idx;
            }
            suffix = suffixes[idx];
        }

        // Format to a temporary buffer to measure size
        auto buf      = fmt::memory_buffer();
        auto temp_ctx = fmt::format_context(fmt::appender(buf), {}, {});
        float_fmt.format(value, temp_ctx);
        fmt::format_to(fmt::appender(buf), " {}", suffix);

        // Apply width and alignment
        int result_width = static_cast<int>(buf.size());
        if (width > 0 && result_width < width)
        {
            int padding = width - result_width;

            if (align == '<') // left align
            {
                auto out = fmt::format_to(ctx.out(), "{}", fmt::to_string(buf));
                std::fill_n(out, padding, fill);
                return out;
            }
            else if (align == '^') // center align
            {
                int  left_pad = padding / 2;
                auto out      = std::fill_n(ctx.out(), left_pad, fill);
                out           = fmt::format_to(out, "{}", fmt::to_string(buf));
                return std::fill_n(out, padding - left_pad, fill);
            }
            else // right align (default or '>')
            {
                auto out = std::fill_n(ctx.out(), padding, fill);
                return fmt::format_to(out, "{}", fmt::to_string(buf));
            }
        }

        return fmt::format_to(ctx.out(), "{}", fmt::to_string(buf));
    }
};

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
std::string                   get_extension(std::string_view path);
std::string_view              get_filename(std::string_view path);
std::string_view              get_basename(std::string_view path);
std::vector<std::string_view> split(std::string_view text, std::string_view delim);
std::string                   to_lower(std::string_view str);
std::string                   to_upper(std::string_view str);
std::pair<float, std::string> human_readable_size(size_t bytes);
// Helper to split "archive.zip/entry.png" into zip and entry
bool split_zip_entry(std::string_view filename, std::string &zip_path, std::string &entry_path);

/// Run func on each line of the input string
void process_lines(std::string_view input, std::function<void(std::string_view &)> op);
/// Indent the input string by amount spaces. Skips the first line by default, unless also_indent_first is true
std::string                     indent(std::string_view input, bool also_indent_first = false, int amount = 2);
std::string                     add_line_numbers(std::string_view input);
const std::vector<std::string> &channel_names();
const std::vector<std::string> &blend_mode_names();

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
                        Direction_ direction = Direction_Forward)
{
    if (vec.empty())
        return current_index; // Return current index if vector is empty

    const size_t size = vec.size();

    size_t index_increment =
        (direction == Direction_Forward) ? 1 : (size - 1); // Increment/decrement based on direction

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
