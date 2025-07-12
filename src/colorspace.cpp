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
static const float2 _wp_ACES{0.32168f, 0.33767f};
static const float2 _wp_C{0.31006f, 0.31616f};
static const float2 _wp_DCI{0.314f, 0.351f};
static const float2 _wp_D50{0.34567f, 0.35850f};
static const float2 _wp_D65{0.31271f, 0.32902f};
static const float2 _wp_E{0.33333f, 0.33333f};

static const char _lin_ap0[]         = "ACES AP0";
static const char _lin_ap1_acescg[]  = "ACEScg AP1";
static const char _lin_adobergb[]    = "Adobe RGB";
static const char _lin_cie1931xyz[]  = "CIE 1931 XYZ";
static const char _lin_displayp3[]   = "Display P3";
static const char _lin_prophotorgb[] = "ProPhoto RGB";
static const char _lin_bt2020_2100[] = "BT.2020/BT.2100";
static const char _lin_srgb_bt709[]  = "sRGB/BT.709";
static const char _lin_cicp_01[]     = "CICP 01";
static const char _lin_cicp_04[]     = "CICP 04";
static const char _lin_cicp_05[]     = "CICP 05";
static const char _lin_cicp_06[]     = "CICP 06";
static const char _lin_cicp_07[]     = "CICP 07";
static const char _lin_cicp_08[]     = "CICP 08";
static const char _lin_cicp_09[]     = "CICP 09";
static const char _lin_cicp_10[]     = "CICP 10";
static const char _lin_cicp_11[]     = "CICP 11";
static const char _lin_cicp_12[]     = "CICP 12";
static const char _lin_cicp_22[]     = "CICP 22";

const char *lin_ap0_gamut         = _lin_ap0;
const char *lin_ap1_acescg_gamut  = _lin_ap1_acescg;
const char *lin_adobergb_gamut    = _lin_adobergb;
const char *lin_cie1931xyz_gamut  = _lin_cie1931xyz;
const char *lin_displayp3_gamut   = _lin_displayp3;
const char *lin_prophotorgb_gamut = _lin_prophotorgb;
const char *lin_bt2020_2100_gamut = _lin_bt2020_2100;
const char *lin_srgb_bt709_gamut  = _lin_srgb_bt709;
const char *lin_cicp_01_gamut     = _lin_cicp_01;
const char *lin_cicp_04_gamut     = _lin_cicp_04;
const char *lin_cicp_05_gamut     = _lin_cicp_05;
const char *lin_cicp_06_gamut     = _lin_cicp_06;
const char *lin_cicp_07_gamut     = _lin_cicp_07;
const char *lin_cicp_08_gamut     = _lin_cicp_08;
const char *lin_cicp_09_gamut     = _lin_cicp_09;
const char *lin_cicp_10_gamut     = _lin_cicp_10;
const char *lin_cicp_11_gamut     = _lin_cicp_11;
const char *lin_cicp_12_gamut     = _lin_cicp_12;
const char *lin_cicp_22_gamut     = _lin_cicp_22;

// clang-format off
static const char* _color_gammut_names[] = {
    _lin_ap0,
    _lin_ap1_acescg,
    _lin_adobergb,
    _lin_cie1931xyz,
    _lin_displayp3,
    _lin_prophotorgb,
    _lin_bt2020_2100,
    _lin_srgb_bt709,
    _lin_cicp_01,
    _lin_cicp_04,
    _lin_cicp_05,
    _lin_cicp_06,
    _lin_cicp_07,
    _lin_cicp_08,
    _lin_cicp_09,
    _lin_cicp_10,
    _lin_cicp_11,
    _lin_cicp_12,
    _lin_cicp_22,
    nullptr
};
// clang-format on

const char *linear_tf        = "Linear";
const char *gamma_tf         = "Gamma";
const char *srgb_tf          = "sRGB IEC61966-2.1";
const char *itu_tf           = "BT.709/2020";
const char *pq_tf            = "BT.2100 PQ";
const char *hlg_tf           = "BT.2100 HLG";
const char *st240_tf         = "SMPTE ST 240";
const char *log100_tf        = "Log100";
const char *log100_sqrt10_tf = "Log100 Sqrt10";
const char *iec61966_2_4_tf  = "IEC 61966-2-4";
const char *dci_p3_tf        = "DCI-P3";

// clang-format off
static const char* _tf_names[] = {
    linear_tf,
    gamma_tf,
    srgb_tf,
    itu_tf,
    pq_tf,
    hlg_tf,
    nullptr
};
// clang-format on

// data from:
//  https://en.wikipedia.org/wiki/Standard_illuminant
//  https://en.wikipedia.org/wiki/RGB_color_spaces
//  http://www.brucelindbloom.com/index.html?WorkingSpaceInfo.html
//  ITU-T H.273: https://www.itu.int/rec/T-REC-H.273-202407-I/en
// Chromaticity data for common color spaces
static const std::map<const char *, Chromaticities> _chromaticities_map = {
    {_lin_ap0, {{0.73470f, 0.26530f}, {0.00000f, 1.00000f}, {0.00010f, -0.07700f}, _wp_ACES}},
    {_lin_ap1_acescg, {{0.713f, 0.293f}, {0.165f, 0.830f}, {0.128f, 0.044f}, _wp_ACES}},
    {_lin_adobergb, {{0.6400f, 0.3300f}, {0.2100f, 0.7100f}, {0.1500f, 0.0600f}, _wp_D65}},
    {_lin_displayp3, {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, _wp_D65}},
    {_lin_bt2020_2100, {{0.7080f, 0.2920f}, {0.1700f, 0.7970f}, {0.1310f, 0.0460f}, _wp_D65}},
    {_lin_cie1931xyz, {{1.f, 0.f}, {0.f, 1.f}, {0.f, 0.f}, _wp_E}},
    {_lin_prophotorgb, {{0.7347f, 0.2653f}, {0.1596f, 0.8404f}, {0.0366f, 0.0001f}, _wp_D50}},
    {_lin_srgb_bt709, {{0.6400f, 0.3300f}, {0.3000f, 0.6000f}, {0.1500f, 0.0600f}, _wp_D65}},
    {_lin_cicp_01, {{0.6400f, 0.3300f}, {0.3000f, 0.6000f}, {0.1500f, 0.0600f}, _wp_D65}}, // BT.709, sRGB
    {_lin_cicp_04, {{0.6700f, 0.3300f}, {0.2100f, 0.7100f}, {0.1400f, 0.0800f}, _wp_C}},   // BT.470 System M
    {_lin_cicp_05, {{0.6400f, 0.3300f}, {0.2900f, 0.6000f}, {0.1500f, 0.0600f}, _wp_D65}}, // BT.470 System BG
    {_lin_cicp_06, {{0.6300f, 0.3400f}, {0.3100f, 0.5950f}, {0.1550f, 0.0700f}, _wp_D65}}, // SMPTE 170M
    {_lin_cicp_07, {{0.6300f, 0.3400f}, {0.3100f, 0.5950f}, {0.1550f, 0.0700f}, _wp_D65}}, // SMPTE 240M
    {_lin_cicp_08, {{0.6810f, 0.3190f}, {0.2430f, 0.6920f}, {0.1450f, 0.0490f}, _wp_C}},   // Generic Film
    {_lin_cicp_09, {{0.7080f, 0.2920f}, {0.1700f, 0.7970f}, {0.1310f, 0.0460f}, _wp_D65}}, // BT.2020
    {_lin_cicp_10, {{0.7350f, 0.2650f}, {0.2740f, 0.7170f}, {0.1670f, 0.0090f}, _wp_E}},   // SMPTE 428 (XYZ)
    {_lin_cicp_11, {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, _wp_DCI}}, // SMPTE 431-2 (DCI P3)
    {_lin_cicp_12, {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, _wp_D65}}, // SMPTE 432-1 (P3 D65)
    {_lin_cicp_22, {{0.6300f, 0.3400f}, {0.2950f, 0.6050f}, {0.1550f, 0.0770f}, _wp_D65}}};

static const std::map<const char *, const char *> _gamut_descriptions = {
    {_lin_ap0, "AP0 primaries, Academy Color Encoding System white point"},
    {_lin_ap1_acescg, "AP1 primaries, Academy Color Encoding System white point"},
    {_lin_adobergb, "Adobe RGB (1998) gamut, D65"},
    {_lin_displayp3, "Display-P3 gamut, D65"},
    {_lin_bt2020_2100, "BT.2020/BT.2100 gamut, D65"},
    {_lin_cie1931xyz, "CIE (1931) XYZ primaries, E white point"},
    {_lin_prophotorgb, "ProPhoto RGB gamut, D50"},
    {_lin_srgb_bt709, "sRGB/Rec709 gamut, D65"},
    {_lin_cicp_01, "CICP 01 BT.709/sRGB gamut, D65"},
    {_lin_cicp_04, "CICP 04 BT.470 System M, NTSC gamut, C white point"},
    {_lin_cicp_05, "CICP 05 BT.470 System BG, D65"},
    {_lin_cicp_06, "CICP 06 SMPTE 170M, D65"},
    {_lin_cicp_07, "CICP 07 SMPTE 240M, D65"},
    {_lin_cicp_08, "CICP 08 Generic Film, C white point"},
    {_lin_cicp_09, "CICP 09 BT.2020, D65"},
    {_lin_cicp_10, "CICP 10 SMPTE 428 (XYZ), E white point"},
    {_lin_cicp_11, "CICP 11 SMPTE 431-2 (DCI P3), DCI white point"},
    {_lin_cicp_12, "CICP 12 SMPTE 432-1 (P3 D65), D65"},
    {_lin_cicp_22, "CICP 22 EBU Tech. 3213-E, D65"}};

const char **color_gamut_names() { return _color_gammut_names; }
const char  *color_gamut_description(const char *name) { return _gamut_descriptions.at(name); }

const char **transfer_function_names() { return _tf_names; }

const std::map<const char *, Chromaticities> &color_gamuts() { return _chromaticities_map; }

const Chromaticities &gamut_chromaticities(const char *name) { return _chromaticities_map.at(name); }

float4x4 RGB_to_XYZ(const Chromaticities &chroma, float Y)
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

    float4x4 M{la::identity};

    M[0][0] = Sr * chroma.red.x;
    M[0][1] = Sr * chroma.red.y;
    M[0][2] = Sr * (1 - chroma.red.x - chroma.red.y);

    M[1][0] = Sg * chroma.green.x;
    M[1][1] = Sg * chroma.green.y;
    M[1][2] = Sg * (1 - chroma.green.x - chroma.green.y);

    M[2][0] = Sb * chroma.blue.x;
    M[2][1] = Sb * chroma.blue.y;
    M[2][2] = Sb * (1 - chroma.blue.x - chroma.blue.y);

    M[3][3] = 1.f;

    return M;
}

bool color_conversion_matrix(float3x3 &M, const Chromaticities &src, const Chromaticities &dst, int CAT_method)
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

        auto m1 = RGB_to_XYZ(src, 1);
        // extract the upper left 3x3 of m1 as an M33f
        float3x3 src_to_XYZ(m1.x.xyz(), m1.y.xyz(), m1.z.xyz());

        m1 = XYZ_to_RGB(dst, 1);
        // extract the upper left 3x3 of m1 as an M33f
        float3x3 XYZ_to_dst(m1.x.xyz(), m1.y.xyz(), m1.z.xyz());

        M = mul(XYZ_to_dst, CAT, src_to_XYZ);

        return true;
    }
    catch (...)
    {
        return false;
    }
}

float3 YCToRGB(float3 input, float3 Yw)
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

Color3 LinearToSRGB(const Color3 &c) { return la::apply(LinearToSRGB<float>, c); }
Color4 LinearToSRGB(const Color4 &c) { return {LinearToSRGB(c.xyz()), c.w}; }
Color3 SRGBToLinear(const Color3 &c) { return la::apply(SRGBToLinear<float>, c); }
Color4 SRGBToLinear(const Color4 &c) { return {SRGBToLinear(c.xyz()), c.w}; }

Color3 LinearToGamma(const Color3 &c, const Color3 &inv_gamma) { return la::apply(LinearToGamma<float>, c, inv_gamma); }
Color4 LinearToGamma(const Color4 &c, const Color3 &inv_gamma) { return {LinearToGamma(c.xyz(), inv_gamma), c.w}; }

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
