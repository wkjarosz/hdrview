//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "common.h"
#include <cmath>
#include <float.h>

#include "dithermatrix256.h"

#include <ImfChromaticities.h>
#include <ImfRgbaYca.h>
#include <ImfStandardAttributes.h>

using namespace std;

namespace
{

// Reference whites used in the common color spaces below
const std::map<string, Imath::V2f> referenceWhites = {{"C", {0.31006f, 0.31616f}},
                                                      {"D50", {0.34567f, 0.35850f}},
                                                      {"D65", {0.31271f, 0.32902f}},
                                                      {"E", {0.33333f, 0.33333f}}};

// data from:
//  https://en.wikipedia.org/wiki/Standard_illuminant
//  https://en.wikipedia.org/wiki/RGB_color_spaces
//  http://www.brucelindbloom.com/index.html?WorkingSpaceInfo.html
// Chromaticity data for common color spaces
const std::map<string, Imf::Chromaticities> chromaticitiesMap = {
    {"ACES 2065-1 (Academy Color Encoding System, AP0)",
     {{0.73470f, 0.26530f}, {0.00000f, 1.00000f}, {0.00010f, -0.07700f}, {0.32168f, 0.33767f}}},
    {"ACEScg (Academy Color Encoding System, AP1)",
     {{0.713f, 0.293f}, {0.165f, 0.830f}, {0.128f, 0.044f}, {0.32168f, 0.33767f}}},
    {"Adobe RGB (1998)", {{0.6400f, 0.3300f}, {0.2100f, 0.7100f}, {0.1500f, 0.0600f}, referenceWhites.at("D65")}},
    {"Apple RGB", {{0.6250f, 0.3400f}, {0.2800f, 0.5950f}, {0.1550f, 0.0700f}, referenceWhites.at("D65")}},
    {"Best RGB", {{0.7347f, 0.2653f}, {0.2150f, 0.7750f}, {0.1300f, 0.0350f}, referenceWhites.at("D50")}},
    {"Beta RGB", {{0.6888f, 0.3112f}, {0.1986f, 0.7551f}, {0.1265f, 0.0352f}, referenceWhites.at("D50")}},
    {"Bruce RGB", {{0.6400f, 0.3300f}, {0.2800f, 0.6500f}, {0.1500f, 0.0600f}, referenceWhites.at("D65")}},
    {"BT 2020/2100", {{0.7080f, 0.2920f}, {0.1700f, 0.7970f}, {0.1310f, 0.0460f}, referenceWhites.at("D65")}},
    {"CIE RGB", {{0.7350f, 0.2650f}, {0.2740f, 0.7170f}, {0.1670f, 0.0090f}, referenceWhites.at("E")}},
    {"CIE XYZ", {{1.f, 0.f}, {0.f, 1.f}, {0.f, 0.f}, referenceWhites.at("E")}},
    {"ColorMatch RGB", {{0.6300f, 0.3400f}, {0.2950f, 0.6050f}, {0.1500f, 0.0750f}, referenceWhites.at("D50")}},
    {"Display P3", {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, referenceWhites.at("D65")}},
    {"Don RGB 4", {{0.6960f, 0.3000f}, {0.2150f, 0.7650f}, {0.1300f, 0.0350f}, referenceWhites.at("D50")}},
    {"ECI RGB v2", {{0.6700f, 0.3300f}, {0.2100f, 0.7100f}, {0.1400f, 0.0800f}, referenceWhites.at("D50")}},
    {"Ekta Space PS5", {{0.6950f, 0.3050f}, {0.2600f, 0.7000f}, {0.1100f, 0.0050f}, referenceWhites.at("D50")}},
    {"NTSC RGB", {{0.6700f, 0.3300f}, {0.2100f, 0.7100f}, {0.1400f, 0.0800f}, referenceWhites.at("C")}},
    {"PAL/SECAM RGB", {{0.6400f, 0.3300f}, {0.2900f, 0.6000f}, {0.1500f, 0.0600f}, referenceWhites.at("D65")}},
    {"ProPhoto RGB", {{0.7347f, 0.2653f}, {0.1596f, 0.8404f}, {0.0366f, 0.0001f}, referenceWhites.at("D50")}},
    {"SMPTE-C RGB", {{0.6300f, 0.3400f}, {0.3100f, 0.5950f}, {0.1550f, 0.0700f}, referenceWhites.at("D65")}},
    {"sRGB/BT 709", {{0.6400f, 0.3300f}, {0.3000f, 0.6000f}, {0.1500f, 0.0600f}, referenceWhites.at("D65")}},
    {"Wide Gamut RGB", {{0.7350f, 0.2650f}, {0.1150f, 0.8260f}, {0.1570f, 0.0180f}, referenceWhites.at("D50")}}};

} // namespace

const std::map<string, Imf::Chromaticities> &color_space_chromaticities() { return chromaticitiesMap; }

const Imf::Chromaticities &color_space_chromaticity(const string &name) { return chromaticitiesMap.at(name); }

const std::map<string, Imath::V2f> &white_points() { return referenceWhites; }

const Imath::V2f &white_point(const string &name) { return referenceWhites.at(name); }

bool color_conversion_matrix(Imath::M33f &M, const Imf::Chromaticities &src, const Imf::Chromaticities &dst,
                             int CAT_method)
{
    using namespace Imath;
    using namespace Imf;

    if (src == dst)
    {
        // The file already contains data in the target colorspace.
        // color conversion is not necessary.
        M = M33f{};
        return false;
    }

    //
    // Create a matrix that transforms colors from the
    // RGB space of the input file into the target space
    // using a color adaptation transform to move the
    // white point.
    //

    M33f CAT{}; // chromatic adaptation matrix
    if (CAT_method > 0 && CAT_method <= 3)
    {
        // the cone primary respons matrices (and their inverses) for 3 different methods
        static const M33f CPM[3] = {{1.f, 0.f, 0.f, // XYZ scaling
                                     0.f, 1.f, 0.f, //
                                     0.f, 0.f, 1.f},
                                    {0.895100f, -0.750200f, 0.038900f, // Bradford
                                     0.266400f, 1.713500f, -0.068500f, //
                                     -0.161400f, 0.036700f, 1.029600f},
                                    {0.4002400f, -0.2263000f, 0.0000000f, // Von Kries
                                     0.7076000f, 1.1653200f, 0.0000000f,  //
                                     -0.0808100f, 0.0457000f, 0.9182200f}};
        static const M33f invCPM[3]{{1.f, 0.f, 0.f, //
                                     0.f, 1.f, 0.f, //
                                     0.f, 0.f, 1.f},
                                    {0.986993f, 0.432305f, -0.008529f, //
                                     -0.147054f, 0.518360f, 0.040043f, //
                                     0.159963f, 0.049291f, 0.968487f},
                                    {1.8599364f, 0.3611914f, 0.0000000f,  //
                                     -1.1293816f, 0.6388125f, 0.0000000f, //
                                     0.2198974f, -0.0000064f, 1.0890636f}};
        //
        // Convert the white points of the two RGB spaces to XYZ
        //

        float fx = src.white.x;
        float fy = src.white.y;
        V3f   src_neutral_XYZ(fx / fy, 1, (1 - fx - fy) / fy);

        float ax = dst.white.x;
        float ay = dst.white.y;
        V3f   dst_neutral_XYZ(ax / ay, 1, (1 - ax - ay) / ay);

        //
        // Compute the CAT
        //

        V3f ratio((dst_neutral_XYZ * CPM[CAT_method - 1]) / (src_neutral_XYZ * CPM[CAT_method - 1]));

        M33f ratio_mat(ratio[0], 0, 0, 0, ratio[1], 0, 0, 0, ratio[2]);

        CAT = CPM[CAT_method - 1] * ratio_mat * invCPM[CAT_method - 1];
    }

    //
    // Build a combined file-RGB-to-target-RGB conversion matrix
    //

    auto m1 = RGBtoXYZ(src, 1);
    // extract the upper left 3x3 of m1 as an M33f
    M33f src_to_XYZ(m1[0][0], m1[0][1], m1[0][2], //
                    m1[1][0], m1[1][1], m1[1][2], //
                    m1[2][0], m1[2][1], m1[2][2]);

    m1 = XYZtoRGB(dst, 1);
    // extract the upper left 3x3 of m1 as an M33f
    M33f XYZ_to_dst(m1[0][0], m1[0][1], m1[0][2], //
                    m1[1][0], m1[1][1], m1[1][2], //
                    m1[2][0], m1[2][1], m1[2][2]);

    M = src_to_XYZ * CAT * XYZ_to_dst;

    return true;
}

float3 YCToRGB(float3 input, float3 Yw)
{
    if (input[0] == 0.0 && input[2] == 0.0)
        //
        // Special case -- both chroma channels are 0.  To avoid
        // rounding errors, we explicitly set the output R, G and B
        // channels equal to the input luminance.
        //
        return float3(input[1], input[1], input[1]);

    float Y = input[1];
    float r = (input[0] + 1.0) * input[1];
    float b = (input[2] + 1.0) * input[1];
    float g = (Y - r * Yw.x - b * Yw.z) / Yw.y;

    return float3(r, g, b);
}

float3 RGBToYC(float3 input, float3 Yw)
{
    //
    // Conversion to YCA works only if R, G and B are finite and non-negative.
    //
    float3 output;
    if (!std::isfinite(input[0]) || input[0] < 0.f)
        input[0] = 0;
    if (!std::isfinite(input[1]) || input[1] < 0.f)
        input[1] = 0;
    if (!std::isfinite(input[2]) || input[2] < 0.f)
        input[2] = 0;

    if (input[0] == input[1] && input[1] == input[2])
    {
        //
        // Special case -- R, G and B are equal. To avoid rounding
        // errors, we explicitly set the output luminance channel
        // to G, and the chroma channels to 0.
        //
        // The special cases here and in YCtoRGB() ensure that
        // converting black-and white images from RGB to YC and
        // back is lossless.
        //

        output[0] = 0;
        output[1] = input[1];
        output[2] = 0;
    }
    else
    {
        output[1] = dot(input, Yw);
        float Y   = output[1];
        input[0]  = (std::abs(input[0] - Y) < FLT_MAX * Y) ? (input[0] - Y) / Y : 0;
        input[2]  = (std::abs(input[2] - Y) < FLT_MAX * Y) ? (input[2] - Y) / Y : 0;
    }
    return output;
}

void sRGBToLinear(float *r, float *g, float *b)
{
    *r = SRGBToLinear(*r);
    *g = SRGBToLinear(*g);
    *b = SRGBToLinear(*b);
}
void LinearToSRGB(float *r, float *g, float *b)
{
    *r = LinearToSRGB(*r);
    *g = LinearToSRGB(*g);
    *b = LinearToSRGB(*b);
}

Color3 LinearToSRGB(const Color3 &c) { return Color3(LinearToSRGB(c.x), LinearToSRGB(c.y), LinearToSRGB(c.z)); }
Color4 LinearToSRGB(const Color4 &c) { return Color4(LinearToSRGB(c.xyz()), c.w); }
Color3 SRGBToLinear(const Color3 &c) { return Color3(SRGBToLinear(c.x), SRGBToLinear(c.y), SRGBToLinear(c.z)); }
Color4 SRGBToLinear(const Color4 &c) { return Color4(SRGBToLinear(c.xyz()), c.w); }

Color3 LinearToGamma(const Color3 &c, const Color3 &inv_gamma)
{
    return Color3(LinearToGamma(c.x, inv_gamma.x), LinearToGamma(c.y, inv_gamma.y), LinearToGamma(c.z, inv_gamma.z));
}
Color4 LinearToGamma(const Color4 &c, const Color3 &inv_gamma)
{
    return Color4(LinearToGamma(c.xyz(), inv_gamma), c.w);
}

void XYZToLinearSRGB(float *R, float *G, float *B, float X, float Y, float Z)
{
    *R = 3.240479f * X - 1.537150f * Y - 0.498535f * Z;
    *G = -0.969256f * X + 1.875992f * Y + 0.041556f * Z;
    *B = 0.055648f * X - 0.204043f * Y + 1.057311f * Z;
}

void LinearSRGBToXYZ(float *X, float *Y, float *Z, float R, float G, float B)
{
    *X = 0.412453f * R + 0.357580f * G + 0.180423f * B;
    *Y = 0.212671f * R + 0.715160f * G + 0.072169f * B;
    *Z = 0.019334f * R + 0.119193f * G + 0.950227f * B;
}

void xyYToXZ(float *X, float *Z, float x, float y, float Y)
{
    if (Y == 0.0f)
    {
        *X = 0.0f;
        *Z = 0.0f;
    }
    else
    {
        *X = x * Y;
        *Z = (1.0f - x - y) * Y / y;
    }
}

const vector<string> &colorSpaceNames()
{
    static const vector<string> names = {"Linear sRGB", "Linear sGray", "Linear Adobe RGB",
                                         "CIE XYZ",     "CIE L*a*b*",   "CIE L*u*v*",
                                         "CIE xyY",     "HSL",          "HSV"};
    return names;
}

uint8_t f32_to_byte(float v, int x, int y, bool sRGB, bool dither)
{
    return (uint8_t)clamp((sRGB ? LinearToSRGB(v) : v) * 255.0f + 0.5f + (dither ? tent_dither(x, y) : 0.f), 0.0f,
                          255.0f);
}
