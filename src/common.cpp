/*!
    \file common.cpp
    \author Wojciech Jarosz
*/

#include "common.h"

using std::string;
using std::vector;

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