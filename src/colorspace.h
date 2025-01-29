//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "common.h"
#include "fwd.h"
#include <string>
#include <vector>

#include <ImathMatrix.h>
#include <ImfChromaticities.h>
#include <ImfHeader.h>
#include <map>
#include <string>

// Function to deduce chromaticities and white point from a 3x3 RGB to XYZ matrix
inline void primaries_from_matrix(const float3x3 &rgb_to_XYZ, float2 *red, float2 *green, float2 *blue, float2 *white)
{
    // Multiplying the matrix by [1,0,0], [0,1,0], or [0,0,1] gives the XYZ values of the primaries.
    // Hence, the columns of the matrix are XYZ values of the primaries.
    // Divide each by its sum to get corresponding chromaticities.
    *red   = rgb_to_XYZ.x.xy() / sum(rgb_to_XYZ.x);
    *green = rgb_to_XYZ.y.xy() / sum(rgb_to_XYZ.y);
    *blue  = rgb_to_XYZ.z.xy() / sum(rgb_to_XYZ.z);

    // Multiplying the matrix by full-intensity for each primary [1,1,1] gives us XYZ of white.
    // Hence, the sum of the columns is the XYZ of white.
    // Divide that by the sum of its components to get its chromaticity;
    float3 wpXYZ = rgb_to_XYZ.x + rgb_to_XYZ.y + rgb_to_XYZ.z;
    *white       = wpXYZ.xy() / sum(wpXYZ);
}

/*!
    Build a combined color space conversion matrix from the chromaticities defined in src to those of dst.

    Adapted from AcesInputFile::Data::initColorConversion.
    See also http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html

    \param[out] M
        Conversion matrix
    \param src
        Source file header
    \param dst
        Destination chromaticities
    \param CAT_method
        Method for chromatic adaptation transform (1: XYZ scaling, 2: Bradford, 3: Von Kries; all other values use
   an identity CAT)
    \returns
        True if color conversion is needed, in which case M will contain the conversion matrix.
*/
bool color_conversion_matrix(Imath::M33f &M, const Imf::Chromaticities &src, const Imf::Chromaticities &dst,
                             int CAT_method = 0);

// names of the predefined linear color gamuts
extern const char *lin_ap0_gamut;
extern const char *lin_ap1_acescg_gamut;
extern const char *lin_adobergb_gamut;
extern const char *lin_cie1931xyz_gamut;
extern const char *lin_displayp3_gamut;
extern const char *lin_prophotorgb_gamut;
extern const char *lin_rec2020_2100_gamut;
extern const char *lin_srgb_rec709_gamut;

// return a map of common color spaces, indexed by name
const std::map<const char *, Imf::Chromaticities> &color_gamuts();

// return a reference to the chromaticities of a named color gamut, or throw if not found
const Imf::Chromaticities &gamut_chromaticities(const char *name);

// return a map of common white points, indexed by name
const std::map<const char *, Imath::V2f> &white_points();

const char **color_gamut_names();
const char  *color_gamut_description(const char *name);

// approximate equality comparison for Imf::Chromaticities
inline bool approx_equal(const Imf::Chromaticities &a, const Imf::Chromaticities &b, float tol = 1e-4f)
{
    return a.red.equalWithAbsError(b.red, tol) && a.green.equalWithAbsError(b.green, tol) &&
           a.blue.equalWithAbsError(b.blue, tol) && a.white.equalWithAbsError(b.white, tol);
}

using TransferFunction_ = int;
enum TransferFunction : TransferFunction_
{
    TransferFunction_Linear = 0,
    TransferFunction_Gamma,
    TransferFunction_sRGB,
    TransferFunction_Rec709_2020, //!< ITU-T BT.601, BT.709 and BT.2020 specifications, and and SMPTE 170M
    TransferFunction_Rec2100_PQ,
    TransferFunction_Rec2100_HLG,
    // DCI_P3, // TODO

    TransferFunction_COUNT
};

// names of the predefined transfer functions
extern const char *linear_tf;
extern const char *gamma_tf;
extern const char *srgb_tf;
extern const char *rec709_2020_tf;
extern const char *pq_tf;
extern const char *hlg_tf;

const char **transfer_function_names();

template <typename Real>
Real LinearToSRGB_positive(Real linear)
{
    static constexpr Real inv_gamma = Real(1) / Real(2.4);
    if (linear <= Real(0.0031308))
        return 12.92 * linear;
    else
        return Real(1.055) * std::pow(linear, inv_gamma) - Real(0.055);
}

// to/from linear to sRGB
template <typename Real>
Real LinearToSRGB(Real linear)
{
    return sign(linear) * LinearToSRGB_positive(std::fabs(linear));
}

template <typename Real>
Real LinearToGamma_positive(Real linear, Real inv_gamma)
{
    return std::pow(linear, inv_gamma);
}

template <typename Real>
Real LinearToGamma(Real linear, Real inv_gamma)
{
    return sign(linear) * LinearToGamma_positive(std::fabs(linear), inv_gamma);
}

template <typename Real>
Real SRGBToLinear_positive(Real sRGB)
{
    if (sRGB < Real(0.04045))
        return (Real(1) / Real(12.92)) * sRGB;
    else
        return std::pow((sRGB + Real(0.055)) * (Real(1) / Real(1.055)), Real(2.4));
}

template <typename Real>
Real SRGBToLinear(Real sRGB)
{
    return sign(sRGB) * SRGBToLinear_positive(std::fabs(sRGB));
}

template <typename Real>
Real Rec2020ToLinear(Real rec2020)
{
    constexpr Real alpha   = Real(1.09929682680944);
    constexpr Real alpham1 = alpha - Real(1.0);
    constexpr Real gamma   = Real(0.081242858298635); // alpha * pow(beta, 0.45) - (alpha-1)

    if (rec2020 < gamma)
        return rec2020 / Real(4.5);
    else
        return std::pow((rec2020 + alpham1) / alpha, Real(1.0 / 0.45));
}

template <typename Real>
Real LinearToRec2020(Real linear)
{
    constexpr Real alpha = Real(1.09929682680944);
    constexpr Real beta  = Real(0.018053968510807);
    if (linear < beta)
        return Real(4.5) * linear;
    else
        return alpha * std::pow(linear, Real(0.45)) - Real(alpha - 1.0);
}

/*! Defines Recommendation ITU-R BT.2100-2 Reference PQ electro-optical transfer function (EOTF).

    \param E_p Denotes a non-linear color value R', G', B' in PQ space.

    \returns Luminance of a displayed linear component R_D, G_D, B_D in cd/m^2/

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_PQ
*/
template <typename Real>
inline Real EOTF_PQ(Real E_p)
{
    constexpr Real m1inv = Real(16384) / Real(2610);
    constexpr Real m2inv = Real(4096) / Real(2523 * 128);
    constexpr Real c1    = Real(3424) / Real(4096);      // 0.8359375f;
    constexpr Real c2    = Real(2413) / Real(4096) * 32; // 18.8515625f;
    constexpr Real c3    = Real(2392) / Real(4096) * 32; // 18.6875f;

    const auto E_pm2 = std::pow(std::max(E_p, Real(0)), m2inv);
    return Real(10000) * std::pow(std::max(Real(0), E_pm2 - c1) / (c2 - c3 * E_pm2), m1inv);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG inverse optical-electro transfer function (OETF).

    \param x A non-linear color value R', G', B' in HLG space in the range of [0,1]

    \returns The R_S, G_S, B_S color component of linear scene light, normalized to [0,1]

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline Real OETF_inverse_HLG(Real x)
{
    // Constants defined by the HLG standard
    constexpr Real a = Real(0.17883277);
    constexpr Real b = Real(0.28466892); // 1 - 4*a;
    constexpr Real c = Real(0.55991073); // 0.5 - a * std::log(4 * a)

    return (x < Real(0.5)) ? (x * x) / Real(3) : (std::exp((x - c) / a) + b) / Real(12);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG inverse opto-optical transfer function (OOTF).

    \param E_s   Signal for a color component R_s, G_s, B_s proportional to scene linear light normalized to the range
                 [0,1].
    \param Y_s   Normalized scene luminance, defined as: 0.2627 × R_s + 0.6780 × G_s + 0.0593 × B_s
    \param alpha Adjustable user gain (display “contrast”) representing L_W, the nominal peak luminance of achromatic
                 pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns  Luminance of a displayed linear component {R_d, G_d, or B_d}, in cd/m^2.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline Real OOTF_HLG(Real E_s, Real Y_s, Real alpha, Real gamma = Real(1.2))
{
    return (alpha * std::pow(Y_s, gamma - Real(1)) * E_s);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG electro-optical transfer function (EOTF).

    \param E_p Denotes a non-linear color value R', G', B' in HLG space.
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns Luminance of a displayed linear component {R_d, G_d, or B_d} in cd/m^2/.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline Real EOTF_HLG(Real E_p, Real L_B = Real(0), Real L_W = Real(1000), Real gamma = Real(1.2))
{
    const Real alpha = L_W - L_B;
    const Real beta  = L_B != 0 ? std::sqrt(Real(3) * std::pow(L_B / L_W, Real(1) / gamma)) : Real(0);
    auto       E_s   = OETF_inverse_HLG(std::max(Real(0), (Real(1) - beta) * E_p + beta));
    return OOTF_HLG(E_s, E_s, alpha, gamma);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG electro-optical transfer function (EOTF).

    \param E_p A non-linear color value {R', G', B'} in HLG space.
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns Displayed linear color {R_d, G_d, B_d} in cd/m^2.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline la::vec<Real, 3> EOTF_HLG(const la::vec<Real, 3> &E_p, Real L_B = Real(0), Real L_W = Real(1000),
                                 Real gamma = Real(1.2))
{
    const Real alpha = L_W - L_B;
    const Real beta  = L_B != 0 ? std::sqrt(Real(3) * std::pow(L_B / L_W, Real(1) / gamma)) : Real(0);

    la::vec<Real, 3> E_s{OETF_inverse_HLG(std::max(Real(0), (Real(1) - beta) * E_p[0] + beta)),
                         OETF_inverse_HLG(std::max(Real(0), (Real(1) - beta) * E_p[1] + beta)),
                         OETF_inverse_HLG(std::max(Real(0), (Real(1) - beta) * E_p[2] + beta))};
    Real             Y_s = dot(la::vec<Real, 3>(0.2627, 0.6780, 0.0593), E_s);

    return {OOTF_HLG(E_s[0], Y_s, alpha, gamma), OOTF_HLG(E_s[1], Y_s, alpha, gamma),
            OOTF_HLG(E_s[2], Y_s, alpha, gamma)};
}

float3 YCToRGB(float3 input, float3 Yw);
float3 RGBToYC(float3 input, float3 Yw);
void   SRGBToLinear(float *r, float *g, float *b);
Color3 SRGBToLinear(const Color3 &c);
Color4 SRGBToLinear(const Color4 &c);
void   LinearToSRGB(float *r, float *g, float *b);
Color3 LinearToSRGB(const Color3 &c);
Color4 LinearToSRGB(const Color4 &c);
Color3 LinearToGamma(const Color3 &c, const Color3 &inv_gamma);
Color4 LinearToGamma(const Color4 &c, const Color3 &inv_gamma);

inline float to_linear(const float encoded, const TransferFunction tf, const float gamma = 2.2f)
{
    switch (tf)
    {
    case TransferFunction_Gamma: return LinearToGamma(encoded, 1.f / gamma);
    case TransferFunction_sRGB: return SRGBToLinear(encoded);
    case TransferFunction_Rec709_2020: return Rec2020ToLinear(encoded);
    case TransferFunction_Rec2100_PQ: return EOTF_PQ(encoded) / 255.f;
    case TransferFunction_Rec2100_HLG: return EOTF_HLG(encoded) / 255.f;
    case TransferFunction_Linear: [[fallthrough]];
    default: return encoded;
    }
}

inline float3 to_linear(const float3 &encoded, const TransferFunction tf, const float3 &gamma = float3(2.2f))
{
    switch (tf)
    {
    case TransferFunction_Gamma: return LinearToGamma(encoded, 1.f / gamma);
    case TransferFunction_sRGB: return SRGBToLinear(encoded);
    case TransferFunction_Rec709_2020:
        return float3{Rec2020ToLinear(encoded.x), Rec2020ToLinear(encoded.y), Rec2020ToLinear(encoded.z)};
    case TransferFunction_Rec2100_PQ: return float3{EOTF_PQ(encoded.x), EOTF_PQ(encoded.y), EOTF_PQ(encoded.z)} / 255.f;
    case TransferFunction_Rec2100_HLG: return EOTF_HLG(encoded) / 255.f;
    case TransferFunction_Linear: [[fallthrough]];
    default: return encoded;
    }
}

inline Color3 tonemap(const Color3 color, float gamma, bool sRGB)
{
    return sRGB ? LinearToSRGB(color) : LinearToGamma(color, Color3(1.f / gamma));
}
inline Color4 tonemap(const Color4 color, float gamma, bool sRGB)
{
    return Color4(sRGB ? LinearToSRGB(color.xyz()) : LinearToGamma(color.xyz(), Color3(1.f / gamma)), color.w);
}

const std::vector<std::string> &colorSpaceNames();

// assumes values of v are in byte range: [0, 255]
inline float byte_to_f32(float v, bool linearize = true)
{
    // perform unbiased quantization as in http://eastfarthing.com/blog/2015-12-19-color/
    float u8 = (v + 0.f) / 255.0f;
    return linearize ? SRGBToLinear(u8) : u8;
}

uint8_t f32_to_byte(float v, int x, int y, bool sRGB = true, bool dither = true);