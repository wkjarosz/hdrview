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
static const float2 wp_ACES{0.32168f, 0.33767f};
static const float2 wp_A{0.44758f, 0.40745f};
static const float2 wp_B{0.34842f, 0.35161f};
static const float2 wp_C{0.31006f, 0.31616f};
static const float2 wp_E{0.33333f, 0.33333f};
static const float2 wp_D50{0.34567f, 0.35850f};
static const float2 wp_D55{0.33242f, 0.34743f};
static const float2 wp_D65{0.31271f, 0.32902f};
static const float2 wp_D75{0.29902f, 0.31485f};
static const float2 wp_DCI{0.3140f, 0.3510f};

static const char *Unspecified_wp = "Unspecified (Assuming D65)";
static const char *ACES_wp        = "ACES";
static const char *A_wp           = "Std Illuminant A";
static const char *B_wp           = "Std Illuminant B";
static const char *C_wp           = "Std Illuminant C";
static const char *E_wp           = "Std Illuminant E";
static const char *D50_wp         = "D50";
static const char *D55_wp         = "D55";
static const char *D65_wp         = "D65";
static const char *D75_wp         = "D75";
static const char *DCI_wp         = "DCI";
static const char *Custom_wp      = "Custom";

static const char *unspecified_gamut         = "Unspecified (Assuming sRGB/BT.709)";
static const char *srgb_bt709_gamut          = "sRGB/BT.709";
static const char *bt470m_gamut              = "BT.470 M";
static const char *bt470bg_gamut             = "BT.470 BG";
static const char *smpte170_gamut            = "SMPTE ST 170";
static const char *smpte240_gamut            = "SMPTE ST 240";
static const char *generic_film_gamut        = "Generic film";
static const char *bt2020_2100_cicp_09_gamut = "BT.2020/BT.2100";
static const char *smpte428_gamut            = "SMPTE ST 428-1";
static const char *dcip3_gamut               = "DCI P3/SMPTE RP 431";
static const char *displayp3_gamut           = "Display P3/SMPTE EG 432";
static const char *cicp_22_gamut             = "CICP 22";
static const char *aces_ap0_gamut            = "ACES AP0";
static const char *acescg_ap1_gamut          = "ACEScg AP1";
static const char *adobergb_gamut            = "Adobe RGB";
static const char *prophotorgb_gamut         = "ProPhoto RGB";
static const char *cie1931xyz_gamut          = "CIE 1931 XYZ";
static const char *custom_gamut              = "Custom";

static const char *unknown_tf       = "Unknown (Assuming sRGB)";
static const char *linear_tf        = "Linear";
static const char *gamma_tf         = "Gamma";
static const char *srgb_tf          = "sRGB IEC61966-2.1";
static const char *itu_tf           = "BT.709/2020";
static const char *pq_tf            = "BT.2100 PQ";
static const char *hlg_tf           = "BT.2100 HLG";
static const char *st240_tf         = "SMPTE ST 240";
static const char *log100_tf        = "Log100";
static const char *log100_sqrt10_tf = "Log100 Sqrt10";
static const char *iec61966_2_4_tf  = "IEC 61966-2-4";
static const char *dci_p3_tf        = "DCI-P3";

float2 white_point(WhitePoint wp)
{
    switch (wp)
    {
    case WhitePoint_Unspecified: return wp_D65;
    case WhitePoint_ACES: return wp_ACES;
    case WhitePoint_A: return wp_A;
    case WhitePoint_B: return wp_B;
    case WhitePoint_C: return wp_C;
    case WhitePoint_E: return wp_E;
    case WhitePoint_D50: return wp_D50;
    case WhitePoint_D55: return wp_D55;
    case WhitePoint_D65: return wp_D65;
    case WhitePoint_D75: return wp_D75;
    case WhitePoint_DCI: return wp_DCI;
    case WhitePoint_Custom: [[fallthrough]];
    default:
        // Return NaNs for unrecognized
        return {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    }
}

WhitePoint named_white_point(float2 wp)
{
    if (approx_equal(wp, wp_ACES))
        return WhitePoint_ACES;
    if (approx_equal(wp, wp_A))
        return WhitePoint_A;
    if (approx_equal(wp, wp_B))
        return WhitePoint_B;
    if (approx_equal(wp, wp_C))
        return WhitePoint_C;
    if (approx_equal(wp, wp_E))
        return WhitePoint_E;
    if (approx_equal(wp, wp_D50))
        return WhitePoint_D50;
    if (approx_equal(wp, wp_D55))
        return WhitePoint_D55;
    if (approx_equal(wp, wp_D65))
        return WhitePoint_D65;
    if (approx_equal(wp, wp_D75))
        return WhitePoint_D75;
    if (approx_equal(wp, wp_DCI))
        return WhitePoint_DCI;

    // Return custom for unrecognized
    return WhitePoint_Custom;
}

const char *white_point_name(WhitePoint wp)
{
    switch (wp)
    {
    case WhitePoint_Unspecified: return Unspecified_wp;
    case WhitePoint_ACES: return ACES_wp;
    case WhitePoint_A: return A_wp;
    case WhitePoint_B: return B_wp;
    case WhitePoint_C: return C_wp;
    case WhitePoint_E: return E_wp;
    case WhitePoint_D50: return D50_wp;
    case WhitePoint_D55: return D55_wp;
    case WhitePoint_D65: return D65_wp;
    case WhitePoint_D75: return D75_wp;
    case WhitePoint_DCI: return DCI_wp;
    case WhitePoint_Custom: [[fallthrough]];
    default: return Custom_wp;
    }
}

const char **white_point_names()
{
    // clang-format off
static const char* _names[] = {
    Unspecified_wp,
    ACES_wp,
    A_wp,
    B_wp,
    C_wp,
    E_wp,
    D50_wp,
    D55_wp,
    D65_wp,
    D75_wp,
    DCI_wp,
    Custom_wp,
    nullptr
};
    // clang-format on
    return _names;
}

const char *color_gamut_name(const ColorGamut primaries)
{
    switch (primaries)
    {
    case ColorGamut_sRGB_BT709: return srgb_bt709_gamut;
    case ColorGamut_Unspecified: return unspecified_gamut;
    case ColorGamut_BT470M: return bt470m_gamut;
    case ColorGamut_BT470BG: return bt470bg_gamut;
    case ColorGamut_SMPTE170M: return smpte170_gamut;
    case ColorGamut_SMPTE240M: return smpte240_gamut;
    case ColorGamut_Film: return generic_film_gamut;
    case ColorGamut_BT2020_2100: return bt2020_2100_cicp_09_gamut;
    case ColorGamut_SMPTE428: return smpte428_gamut;
    case ColorGamut_DCI_P3_SMPTE431: return dcip3_gamut;
    case ColorGamut_Display_P3_SMPTE432: return displayp3_gamut;
    case ColorGamut_CICP_22: return cicp_22_gamut;
    case ColorGamut_ACES_AP0: return aces_ap0_gamut;
    case ColorGamut_ACEScg_AP1: return acescg_ap1_gamut;
    case ColorGamut_AdobeRGB: return adobergb_gamut;
    case ColorGamut_ProPhotoRGB: return prophotorgb_gamut;
    case ColorGamut_CIE1931XYZ: return cie1931xyz_gamut;
    case ColorGamut_Custom: [[fallthrough]];
    default: return custom_gamut;
    }
}

const char **color_gamut_names()
{
    static const char *_names[] = {unspecified_gamut,
                                   srgb_bt709_gamut,
                                   bt470m_gamut,
                                   bt470bg_gamut,
                                   smpte170_gamut,
                                   smpte240_gamut,
                                   generic_film_gamut,
                                   bt2020_2100_cicp_09_gamut,
                                   smpte428_gamut,
                                   dcip3_gamut,
                                   displayp3_gamut,
                                   cicp_22_gamut,
                                   aces_ap0_gamut,
                                   acescg_ap1_gamut,
                                   adobergb_gamut,
                                   prophotorgb_gamut,
                                   cie1931xyz_gamut,
                                   custom_gamut,
                                   nullptr};
    return _names;
}

Chromaticities gamut_chromaticities(ColorGamut primaries)
{
    // data from:
    //  https://en.wikipedia.org/wiki/Standard_illuminant
    //  https://en.wikipedia.org/wiki/RGB_color_spaces
    //  http://www.brucelindbloom.com/index.html?WorkingSpaceInfo.html
    //  ITU-T H.273: https://www.itu.int/rec/T-REC-H.273-202407-I/en
    // Chromaticity data for common color spaces
    switch (primaries)
    {
    case ColorGamut_sRGB_BT709: [[fallthrough]];
    case ColorGamut_Unspecified: return {{0.6400f, 0.3300f}, {0.3000f, 0.6000f}, {0.1500f, 0.0600f}, wp_D65};
    case ColorGamut_BT470M: return {{0.6700f, 0.3300f}, {0.2100f, 0.7100f}, {0.1400f, 0.0800f}, wp_C};
    case ColorGamut_BT470BG: return {{0.6400f, 0.3300f}, {0.2900f, 0.6000f}, {0.1500f, 0.0600f}, wp_D65};
    case ColorGamut_SMPTE170M: return {{0.6300f, 0.3400f}, {0.3100f, 0.5950f}, {0.1550f, 0.0700f}, wp_D65};
    case ColorGamut_SMPTE240M: return {{0.6300f, 0.3400f}, {0.3100f, 0.5950f}, {0.1550f, 0.0700f}, wp_D65};
    case ColorGamut_Film: return {{0.6810f, 0.3190f}, {0.2430f, 0.6920f}, {0.1450f, 0.0490f}, wp_C};
    case ColorGamut_BT2020_2100: return {{0.7080f, 0.2920f}, {0.1700f, 0.7970f}, {0.1310f, 0.0460f}, wp_D65};
    case ColorGamut_SMPTE428: return {{0.7350f, 0.2650f}, {0.2740f, 0.7170f}, {0.1670f, 0.0090f}, wp_E};
    case ColorGamut_DCI_P3_SMPTE431: return {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, wp_DCI};
    case ColorGamut_Display_P3_SMPTE432: return {{0.6800f, 0.3200f}, {0.2650f, 0.6900f}, {0.1500f, 0.0600f}, wp_D65};
    case ColorGamut_CICP_22: return {{0.6300f, 0.3400f}, {0.2950f, 0.6050f}, {0.1550f, 0.0770f}, wp_D65};
    case ColorGamut_ACES_AP0: return {{0.73470f, 0.26530f}, {0.00000f, 1.00000f}, {0.00010f, -0.07700f}, wp_ACES};
    case ColorGamut_ACEScg_AP1: return {{0.713f, 0.293f}, {0.165f, 0.830f}, {0.128f, 0.044f}, wp_ACES};
    case ColorGamut_AdobeRGB: return {{0.6400f, 0.3300f}, {0.2100f, 0.7100f}, {0.1500f, 0.0600f}, wp_D65};
    case ColorGamut_ProPhotoRGB: return {{0.7347f, 0.2653f}, {0.1596f, 0.8404f}, {0.0366f, 0.0001f}, wp_D50};
    case ColorGamut_CIE1931XYZ: return {{1.f, 0.f}, {0.f, 1.f}, {0.f, 0.f}, wp_E};
    case ColorGamut_Custom: [[fallthrough]];
    default: throw std::invalid_argument("Unrecognized ColorGamut value");
    }
}

Chromaticities chromaticities_from_cicp(int cicp)
{
    switch (cicp)
    {
    case 1: return gamut_chromaticities(ColorGamut_sRGB_BT709);
    case 2: return gamut_chromaticities(ColorGamut_Unspecified);
    case 4: return gamut_chromaticities(ColorGamut_BT470M);
    case 5: return gamut_chromaticities(ColorGamut_BT470BG);
    case 6: return gamut_chromaticities(ColorGamut_SMPTE170M);
    case 7: return gamut_chromaticities(ColorGamut_SMPTE240M);
    case 8: return gamut_chromaticities(ColorGamut_Film);
    case 9: return gamut_chromaticities(ColorGamut_BT2020_2100);
    case 10: return gamut_chromaticities(ColorGamut_SMPTE428);
    case 11: return gamut_chromaticities(ColorGamut_DCI_P3_SMPTE431);
    case 12: return gamut_chromaticities(ColorGamut_Display_P3_SMPTE432);
    case 22: return gamut_chromaticities(ColorGamut_CICP_22);
    default: throw std::invalid_argument("Unrecognized or unsupported CICP value for chromaticities");
    }
}

ColorGamut named_color_gamut(const Chromaticities &chr)
{
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_sRGB_BT709)))
        return ColorGamut_sRGB_BT709;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_BT470M)))
        return ColorGamut_BT470M;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_BT470BG)))
        return ColorGamut_BT470BG;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_SMPTE170M)))
        return ColorGamut_SMPTE170M;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_SMPTE240M)))
        return ColorGamut_SMPTE240M;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_Film)))
        return ColorGamut_Film;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_BT2020_2100)))
        return ColorGamut_BT2020_2100;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_SMPTE428)))
        return ColorGamut_SMPTE428;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_DCI_P3_SMPTE431)))
        return ColorGamut_DCI_P3_SMPTE431;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_Display_P3_SMPTE432)))
        return ColorGamut_Display_P3_SMPTE432;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_CICP_22)))
        return ColorGamut_CICP_22;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_ACES_AP0)))
        return ColorGamut_ACES_AP0;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_ACEScg_AP1)))
        return ColorGamut_ACEScg_AP1;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_AdobeRGB)))
        return ColorGamut_AdobeRGB;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_ProPhotoRGB)))
        return ColorGamut_ProPhotoRGB;
    if (approx_equal(chr, gamut_chromaticities(ColorGamut_CIE1931XYZ)))
        return ColorGamut_CIE1931XYZ;

    return ColorGamut_Custom;
}

string transfer_function_name(TransferFunction tf, float gamma)
{
    switch (tf)
    {
    case TransferFunction_Linear: return linear_tf;
    case TransferFunction_Gamma: return fmt::format("{} (={})", gamma_tf, float(1.0 / gamma));
    case TransferFunction_sRGB: return srgb_tf;
    case TransferFunction_ITU: return itu_tf;
    case TransferFunction_BT2100_PQ: return pq_tf;
    case TransferFunction_BT2100_HLG: return hlg_tf;
    case TransferFunction_ST240: return st240_tf;
    case TransferFunction_Log100: return log100_tf;
    case TransferFunction_Log100_Sqrt10: return log100_sqrt10_tf;
    case TransferFunction_IEC61966_2_4: return iec61966_2_4_tf;
    case TransferFunction_DCI_P3: return dci_p3_tf;
    default: return unknown_tf;
    }
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
