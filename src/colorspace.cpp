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

// Reference whites used in the common color spaces below
static const Imath::V2f _wp_ACES{0.32168f, 0.33767f};
// static const Imath::V2f _wp_C{0.31006f, 0.31616f};
static const Imath::V2f _wp_D50{0.34567f, 0.35850f};
static const Imath::V2f _wp_D65{0.31271f, 0.32902f};
static const Imath::V2f _wp_E{0.33333f, 0.33333f};

// static const char _lin_ap0[]          = "lin_ap0";
// static const char _lin_ap1_acescg[]   = "lin_ap1/acescg";
// static const char _lin_adobergb[]     = "lin_adobergb";
// static const char _lin_cie1931xyz[]   = "lin_cie1931xyz";
// static const char _lin_displayp3[]    = "lin_displayp3";
// static const char _lin_prophotorgb[]  = "lin_prophotorgb";
// static const char _lin_rec2020_2100[] = "lin_rec2020/rec2100";
// static const char _lin_srgb_rec709[]  = "lin_srgb/rec709";
static const char _lin_ap0[]          = "ACES AP0";
static const char _lin_ap1_acescg[]   = "ACEScg AP1";
static const char _lin_adobergb[]     = "Adobe RGB";
static const char _lin_cie1931xyz[]   = "CIE 1931 XYZ";
static const char _lin_displayp3[]    = "Display P3";
static const char _lin_prophotorgb[]  = "ProPhoto RGB";
static const char _lin_rec2020_2100[] = "Rec2020/Rec2100";
static const char _lin_srgb_rec709[]  = "sRGB/Rec709";

const char *lin_ap0_gamut          = _lin_ap0;
const char *lin_ap1_acescg_gamut   = _lin_ap1_acescg;
const char *lin_adobergb_gamut     = _lin_adobergb;
const char *lin_cie1931xyz_gamut   = _lin_cie1931xyz;
const char *lin_displayp3_gamut    = _lin_displayp3;
const char *lin_prophotorgb_gamut  = _lin_prophotorgb;
const char *lin_rec2020_2100_gamut = _lin_rec2020_2100;
const char *lin_srgb_rec709_gamut  = _lin_srgb_rec709;

// clang-format off
static const char* _color_gammut_names[] = {
    _lin_ap0,
    _lin_ap1_acescg,
    _lin_adobergb,
    _lin_cie1931xyz,
    _lin_displayp3,
    _lin_prophotorgb,
    _lin_rec2020_2100,
    _lin_srgb_rec709,
    nullptr
};
// clang-format on

// data from:
//  https://en.wikipedia.org/wiki/Standard_illuminant
//  https://en.wikipedia.org/wiki/RGB_color_spaces
//  http://www.brucelindbloom.com/index.html?WorkingSpaceInfo.html
// Chromaticity data for common color spaces
static const std::map<const char *, Imf::Chromaticities> _chromaticities_map = {
    {_lin_ap0, {{0.73470f, 0.26530f}, {0.00000f, 1.00000f}, {0.00010f, -0.07700f}, _wp_ACES}},
    {_lin_ap1_acescg, {{0.713f, 0.293f}, {0.165f, 0.830f}, {0.128f, 0.044f}, _wp_ACES}},
    {_lin_adobergb, {{0.6400f, 0.3300f}, {0.2100f, 0.7100f}, {0.1500f, 0.0600f}, _wp_D65}},
    {_lin_displayp3, {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, _wp_D65}},
    {_lin_rec2020_2100, {{0.7080f, 0.2920f}, {0.1700f, 0.7970f}, {0.1310f, 0.0460f}, _wp_D65}},
    {_lin_cie1931xyz, {{1.f, 0.f}, {0.f, 1.f}, {0.f, 0.f}, _wp_E}},
    {_lin_prophotorgb, {{0.7347f, 0.2653f}, {0.1596f, 0.8404f}, {0.0366f, 0.0001f}, _wp_D50}},
    {_lin_srgb_rec709, {{0.6400f, 0.3300f}, {0.3000f, 0.6000f}, {0.1500f, 0.0600f}, _wp_D65}}};

static const std::map<const char *, const char *> _gamut_descriptions = {
    {_lin_ap0, "AP0 primaries, Academy Color Encoding System white point"},
    {_lin_ap1_acescg, "AP1 primaries, Academy Color Encoding System white point"},
    {_lin_adobergb, "Adobe RGB (1998) gamut, D65"},
    {_lin_displayp3, "Display-P3 gamut, D65"},
    {_lin_rec2020_2100, "Rec2020/Rec2100 gamut, D65"},
    {_lin_cie1931xyz, "CIE (1931) XYZ primaries, E white point"},
    {_lin_prophotorgb, "ProPhoto RGB gamut, D50"},
    {_lin_srgb_rec709, "sRGB/Rec709 gamut, D65"}};

const char **color_gamut_names() { return _color_gammut_names; }
const char  *color_gamut_description(const char *name) { return _gamut_descriptions.at(name); }

const std::map<const char *, Imf::Chromaticities> &color_gamuts() { return _chromaticities_map; }

const Imf::Chromaticities &gamut_chromaticities(const char *name) { return _chromaticities_map.at(name); }

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
