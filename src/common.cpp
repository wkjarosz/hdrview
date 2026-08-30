//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "common.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>

using namespace std;
namespace fs = std::filesystem;

bool starts_with(string_view s, string_view prefix) { return s.rfind(prefix, 0) == 0; }
bool ends_with(string_view s, string_view suffix)
{
    return s.find(suffix, s.length() - suffix.length()) != string::npos;
}

string get_extension(string_view path)
{
    try
    {
        return fs::path(path).extension().u8string();
    }
    catch (...)
    {
        return "";
    }
}

string_view get_filename(string_view path)
{
    if (auto last_slash = path.find_last_of("/\\"); last_slash != string::npos)
        return path.substr(last_slash + 1);
    return path;
}

string_view get_basename(string_view path)
{
    auto last_slash = path.find_last_of("/\\");
    auto last_dot   = path.find_last_of(".");
    if (last_slash == string::npos && last_dot == string::npos)
        return path;

    auto start  = (last_slash != string::npos) ? last_slash + 1 : 0;
    auto length = (last_dot != string::npos) ? last_dot - start : path.size() - start;
    return path.substr(start, length);
}

string to_lower(string_view str)
{
    string result{str};
    transform(begin(result), end(result), begin(result), [](unsigned char c) { return (char)tolower(c); });
    return result;
}

string to_upper(string_view str)
{
    string result{str};
    transform(begin(result), end(result), begin(result), [](unsigned char c) { return (char)toupper(c); });
    return result;
}

vector<string_view> split(string_view text, string_view delim)
{
    vector<string_view> result;
    size_t              begin = 0;
    while (true)
    {
        size_t end = text.find_first_of(delim, begin);
        if (end == string::npos)
        {
            result.emplace_back(text.substr(begin));
            break;
        }
        else
        {
            result.emplace_back(text.substr(begin, end - begin));
            begin = end + 1;
        }
    }

    return result;
}

bool split_zip_entry(string_view filename, string &zip_path, string &entry_path)
{
    if (to_lower(get_extension(filename)) == ".zip")
    {
        zip_path = filename;
        entry_path.clear();
        return true;
    }
    else
    {
        auto pos = filename.find(".zip/");
        if (pos == string::npos)
        {
            zip_path = filename;
            entry_path.clear();
            return false;
        }
        pos += 4; // include ".zip"
        zip_path   = filename.substr(0, pos);
        entry_path = filename.substr(pos + 1);
        return true;
    }
}

bool is_url(string_view path)
{
    auto scheme = to_lower(path.substr(0, 8));
    return starts_with(scheme, "http://") || starts_with(scheme, "https://");
}

// The three functions below each stream over `input` line by line. They take a copy rather than reading
// through input.data(): a string_view need not be null-terminated, so a view over part of a larger buffer
// would otherwise be read well past its end.
void process_lines(string_view input, function<void(string_view)> op)
{
    istringstream iss{string(input)};
    for (string line; getline(iss, line);) op(line);
}

string add_line_numbers(string_view input)
{
    istringstream iss{string(input)};
    ostringstream oss;
    size_t        line_number = 1;

    // Calculate the number of digits in the total number of lines
    size_t total_lines = std::count(input.begin(), input.end(), '\n') + 1;
    size_t line_digits = (total_lines == 0) ? 1 : static_cast<size_t>(std::log10(total_lines)) + 1;

    for (string line; getline(iss, line);)
    {
        // Prepend the line number with padding
        oss << std::setw(line_digits) << std::setfill(' ') << line_number << ": " << line << endl;
        line_number++;
    }

    return oss.str();
}

string indent(string_view input, bool also_indent_first, int amount)
{
    istringstream iss{string(input)};
    ostringstream oss;
    string        spacer(amount, ' ');
    bool          first_line = !also_indent_first;
    for (string line; getline(iss, line);)
    {
        if (!first_line)
            oss << spacer;
        oss << line;
        if (!iss.eof())
            oss << endl;
        first_line = false;
    }
    return oss.str();
}

const vector<string> &channel_names()
{
    static const vector<string> names{"RGBA", "RGB", "Red", "Green", "Blue", "Alpha", "Luminance"};
    return names;
}

const vector<string> &blend_mode_names()
{
    static const vector<string> names{
        "Normal",
        "Multiply",
        "Divide",
        "Add",
        "Average",
        "Subtract",
        "Relative subtract",
        "Difference",
        "Relative difference",
    };
    return names;
}

static inline int code_point_length(char first)
{
    if ((first & 0xf8) == 0xf0)
        return 4;
    else if ((first & 0xf0) == 0xe0)
        return 3;
    else if ((first & 0xe0) == 0xc0)
        return 2;
    else
        return 1;
}

// This function is adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.
pair<int, int> find_common_prefix_suffix(const vector<string> &names)
{
    int begin_short_offset = 0;
    int end_short_offset   = 0;
    if (!names.empty())
    {
        string first      = names.front();
        int    first_size = (int)first.size();
        if (first_size > 0)
        {
            bool all_start_with_same_char = false;
            do {
                int len = code_point_length(first[begin_short_offset]);

                all_start_with_same_char =
                    all_of(begin(names), end(names),
                           [&first, begin_short_offset, len](const string &name)
                           {
                               if (begin_short_offset + len > (int)name.size())
                                   return false;

                               for (int i = begin_short_offset; i < begin_short_offset + len; ++i)
                                   if (name[i] != first[i])
                                       return false;

                               return true;
                           });

                if (all_start_with_same_char)
                    begin_short_offset += len;
            } while (all_start_with_same_char && begin_short_offset < first_size);

            bool all_end_with_same_char;
            do {
                char last_char         = first[first_size - end_short_offset - 1];
                all_end_with_same_char = all_of(begin(names), end(names),
                                                [last_char, end_short_offset](const string &name)
                                                {
                                                    int index = (int)name.size() - end_short_offset - 1;
                                                    return index >= 0 && name[index] == last_char;
                                                });

                if (all_end_with_same_char)
                    ++end_short_offset;
            } while (all_end_with_same_char && end_short_offset < first_size);
        }
    }
    return {begin_short_offset, end_short_offset};
}

vector<string> shorten_names(const vector<string> &names)
{
    // isalnum() is only defined for values representable as unsigned char, and a byte of a multi-byte UTF-8
    // character is negative as a plain char.
    auto is_alnum = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };

    auto [begin_short_offset, end_short_offset] = find_common_prefix_suffix(names);
    // we'll add ellipses, so don't shorten if we don't save much space
    if (begin_short_offset <= 4)
        begin_short_offset = 0;
    if (end_short_offset <= 4)
        end_short_offset = 0;

    vector<string> short_names;
    short_names.reserve(names.size());
    for (const auto &long_name : names)
    {
        size_t short_begin = (size_t)begin_short_offset;
        size_t short_end   = std::max(long_name.size() - (size_t)end_short_offset, short_begin);

        // Nothing unique is left: every other name contains this one whole, either because the paths are
        // all identical or because this name is entirely their common suffix. Its own file name is
        // complete on its own, so it needs no ellipsis on either side.
        if (short_begin >= short_end)
        {
            short_names.emplace_back(get_filename(long_name));
            continue;
        }

        // Extend beginning and ending of short region to entire word/number
        if (short_begin < long_name.size() && is_alnum(long_name[short_begin]))
            while (short_begin > 0 && is_alnum(long_name[short_begin - 1])) --short_begin;
        if (is_alnum(long_name[short_end - 1]))
            while (short_end < long_name.size() && is_alnum(long_name[short_end])) ++short_end;

        string short_name = long_name.substr(short_begin, short_end - short_begin);

        // add ellipses to indicate where we shortened
        if (short_begin != 0)
            short_name = "..." + short_name;
        if (short_end != long_name.size())
            short_name += "...";

        short_names.emplace_back(std::move(short_name));
    }
    return short_names;
}

pair<float, std::string> human_readable_size(size_t bytes)
{
    float              size       = static_cast<float>(bytes);
    static const char *units[]    = {"B", "kB", "MiB", "GiB", "TiB", "PiB"};
    int                unit_index = 0;

    while (size >= 1024 && unit_index < 5)
    {
        size /= 1024;
        ++unit_index;
    }

    return {size, units[unit_index]};
}

bool natural_less(const string_view a, const string_view b)
{
    // isdigit() is only defined for values representable as unsigned char, and a byte of a multi-byte UTF-8
    // character is negative as a plain char -- which any non-ASCII file name in a sorted folder supplies.
    auto is_digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };

    size_t ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size())
    {
        if (is_digit(a[ia]) && is_digit(b[ib]))
        {
            // Skip leading zeros
            size_t za = ia, zb = ib;
            while (za < a.size() && a[za] == '0') ++za;
            while (zb < b.size() && b[zb] == '0') ++zb;

            // Find the end of the digit sequence
            size_t enda = za;
            while (enda < a.size() && is_digit(a[enda])) ++enda;
            size_t endb = zb;
            while (endb < b.size() && is_digit(b[endb])) ++endb;

            size_t lena = enda - za;
            size_t lenb = endb - zb;

            // Compare by length first (more digits = larger number)
            if (lena != lenb)
                return lena < lenb;

            // Compare digit by digit
            for (size_t i = 0; i < lena; ++i)
            {
                if (a[za + i] != b[zb + i])
                    return a[za + i] < b[zb + i];
            }

            // If equal, shorter sequence with more leading zeros is less
            size_t total_len_a = enda - ia;
            size_t total_len_b = endb - ib;
            if (total_len_a != total_len_b)
                return total_len_a < total_len_b;

            ia = enda;
            ib = endb;
        }
        else
        {
            if (a[ia] != b[ib])
                return a[ia] < b[ib];
            ++ia;
            ++ib;
        }
    }
    return a.size() < b.size();
}

int download_percent_remaining(int64_t bytes_loaded, int64_t total_bytes)
{
    if (total_bytes <= 0)
        return 100; // nothing to measure against yet; leave the bar where it started

    const int64_t remaining = std::max<int64_t>(0, total_bytes - bytes_loaded);
    return (int)std::min<int64_t>(100, (100 * remaining + total_bytes - 1) / total_bytes);
}
