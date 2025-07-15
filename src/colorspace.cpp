//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "Imath_to_linalg.h"
#include "common.h"
#include "scheduler.h"
#include <cmath>
#include <float.h>

using namespace std;

// Reference whites used in the common color spaces below
static float2 s_white_point_values[] = {
    {0.32168f, 0.33767f}, // ACES: Academy Color Encoding System, ~6000k
    {0.34567f, 0.35850f}, // D50:	horizon light, ICC profile PCS
    {0.33242f, 0.34743f}, // D55:	mid-morning / mid-afternoon daylight
    {0.31271f, 0.32902f}, // D65:	noon daylight: television, sRGB color space
    {0.29902f, 0.31485f}, // D75:	North sky daylight
    {0.28315f, 0.29711f}, // D93:	high-efficiency blue phosphor monitors, BT.2035
    {0.3140f, 0.3510f},   // DCI: ~6300 K
    {0.31310f, 0.33727f}, // F1:	daylight fluorescent
    {0.37208f, 0.37529f}, // F2:	cool white fluorescent
    {0.40910f, 0.39430f}, // F3:	white fluorescent
    {0.44018f, 0.40329f}, // F4:	warm white fluorescent
    {0.31379f, 0.34531f}, // F5:	daylight fluorescent
    {0.37790f, 0.38835f}, // F6:	light white fluorescent
    {0.31292f, 0.32933f}, // F7:	D65 simulator, daylight simulator
    {0.34588f, 0.35875f}, // F8:	D50 simulator, Sylvania F40 Design 50
    {0.37417f, 0.37281f}, // F9:	cool white deluxe fluorescent
    {0.34609f, 0.35986f}, // F10:	Philips TL85, Ultralume 50
    {0.38052f, 0.37713f}, // F11:	Philips TL84, Ultralume 40
    {0.43695f, 0.40441f}, // F12:	Philips TL83, Ultralume 30
    {0.44757f, 0.40745f}, // A:	incandescent / tungsten
    {0.34842f, 0.35161f}, // B:	obsolete, direct sunlight at noon
    {0.31006f, 0.31616f}, // C:	obsolete, average / North sky daylight
    {0.33333f, 0.33333f}, // E:	equal energy
    {0.31271f, 0.32902f}, // Unspecified = D65
    {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()}};

static const char *s_white_point_names[] = {"ACES",             // ACES: Academy Color Encoding System, ~6000k
                                            "D50",              // D50: horizon light, ICC profile PCS
                                            "D55",              // D55: mid-morning / mid-afternoon daylight
                                            "D65",              // D65: noon daylight: television, sRGB color space
                                            "D75",              // D75: North sky daylight
                                            "D93",              // D93: high-efficiency blue phosphor monitors, BT.2035
                                            "DCI",              // DCI: ~6300 K
                                            "F1",               // F1: daylight fluorescent
                                            "F2",               // F2: cool white fluorescent
                                            "F3",               // F3: white fluorescent
                                            "F4",               // F4: warm white fluorescent
                                            "F5",               // F5: daylight fluorescent
                                            "F6",               // F6: light white fluorescent
                                            "F7",               // F7: D65 simulator, daylight simulator
                                            "F8",               // F8: D50 simulator, Sylvania F40 Design 50
                                            "F9",               // F9: cool white deluxe fluorescent
                                            "F10",              // F10: Philips TL85, Ultralume 50
                                            "F11",              // F11: Philips TL84, Ultralume 40
                                            "F12",              // F12: Philips TL83, Ultralume 30
                                            "Std Illuminant A", // A: incandescent / tungsten
                                            "Std Illuminant B", // B: obsolete, direct sunlight at noon
                                            "Std Illuminant C", // C: obsolete, average / North sky daylight
                                            "Std Illuminant E", // E: equal energy
                                            "Unspecified (Assuming D65)", // Unspecified = D65
                                            "Custom",                     // Custom/NaN
                                            nullptr};

// Static array of chromaticities for each ColorGamut value
static const Chromaticities s_gamut_chromaticities[] = {
    {{0.6400f, 0.3300f}, {0.3000f, 0.6000f}, {0.1500f, 0.0600f}, s_white_point_values[WhitePoint_D65]},
    {{0.6700f, 0.3300f}, {0.2100f, 0.7100f}, {0.1400f, 0.0800f}, s_white_point_values[WhitePoint_C]},
    {{0.6400f, 0.3300f}, {0.2900f, 0.6000f}, {0.1500f, 0.0600f}, s_white_point_values[WhitePoint_D65]},
    {{0.6300f, 0.3400f}, {0.3100f, 0.5950f}, {0.1550f, 0.0700f}, s_white_point_values[WhitePoint_D65]},
    {{0.6300f, 0.3400f}, {0.3100f, 0.5950f}, {0.1550f, 0.0700f}, s_white_point_values[WhitePoint_D65]},
    {{0.6810f, 0.3190f}, {0.2430f, 0.6920f}, {0.1450f, 0.0490f}, s_white_point_values[WhitePoint_C]},
    {{0.7080f, 0.2920f}, {0.1700f, 0.7970f}, {0.1310f, 0.0460f}, s_white_point_values[WhitePoint_D65]},
    {{0.7350f, 0.2650f}, {0.2740f, 0.7170f}, {0.1670f, 0.0090f}, s_white_point_values[WhitePoint_E]},
    {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, s_white_point_values[WhitePoint_DCI]},
    {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, s_white_point_values[WhitePoint_D65]},
    {{0.6300f, 0.3400f}, {0.2950f, 0.6050f}, {0.1550f, 0.0770f}, s_white_point_values[WhitePoint_D65]},
    {{0.73470f, 0.26530f}, {0.00000f, 1.00000f}, {0.00010f, -0.07700f}, s_white_point_values[WhitePoint_ACES]},
    {{0.713f, 0.293f}, {0.165f, 0.830f}, {0.128f, 0.044f}, s_white_point_values[WhitePoint_ACES]},
    {{0.6400f, 0.3300f}, {0.2100f, 0.7100f}, {0.1500f, 0.0600f}, s_white_point_values[WhitePoint_D65]},
    {{0.7347f, 0.2653f}, {0.1596f, 0.8404f}, {0.0366f, 0.0001f}, s_white_point_values[WhitePoint_D50]},
    {{1.f, 0.f}, {0.f, 1.f}, {0.f, 0.f}, s_white_point_values[WhitePoint_E]},
    {{0.6400f, 0.3300f}, {0.3000f, 0.6000f}, {0.1500f, 0.0600f}, s_white_point_values[WhitePoint_D65]}
    // ColorGamut_Custom (should not be used, throw below)
};

static const char *s_gamut_names[] = {"sRGB/BT.709",
                                      "BT.470 M/NTSC",
                                      "BT.470 BG/PAL/SECAM",
                                      "SMPTE ST 170",
                                      "SMPTE ST 240",
                                      "Generic film",
                                      "BT.2020/BT.2100",
                                      "SMPTE ST 428-1",
                                      "DCI P3/SMPTE RP 431",
                                      "Display P3/SMPTE EG 432",
                                      "CICP 22",
                                      "ACES AP0",
                                      "ACEScg AP1",
                                      "Adobe RGB",
                                      "ProPhoto RGB",
                                      "CIE 1931 XYZ",
                                      "Unspecified (Assuming sRGB/BT.709)",
                                      "Custom",
                                      nullptr};

static const char *s_transfer_function_names[] = {"Unknown (Assuming sRGB)", // TransferFunction_Unknown
                                                  "Linear",                  // TransferFunction_Linear
                                                  "Gamma",                   // TransferFunction_Gamma
                                                  "sRGB IEC61966-2.1",       // TransferFunction_sRGB
                                                  "BT.709/2020",             // TransferFunction_ITU
                                                  "BT.2100 PQ",              // TransferFunction_BT2100_PQ
                                                  "BT.2100 HLG",             // TransferFunction_BT2100_HLG
                                                  "SMPTE ST 240",            // TransferFunction_ST240
                                                  "Log100",                  // TransferFunction_Log100
                                                  "Log100 Sqrt10",           // TransferFunction_Log100_Sqrt10
                                                  "IEC 61966-2-4",           // TransferFunction_IEC61966_2_4
                                                  "DCI-P3",                  // TransferFunction_DCI_P3
                                                  nullptr};

float2 white_point(WhitePoint wp)
{
    if (wp >= WhitePoint_Custom)
        return s_white_point_values[WhitePoint_Custom];
    return s_white_point_values[wp];
}

WhitePoint named_white_point(float2 wp)
{
    for (WhitePoint_ i = WhitePoint_FirstNamed; i <= WhitePoint_LastNamed; ++i)
    {
        if (approx_equal(wp, s_white_point_values[i]))
            return static_cast<WhitePoint>(i);
    }
    // Return custom for unrecognized
    return WhitePoint_Custom;
}

const char *white_point_name(WhitePoint wp) { return s_white_point_names[wp]; }

const char **white_point_names() { return s_white_point_names; }

const char *color_gamut_name(const ColorGamut primaries)
{
    if (primaries >= ColorGamut_Custom)
        return s_gamut_names[ColorGamut_Custom];
    return s_gamut_names[primaries];
}

const char **color_gamut_names() { return s_gamut_names; }

Chromaticities gamut_chromaticities(ColorGamut primaries)
{
    if (primaries < 0 || primaries >= ColorGamut_Custom)
        throw std::invalid_argument("Unrecognized ColorGamut value");
    return s_gamut_chromaticities[primaries];
}

Chromaticities chromaticities_from_cicp(int cicp)
{
    switch (cicp)
    {
    case 1: return s_gamut_chromaticities[ColorGamut_sRGB_BT709];
    case 2: return s_gamut_chromaticities[ColorGamut_Unspecified];
    case 4: return s_gamut_chromaticities[ColorGamut_BT470M];
    case 5: return s_gamut_chromaticities[ColorGamut_BT470BG];
    case 6: return s_gamut_chromaticities[ColorGamut_SMPTE170M];
    case 7: return s_gamut_chromaticities[ColorGamut_SMPTE240M];
    case 8: return s_gamut_chromaticities[ColorGamut_Film];
    case 9: return s_gamut_chromaticities[ColorGamut_BT2020_2100];
    case 10: return s_gamut_chromaticities[ColorGamut_SMPTE428];
    case 11: return s_gamut_chromaticities[ColorGamut_DCI_P3_SMPTE431];
    case 12: return s_gamut_chromaticities[ColorGamut_Display_P3_SMPTE432];
    case 22: return s_gamut_chromaticities[ColorGamut_CICP_22];
    default: throw std::invalid_argument("Unrecognized or unsupported CICP value for chromaticities");
    }
}

ColorGamut named_color_gamut(const Chromaticities &chr)
{
    for (ColorGamut_ i = ColorGamut_FirstNamed; i <= ColorGamut_LastNamed; ++i)
    {
        if (approx_equal(chr, gamut_chromaticities(static_cast<ColorGamut>(i))))
            return static_cast<ColorGamut>(i);
    }
    // Return custom for unrecognized
    return ColorGamut_Custom;
}

string transfer_function_name(TransferFunction tf, float gamma)
{
    if (tf == TransferFunction_Gamma)
        return fmt::format("{} (={})", s_transfer_function_names[TransferFunction_Gamma], float(1.0 / gamma));
    else if (tf < TransferFunction_Unknown || tf >= TransferFunction_Count)
        return s_transfer_function_names[TransferFunction_Unknown];
    else
        return s_transfer_function_names[tf];
}

TransferFunction transfer_function_from_cicp(int cicp, float *gamma)
{
    switch (cicp)
    {
    case 1: [[fallthrough]];
    case 6: [[fallthrough]];
    case 12: [[fallthrough]];
    case 14: [[fallthrough]];
    case 15: return TransferFunction_ITU;
    case 4:
        if (gamma)
            *gamma = 2.2f;
        return TransferFunction_Gamma;
    case 5:
        if (gamma)
            *gamma = 2.8f;
        return TransferFunction_Gamma;
    case 7: return TransferFunction_ST240;
    case 8: return TransferFunction_Linear;
    case 9: return TransferFunction_Log100;
    case 10: return TransferFunction_Log100_Sqrt10;
    case 11: return TransferFunction_IEC61966_2_4;
    case 13: return TransferFunction_sRGB;
    case 16: return TransferFunction_BT2100_PQ;
    case 17: return TransferFunction_DCI_P3;
    case 18: return TransferFunction_BT2100_HLG;
    default: return TransferFunction_Unknown;
    }
}

float3x3 RGB_to_XYZ(const Chromaticities &chroma, float Y)
{
    // Adapted from ImfChromaticities.cpp
    //

    //
    // For an explanation of how the color conversion matrix is derived,
    // see Roy Hall, "Illumination and Color in Computer Generated Imagery",
    // Springer-Verlag, 1989, chapter 3, "Perceptual Response"; and
    // Charles A. Poynton, "A Technical Introduction to Digital Video",
    // John Wiley & Sons, 1996, chapter 7, "Color science for video".
    //

    //
    // X and Z values of RGB value (1, 1, 1), or "white"
    //

    // prevent a division that rounds to zero
    if (std::abs(chroma.white.y) <= 1.f && std::abs(chroma.white.x * Y) >= std::abs(chroma.white.y) * FLT_MAX)
    {
        throw std::invalid_argument("Bad chromaticities: white.y cannot be zero");
    }

    float X = chroma.white.x * Y / chroma.white.y;
    float Z = (1 - chroma.white.x - chroma.white.y) * Y / chroma.white.y;

    //
    // Scale factors for matrix rows, compute numerators and common denominator
    //

    float d = chroma.red.x * (chroma.blue.y - chroma.green.y) + chroma.blue.x * (chroma.green.y - chroma.red.y) +
              chroma.green.x * (chroma.red.y - chroma.blue.y);

    float SrN =
        (X * (chroma.blue.y - chroma.green.y) - chroma.green.x * (Y * (chroma.blue.y - 1) + chroma.blue.y * (X + Z)) +
         chroma.blue.x * (Y * (chroma.green.y - 1) + chroma.green.y * (X + Z)));

    float SgN =
        (X * (chroma.red.y - chroma.blue.y) + chroma.red.x * (Y * (chroma.blue.y - 1) + chroma.blue.y * (X + Z)) -
         chroma.blue.x * (Y * (chroma.red.y - 1) + chroma.red.y * (X + Z)));

    float SbN =
        (X * (chroma.green.y - chroma.red.y) - chroma.red.x * (Y * (chroma.green.y - 1) + chroma.green.y * (X + Z)) +
         chroma.green.x * (Y * (chroma.red.y - 1) + chroma.red.y * (X + Z)));

    if (std::abs(d) < 1.f && (std::abs(SrN) >= std::abs(d) * FLT_MAX || std::abs(SgN) >= std::abs(d) * FLT_MAX ||
                              std::abs(SbN) >= std::abs(d) * FLT_MAX))
    {
        // cannot generate matrix if all RGB primaries have the same y value
        // or if they all have the an x value of zero
        // in both cases, the primaries are colinear, which makes them unusable
        throw std::invalid_argument("Bad chromaticities: RGBtoXYZ matrix is degenerate");
    }

    float Sr = SrN / d;
    float Sg = SgN / d;
    float Sb = SbN / d;

    //
    // Assemble the matrix
    //

    float3x3 M{la::identity};

    M[0][0] = Sr * chroma.red.x;
    M[0][1] = Sr * chroma.red.y;
    M[0][2] = Sr * (1 - chroma.red.x - chroma.red.y);

    M[1][0] = Sg * chroma.green.x;
    M[1][1] = Sg * chroma.green.y;
    M[1][2] = Sg * (1 - chroma.green.x - chroma.green.y);

    M[2][0] = Sb * chroma.blue.x;
    M[2][1] = Sb * chroma.blue.y;
    M[2][2] = Sb * (1 - chroma.blue.x - chroma.blue.y);

    // M[3][3] = 1.f;

    return M;
}

bool color_conversion_matrix(float3x3 &M, const Chromaticities &src, const Chromaticities &dst,
                             AdaptationMethod CAT_method)
{
    try
    {
        if (src == dst)
        {
            // The file already contains data in the target colorspace.
            // color conversion is not necessary.
            M = float3x3{la::identity};
            return false;
        }

        //
        // Create a matrix that transforms colors from the
        // RGB space of the input file into the target space
        // using a color adaptation transform to move the
        // white point.
        //

        float3x3 CAT{la::identity}; // chromatic adaptation matrix
        if (CAT_method > 0 && CAT_method <= 3 && src.white != dst.white)
        {
            // the cone primary response matrices (and their inverses) for 3 different methods
            static const float3x3 CPM[3] = {{{1.f, 0.f, 0.f}, // XYZ scaling
                                             {0.f, 1.f, 0.f},
                                             {0.f, 0.f, 1.f}},
                                            {{0.895100f, -0.750200f, 0.038900f}, // Bradford
                                             {0.266400f, 1.713500f, -0.068500f},
                                             {-0.161400f, 0.036700f, 1.029600f}},
                                            {{0.4002400f, -0.2263000f, 0.0000000f}, // Von Kries
                                             {0.7076000f, 1.1653200f, 0.0000000f},
                                             {-0.0808100f, 0.0457000f, 0.9182200f}}};
            static const float3x3 invCPM[3]{{{1.f, 0.f, 0.f}, //
                                             {0.f, 1.f, 0.f}, //
                                             {0.f, 0.f, 1.f}},
                                            {{0.986993f, 0.432305f, -0.008529f}, //
                                             {-0.147054f, 0.518360f, 0.040043f}, //
                                             {0.159963f, 0.049291f, 0.968487f}},
                                            {{1.8599364f, 0.3611914f, 0.0000000f},  //
                                             {-1.1293816f, 0.6388125f, 0.0000000f}, //
                                             {0.2198974f, -0.0000064f, 1.0890636f}}};
            //
            // Convert the white points of the two RGB spaces to XYZ
            //

            float  fx = src.white.x;
            float  fy = src.white.y;
            float3 src_neutral_XYZ(fx / fy, 1, (1 - fx - fy) / fy);

            float  ax = dst.white.x;
            float  ay = dst.white.y;
            float3 dst_neutral_XYZ(ax / ay, 1, (1 - ax - ay) / ay);

            //
            // Compute the CAT
            //

            float3 ratio(mul(CPM[CAT_method - 1], dst_neutral_XYZ) / mul(CPM[CAT_method - 1], src_neutral_XYZ));

            float3x3 ratio_mat({ratio[0], 0.f, 0.f}, {0.f, ratio[1], 0.f}, {0.f, 0.f, ratio[2]});

            CAT = mul(invCPM[CAT_method - 1], ratio_mat, CPM[CAT_method - 1]);
        }

        //
        // Build a combined file-RGB-to-target-RGB conversion matrix
        //

        M = mul(XYZ_to_RGB(dst, 1), CAT, RGB_to_XYZ(src, 1));

        return true;
    }
    catch (...)
    {
        return false;
    }
}

Chromaticities primaries_from_matrix(const float3x3 &rgb_to_XYZ)
{
    Chromaticities result;
    // Multiplying the matrix by [1,0,0], [0,1,0], or [0,0,1] gives the XYZ values of the primaries.
    // Hence, the columns of the matrix are XYZ values of the primaries.
    // Divide each by its sum to get corresponding chromaticities.
    result.red   = rgb_to_XYZ.x.xy() / sum(rgb_to_XYZ.x);
    result.green = rgb_to_XYZ.y.xy() / sum(rgb_to_XYZ.y);
    result.blue  = rgb_to_XYZ.z.xy() / sum(rgb_to_XYZ.z);

    // Multiplying the matrix by full-intensity for each primary [1,1,1] gives us XYZ of white.
    // Hence, the sum of the columns is the XYZ of white.
    // Divide that by the sum of its components to get its chromaticity;
    float3 wpXYZ = rgb_to_XYZ.x + rgb_to_XYZ.y + rgb_to_XYZ.z;
    result.white = wpXYZ.xy() / sum(wpXYZ);
    return result;
}

float3 YC_to_RGB(float3 input, float3 Yw)
{
    if (input[0] == 0.f && input[2] == 0.f)
        //
        // Special case -- both chroma channels are 0.  To avoid
        // rounding errors, we explicitly set the output R, G and B
        // channels equal to the input luminance.
        //
        return float3(input[1], input[1], input[1]);

    float Y = input[1];
    float r = (input[0] + 1.f) * input[1];
    float b = (input[2] + 1.f) * input[1];
    float g = (Y - r * Yw.x - b * Yw.z) / Yw.y;

    return float3(r, g, b);
}

float3 RGB_to_YC(float3 input, float3 Yw)
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

const float3x3 &sRGB_to_XYZ()
{
    static const float3x3 M = RGB_to_XYZ(Chromaticities{}, 1.f);
    return M;
}

const float3x3 &XYZ_to_sRGB()
{
    static const float3x3 M = XYZ_to_RGB(Chromaticities{}, 1.f);
    return M;
}

const float3 &sRGB_Yw()
{
    static const float3 Yw = computeYw(Chromaticities{});
    return Yw;
}

Color3 linear_to_sRGB(const Color3 &c) { return la::apply(linear_to_sRGB<float>, c); }
Color4 linear_to_sRGB(const Color4 &c) { return {linear_to_sRGB(c.xyz()), c.w}; }
Color3 sRGB_to_linear(const Color3 &c) { return la::apply(sRGB_to_linear<float>, c); }
Color4 sRGB_to_linear(const Color4 &c) { return {sRGB_to_linear(c.xyz()), c.w}; }

Color3 linear_to_gamma(const Color3 &c, const Color3 &inv_gamma)
{
    return la::apply(linear_to_gamma<float>, c, inv_gamma);
}
Color4 linear_to_gamma(const Color4 &c, const Color3 &inv_gamma) { return {linear_to_gamma(c.xyz(), inv_gamma), c.w}; }

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

void to_linear(float *pixels, int3 size, TransferFunction tf, float gamma)
{
    if (tf == TransferFunction_BT2100_HLG && (size.z == 3 || size.z == 4))
    {
        // HLG needs to operate on all three channels at once
        if (size.z == 3)
            parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                         [rgb = reinterpret_cast<float3 *>(pixels)](int start, int end, int, int)
                         {
                             for (int i = start; i < end; ++i) rgb[i] = EOTF_BT2100_HLG(rgb[i]) / 255.f;
                         });
        else // size.z == 4
            // don't modify the alpha channel
            parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                         [rgb = reinterpret_cast<float4 *>(pixels)](int start, int end, int, int)
                         {
                             for (int i = start; i < end; ++i) rgb[i].xyz() = EOTF_BT2100_HLG(rgb[i].xyz()) / 255.f;
                         });
    }
    else
    {
        // assume this means we have an alpha channel, which we pass through without modification
        int num_color_channels = (size.z == 2 || size.z == 4) ? size.z - 1 : size.z;
        // other transfer functions apply to each channel independently
        parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024 / size.z),
                     [&pixels, tf, gamma, size, num_color_channels](int start, int end, int, int)
                     {
                         for (int i = start; i < end; ++i)
                             for (int c = 0; c < num_color_channels; ++c)
                                 pixels[i * size.z + c] = to_linear(pixels[i * size.z + c], tf, gamma);
                     });
    }
}
