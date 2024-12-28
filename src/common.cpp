//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "common.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace std;

bool starts_with(const string &s, const string &prefix) { return s.rfind(prefix, 0) == 0; }
bool ends_with(const string &s, const string &suffix)
{
    return s.find(suffix, s.length() - suffix.length()) != string::npos;
}

string get_extension(const string &filename)
{
    if (filename.find_last_of(".") != string::npos)
        return filename.substr(filename.find_last_of(".") + 1);
    return "";
}

string get_basename(const string &filename)
{
    auto lastSlash = filename.find_last_of("/\\");
    auto lastDot   = filename.find_last_of(".");
    if (lastSlash == string::npos && lastDot == string::npos)
        return filename;

    auto start  = (lastSlash != string::npos) ? lastSlash + 1 : 0;
    auto length = (lastDot != string::npos) ? lastDot - start : filename.size() - start;
    return filename.substr(start, length);
}

string to_lower(string str)
{
    transform(begin(str), end(str), begin(str), [](unsigned char c) { return (char)tolower(c); });
    return str;
}

string to_upper(string str)
{
    transform(begin(str), end(str), begin(str), [](unsigned char c) { return (char)toupper(c); });
    return str;
}

void process_lines(string_view input, function<void(string_view)> op)
{
    istringstream iss(input.data());
    for (string line; getline(iss, line);)
        op(line);
}

string add_line_numbers(string_view input)
{
    istringstream iss(input.data());
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
    istringstream iss(input.data());
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
    static const vector<string> names{"RGB",
                                      "Red",
                                      "Green",
                                      "Blue",
                                      "Alpha",
                                      "Luminance",
                                      "Gray",
                                      "CIE L*",
                                      "CIE a*",
                                      "CIE b*",
                                      "CIE chromaticity",
                                      "False color",
                                      "Negative-positive"};
    return names;
}

const vector<string> &blend_mode_names()
{
    static const vector<string> names{
        "Normal", "Multiply", "Divide", "Add", "Average", "Subtract", "Difference", "Relative difference",
    };
    return names;
}

string channel_to_string(EChannel channel) { return channel_names()[channel]; }

string blend_mode_to_string(EBlendMode mode) { return blend_mode_names()[mode]; }

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

