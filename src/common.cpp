//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "common.h"
#include <regex>

using namespace std;

string getExtension(const string& filename)
{
    if (filename.find_last_of(".") != string::npos)
        return filename.substr(filename.find_last_of(".")+1);
    return "";
}

string getBasename(const string& filename)
{
    auto lastSlash = filename.find_last_of("/\\");
    auto lastDot = filename.find_last_of(".");
    if (lastSlash == std::string::npos && lastDot == std::string::npos)
        return filename;

    auto start = (lastSlash != string::npos) ? lastSlash + 1 : 0;
    auto length = (lastDot != string::npos) ? lastDot-start : filename.size()-start;
    return filename.substr(start, length);
}


const vector<string> & channelNames()
{
    static const vector<string> names =
        {
            "RGB",
            "Red",
            "Green",
            "Blue",
            "Luminance",
            "CIE L*",
            "CIE a*",
            "CIE b*",
            "CIE chromaticity",
            "False color",
            "Negative-positive"
        };
    return names;
}

const vector<string> & blendModeNames()
{
    static const vector<string> names =
        {
            "Normal",
            "Multiply",
            "Divide",
            "Add",
            "Average",
            "Subtract",
            "Difference",
            "Relative difference",
        };
    return names;
}

string channelToString(EChannel channel)
{
    return channelNames()[channel];
}

string blendModeToString(EBlendMode mode)
{
    return blendModeNames()[mode];
}


// The following functions are adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

vector<string> split(string text, const string& delim)
{
    vector<string> result;
    while (true)
    {
        size_t begin = text.find_last_of(delim);
        if (begin == string::npos)
        {
            result.emplace_back(text);
            return result;
        }
        else
        {
            result.emplace_back(text.substr(begin + 1));
            text.resize(begin);
        }
    }

    return result;
}

string toLower(string str)
{
    transform(begin(str), end(str), begin(str), [](unsigned char c) { return (char)tolower(c); });
    return str;
}

string toUpper(string str)
{
    transform(begin(str), end(str), begin(str), [](unsigned char c) { return (char)toupper(c); });
    return str;
}

bool matches(string text, string filter, bool isRegex)
{
    auto matchesFuzzy = [](string text, string filter)
        {
            if (filter.empty())
                return true;

            // Perform matching on lowercase strings
            text = toLower(text);
            filter = toLower(filter);

            auto words = split(filter, ", ");
            // We don't want people entering multiple spaces in a row to match everything.
            words.erase(remove(begin(words), end(words), ""), end(words));

            if (words.empty())
                return true;

            // Match every word of the filter separately.
            for (const auto& word : words)
                if (text.find(word) != string::npos)
                    return true;

            return false;
        };

    auto matchesRegex = [](string text, string filter)
        {
            if (filter.empty())
                return true;

            try
            {
                regex searchRegex{filter, std::regex_constants::ECMAScript | std::regex_constants::icase};
                return regex_search(text, searchRegex);
            }
            catch (const regex_error&)
            {
                return false;
            }
        };

    return isRegex ? matchesRegex(text, filter) : matchesFuzzy(text, filter);
}