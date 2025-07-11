//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "common.h"

#include "colormap.h"

#include "dithermatrix256.h"

#include "fwd.h"
#include <string>
#include <vector>

#include <ImathMatrix.h>
#include <ImfChromaticities.h>
#include <ImfHeader.h>
#include <map>
#include <string>

struct Chromaticities
{
    // Default constructor produces chromaticities according to Rec. ITU-R BT.709-3 (sRGB)
    float2 red   = {0.64f, 0.33f};
    float2 green = {0.30f, 0.60f};
    float2 blue  = {0.15f, 0.06f};
    float2 white = {0.31271f, 0.32902f}; // D65
};

// Function to deduce chromaticities and white point from a 3x3 RGB to XYZ matrix
inline Chromaticities primaries_from_matrix(const float3x3 &rgb_to_XYZ)
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
extern const char *lin_bt2020_2100_gamut;
extern const char *lin_srgb_bt709_gamut;
extern const char *lin_cicp_01_gamut;
extern const char *lin_cicp_04_gamut;
extern const char *lin_cicp_05_gamut;
extern const char *lin_cicp_06_gamut;
extern const char *lin_cicp_07_gamut;
extern const char *lin_cicp_08_gamut;
extern const char *lin_cicp_09_gamut;
extern const char *lin_cicp_10_gamut;
extern const char *lin_cicp_11_gamut;
extern const char *lin_cicp_12_gamut;
extern const char *lin_cicp_22_gamut;

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
    TransferFunction_ITU, //!< ITU-T BT.601, BT.709 and BT.2020 specifications, and SMPTE 170M
    TransferFunction_BT2100_PQ,
    TransferFunction_BT2100_HLG,
    TransferFunction_ST240,
    TransferFunction_Log100,
    TransferFunction_Log100_Sqrt10,
    TransferFunction_IEC61966_2_4,
    TransferFunction_DCI_P3,
    // DCI_P3, // TODO

    TransferFunction_COUNT
};

// names of the predefined transfer functions
extern const char *linear_tf;
extern const char *gamma_tf;
extern const char *srgb_tf;
extern const char *itu_tf;
extern const char *pq_tf;
extern const char *hlg_tf;
extern const char *st240_tf;
extern const char *iec61966_2_4_tf;
extern const char *dci_p3_tf;

const char **transfer_function_names();

template <typename Real>
Real LinearToSRGB_positive(Real linear)
{
    static constexpr Real inv_gamma = Real(1) / Real(2.4);
    if (linear <= Real(0.0031308))
        return Real(12.92) * linear;
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
Real inverse_OETF_ITU(Real bt2020)
{
    constexpr Real alpha   = Real(1.09929682680944);
    constexpr Real alpham1 = alpha - Real(1.0);
    constexpr Real gamma   = Real(0.081242858298635); // alpha * pow(beta, 0.45) - (alpha-1)

    if (bt2020 < gamma)
        return bt2020 / Real(4.5);
    else
        return std::pow((bt2020 + alpham1) / alpha, Real(1.0 / 0.45));
}

/*! Defines The ITU-T BT.601, BT.709 and BT.2020 optical-electro transfer function (OETF).

    The opto-electrical transfer function (OETF) in ITU-T BT.601, BT.709 and BT.2020 specifications (for standard
   definition television, HDTV and UHDTV respectively), and SMPTE 170M, which defines NTSC broadcasts.

    \param linear A linear color value R_S, G_S, B_S in the range of [0,1]

    \returns A non-linear color value R'_S, G'_S, B'_S in the range of [0,1]

    see:
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_ITU
*/
template <typename Real>
Real OETF_ITU(Real linear)
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
inline Real EOTF_BT2100_PQ(Real E_p)
{
    constexpr Real m1inv = Real(16384) / Real(2610);
    constexpr Real m2inv = Real(4096) / Real(2523 * 128);
    constexpr Real c1    = Real(3424) / Real(4096);      // 0.8359375f;
    constexpr Real c2    = Real(2413) / Real(4096) * 32; // 18.8515625f;
    constexpr Real c3    = Real(2392) / Real(4096) * 32; // 18.6875f;

    const auto E_pm2 = std::pow(std::max(E_p, Real(0)), m2inv);
    return Real(10000) * std::pow(std::max(Real(0), E_pm2 - c1) / (c2 - c3 * E_pm2), m1inv);
}

/*! Defines Recommendation ITU-R BT.2100-2 Reference PQ inverse electro-optical transfer function (EOTF^-1).

    \param F_D Luminance of a displayed linear component R_D, G_D, B_D in cd/m^2/

    \returns A non-linear color value R', G', B' in PQ space.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_PQ
*/
template <typename Real>
inline Real inverse_EOTF_BT2100_PQ(Real F_D)
{
    constexpr Real m1 = Real(2610) / Real(16384);
    constexpr Real m2 = Real(2523 * 128) / Real(4096);
    constexpr Real c1 = Real(3424) / Real(4096);      // 0.8359375f;
    constexpr Real c2 = Real(2413) / Real(4096) * 32; // 18.8515625f;
    constexpr Real c3 = Real(2392) / Real(4096) * 32; // 18.6875f;

    const Real Y = F_D / Real(10000);
    return std::pow((c1 + c2 * std::pow(Y, m1)) / (1 + c3 * std::pow(Y, m1)), m2);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG inverse optical-electro transfer function (OETF^-1).

    \param x A non-linear color value R', G', B' in HLG space in the range of [0,1]

    \returns The R_S, G_S, B_S color component of linear scene light, normalized to [0,1]

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline Real inverse_OETF_BT2100_HLG(Real x)
{
    // Constants defined by the HLG standard
    constexpr Real a = Real(0.17883277);
    constexpr Real b = Real(0.28466892); // 1 - 4*a;
    constexpr Real c = Real(0.55991073); // 0.5 - a * std::log(4 * a)

    return (x < Real(0.5)) ? (x * x) / Real(3) : (std::exp((x - c) / a) + b) / Real(12);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG optical-electro transfer function (OETF).

    \param E The R_S, G_S, B_S color component of linear scene light, normalized to [0,1]

    \returns The resulting non-linear color value R'_S, G'_S, B'_S in HLG space in the range of [0,1]

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline Real OETF_BT2100_HLG(Real E)
{
    // Constants defined by the HLG standard
    constexpr Real a = Real(0.17883277);
    constexpr Real b = Real(0.28466892); // 1 - 4*a;
    constexpr Real c = Real(0.55991073); // 0.5 - a * std::log(4 * a)

    return (E <= Real(1) / Real(12)) ? std::sqrt(Real(3) * E) : (a * std::log(12 * E - b) + c);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG opto-optical transfer function (OOTF^-1).

    \param E_S   Signal for a color component R_S, G_S, B_S proportional to scene linear light normalized to the range
                 [0,1].
    \param Y_S   Normalized scene luminance, defined as: 0.2627 × R_S + 0.6780 × G_S + 0.0593 × B_S
    \param alpha Adjustable user gain (display “contrast”) representing L_W, the nominal peak luminance of achromatic
                 pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns  Luminance of a displayed linear component {R_D, G_D, or B_D}, in cd/m^2.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline Real OOTF_BT2100_HLG(Real E_S, Real Y_S, Real alpha, Real gamma = Real(1.2))
{
    return (alpha * std::pow(Y_S, gamma - Real(1)) * E_S);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG electro-optical transfer function (EOTF).

    \param E_p Denotes a non-linear color value R', G', B' in HLG space.
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns Luminance of a displayed linear component {R_D, G_D, or B_D} in cd/m^2/.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline Real EOTF_BT2100_HLG(Real E_p, Real L_B = Real(0), Real L_W = Real(1000), Real gamma = Real(1.2))
{
    const Real alpha = L_W - L_B;
    const Real beta  = L_B != 0 ? std::sqrt(Real(3) * std::pow(L_B / L_W, Real(1) / gamma)) : Real(0);
    auto       E_s   = inverse_OETF_BT2100_HLG(std::max(Real(0), (Real(1) - beta) * E_p + beta));
    return OOTF_BT2100_HLG(E_s, E_s, alpha, gamma);
}

/*! Recommendation ITU-R BT.2100-2 Reference HLG electro-optical transfer function (EOTF).

    \param E_p A non-linear color value {R', G', B'} in HLG space.
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns Displayed linear color {R_D, G_D, B_D} in cd/m^2.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
template <typename Real>
inline la::vec<Real, 3> EOTF_BT2100_HLG(const la::vec<Real, 3> &E_p, Real L_B = Real(0), Real L_W = Real(1000),
                                        Real gamma = Real(1.2))
{
    const Real alpha = L_W - L_B;
    const Real beta  = L_B != 0 ? std::sqrt(Real(3) * std::pow(L_B / L_W, Real(1) / gamma)) : Real(0);

    la::vec<Real, 3> E_s{inverse_OETF_BT2100_HLG(std::max(Real(0), (Real(1) - beta) * E_p[0] + beta)),
                         inverse_OETF_BT2100_HLG(std::max(Real(0), (Real(1) - beta) * E_p[1] + beta)),
                         inverse_OETF_BT2100_HLG(std::max(Real(0), (Real(1) - beta) * E_p[2] + beta))};
    Real             Y_s = dot(la::vec<Real, 3>(Real(0.2627), Real(0.6780), Real(0.0593)), E_s);

    return {OOTF_BT2100_HLG(E_s[0], Y_s, alpha, gamma), OOTF_BT2100_HLG(E_s[1], Y_s, alpha, gamma),
            OOTF_BT2100_HLG(E_s[2], Y_s, alpha, gamma)};
}

/*! ST240/SMPTE240M OETF (Opto-Electronic Transfer Function).

    Since the OOTF is linear, this function also implements EOTF^-1.
*/
template <typename Real>
inline Real OETF_ST240(Real linear)
{
    constexpr Real alpha = Real(1.1115);
    constexpr Real beta  = Real(0.0228);
    if (linear < beta)
        return Real(4.0) * linear;
    else
        return alpha * std::pow(linear, Real(0.45)) - (alpha - Real(1.0));
}

/*! ST240/SMPTE240M EOTF (Electro-Optical Transfer Function).

    Since the OOTF is linear, this function also implements OETF^-1.
*/
template <typename Real>
inline Real EOTF_ST240(Real nonlinear)
{
    constexpr Real alpha = Real(1.1115);
    constexpr Real beta  = Real(0.0913);
    if (nonlinear < beta)
        return nonlinear / Real(4.0);
    else
        return std::pow((nonlinear + (alpha - Real(1.0))) / alpha, Real(1.0) / Real(0.45));
}

template <typename Real>
inline Real EOTF_log100(Real nonlinear)
{
    return nonlinear > 0.0f ? std::exp((nonlinear - 1.0f) * 2.0f * std::log(10.0f)) : 0.0f;
}

template <typename Real>
inline Real OETF_log100(Real linear)
{
    return linear > 0.0f ? (std::log(linear) / (2.0f * std::log(10.0f)) + 1.0f) : 0.0f;
}

template <typename Real>
inline Real EOTF_log100_sqrt10(Real nonlinear)
{
    return nonlinear > 0.0f ? std::exp((nonlinear - 1.0f) * 2.5f * std::log(10.0f)) : 0.0f;
}

template <typename Real>
inline Real OETF_log100_sqrt10(Real linear)
{
    return linear > 0.0f ? (std::log(linear) / (2.5f * std::log(10.0f)) + 1.0f) : 0.0f;
}

/*! IEC 61966-2-4 OETF (Opto-Electronic Transfer Function).

    Implements the transfer function as specified in IEC 61966-2-4.

    This is imply the ITU OETF which mirrors negative values.

    \param linear Linear input value (R, G, or B channel)
    \returns Non-linear encoded value
*/
template <typename Real>
inline Real OETF_IEC61966_2_4(Real linear)
{
    return std::copysign(OETF_ITU(std::abs(linear)), linear);
}

/*! IEC 61966-2-4 EOTF (Electro-Optical Transfer Function).

    Implements the inverse transfer function as specified in IEC 61966-2-4.

    This is imply the ITU inverse OETF which mirrors negative values.

    \param nonlinear Non-linear encoded value (R', G', or B' channel)
    \returns Linear output value
*/
template <typename Real>
inline Real EOTF_IEC61966_2_4(Real nonlinear)
{
    return std::copysign(inverse_OETF_ITU(std::abs(nonlinear)), nonlinear);
}

/*! DCI-P3 EOTF (Electro-Optical Transfer Function).

    Applies the DCI-P3 EOTF as specified in SMPTE ST 428-1 (DCI):
    X = X'^2.6 * 52.37
    Y = Y'^2.6 * 52.37
    Z = Z'^2.6 * 52.37

    \param nonlinear Non-linear encoded value (R', G', or B' channel)
    \returns Linear output value
*/
template <typename Real>
inline Real EOTF_DCI_P3(Real nonlinear)
{
    return std::pow(nonlinear, Real(2.6)) * Real(52.37);
}

/*! DCI-P3 inverse EOTF (EOTF^-1).

    Applies the inverse DCI-P3 EOTF as specified in SMPTE ST 428-1 (DCI):
    X' = (X / 52.37)^(1/2.6)
    Y' = (Y / 52.37)^(1/2.6)
    Z' = (Z / 52.37)^(1/2.6)

    \param linear Linear input value (R, G, or B channel)
    \returns Non-linear encoded value
*/
template <typename Real>
inline Real inverse_EOTF_DCI_P3(Real linear)
{
    return std::pow(linear / Real(52.37), Real(1.0) / Real(2.6));
}

float3 YCToRGB(float3 input, float3 Yw);
float3 RGBToYC(float3 input, float3 Yw);
Color3 SRGBToLinear(const Color3 &c);
Color4 SRGBToLinear(const Color4 &c);
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
    case TransferFunction_ITU: return inverse_OETF_ITU(encoded);
    case TransferFunction_BT2100_PQ: return EOTF_BT2100_PQ(encoded) / 219.f;
    case TransferFunction_BT2100_HLG: return EOTF_BT2100_HLG(encoded) / 219.f;
    case TransferFunction_ST240: return EOTF_ST240(encoded);
    case TransferFunction_Log100: return EOTF_log100(encoded);
    case TransferFunction_Log100_Sqrt10: return EOTF_log100_sqrt10(encoded);
    case TransferFunction_IEC61966_2_4: return EOTF_IEC61966_2_4(encoded);
    case TransferFunction_DCI_P3: return EOTF_DCI_P3(encoded);
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
    case TransferFunction_ITU: return la::apply(inverse_OETF_ITU<float>, encoded);
    case TransferFunction_BT2100_PQ: return la::apply(EOTF_BT2100_PQ<float>, encoded) / 219.f;
    case TransferFunction_BT2100_HLG: return EOTF_BT2100_HLG(encoded) / 219.f;
    case TransferFunction_ST240: return la::apply(EOTF_ST240<float>, encoded);
    case TransferFunction_Log100: return la::apply(EOTF_log100<float>, encoded);
    case TransferFunction_Log100_Sqrt10: return la::apply(EOTF_log100_sqrt10<float>, encoded);
    case TransferFunction_IEC61966_2_4: return la::apply(EOTF_IEC61966_2_4<float>, encoded);
    case TransferFunction_DCI_P3: return la::apply(EOTF_DCI_P3<float>, encoded);
    case TransferFunction_Linear: [[fallthrough]];
    default: return encoded;
    }
}

void to_linear(float *pixels, int3 size, TransferFunction tf, float gamma = 2.2f);

inline Color3 tonemap(const Color3 color, float gamma, Tonemap tonemap_mode, Colormap_ colormap)
{
    switch (tonemap_mode)
    {
    default: [[fallthrough]];
    case Tonemap_Gamma: return LinearToGamma(color, float3{1.f / gamma});
    case Tonemap_FalseColor: [[fallthrough]];
    case Tonemap_PositiveNegative:
    {
        auto  xform     = tonemap_mode == Tonemap_FalseColor ? float2{1.f, 0.f} : float2{0.5f, 0.5f};
        float avg       = dot(color, float3(1.f / 3.f));
        float cmap_size = Colormap::values(colormap).size();
        float t         = lerp(0.5f / cmap_size, (cmap_size - 0.5f) / cmap_size, xform.x * avg + xform.y);
        return SRGBToLinear(float4{ImPlot::SampleColormap(saturate(t), colormap)}.xyz());
    }
    }
}
inline Color4 tonemap(const Color4 color, float gamma, Tonemap tonemap_mode, Colormap_ colormap)
{
    return Color4(tonemap(color.xyz(), gamma, tonemap_mode, colormap), color.w);
}

inline float2 blend(float2 top, float2 bottom, EBlendMode blend_mode)
{
    float diff  = top.x - bottom.x;
    float alpha = top.y + bottom.y * (1.f - top.y);
    switch (blend_mode)
    {
    // case NORMAL_BLEND:
    default: return float2(top.x + bottom.x * (1.f - top.y), alpha);
    case MULTIPLY_BLEND: return float2(top.x * bottom.x, alpha);
    case DIVIDE_BLEND: return float2(top.x / bottom.x, alpha);
    case ADD_BLEND: return float2(top.x + bottom.x, alpha);
    case AVERAGE_BLEND: return 0.5f * (top + bottom);
    case SUBTRACT_BLEND: return float2(diff, alpha);
    case DIFFERENCE_BLEND: return float2(abs(diff), alpha);
    case RELATIVE_DIFFERENCE_BLEND: return float2(abs(diff) / (bottom.x + 0.01f), alpha);
    }
    return float2(0.f);
}

inline float blend(float top, float bottom, EBlendMode blend_mode)
{
    float diff = top - bottom;
    switch (blend_mode)
    {
    // case NORMAL_BLEND:
    default: return top;
    case MULTIPLY_BLEND: return top * bottom;
    case DIVIDE_BLEND: return top / bottom;
    case ADD_BLEND: return top + bottom;
    case AVERAGE_BLEND: return 0.5f * (top + bottom);
    case SUBTRACT_BLEND: return diff;
    case DIFFERENCE_BLEND: return abs(diff);
    case RELATIVE_DIFFERENCE_BLEND: return abs(diff) / (bottom + 0.01f);
    }
    return float(0.f);
}

inline float4 blend(float4 top, float4 bottom, EBlendMode blend_mode)
{
    float3 diff  = top.xyz() - bottom.xyz();
    float  alpha = top.w + bottom.w * (1.f - top.w);
    switch (blend_mode)
    {
    // case NORMAL_BLEND:
    default: return float4(top.xyz() + bottom.xyz() * (1.f - top.w), alpha);
    case MULTIPLY_BLEND: return float4(top.xyz() * bottom.xyz(), alpha);
    case DIVIDE_BLEND: return float4(top.xyz() / bottom.xyz(), alpha);
    case ADD_BLEND: return float4(top.xyz() + bottom.xyz(), alpha);
    case AVERAGE_BLEND: return 0.5f * (top + bottom);
    case SUBTRACT_BLEND: return float4(diff, alpha);
    case DIFFERENCE_BLEND: return float4(abs(diff), alpha);
    case RELATIVE_DIFFERENCE_BLEND: return float4(abs(diff) / (bottom.xyz() + float3(0.01f)), alpha);
    }
    return float4(0.f);
}

//! see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_FULL
template <typename T>
inline float dequantize_full(T v)
{
    constexpr float denom = 1.f / float(std::numeric_limits<T>::max());
    return v * denom;
}

//! see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_FULL
template <typename T>
T quantize_full(float v, int x = 0, int y = 0, bool dither = true)
{
    constexpr float maximum = float(std::numeric_limits<T>::max());
    return (T)std::clamp(v * maximum + 0.5f + (dither ? tent_dither(x, y) : 0.f), 0.0f, maximum);
}

//! see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_NARROW
template <typename T>
inline float dequantize_narrow(T v)
{
    constexpr float denom = 1.f / float(1u << (std::numeric_limits<T>::digits - 8));
    return (v * denom - 16.f) / 219.f;
}

//! see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_NARROW
template <typename T>
T quantize_narrow(float v, int x = 0, int y = 0, bool dither = true)
{
    constexpr float maximum = 1u << (std::numeric_limits<T>::digits - 8);
    return (T)std::clamp((219.f * v + 16.f) * maximum + 0.5f + (dither ? tent_dither(x, y) : 0.f), 0.0f, maximum);
}

#define HDRVIEW_COL32_R_SHIFT 24
#define HDRVIEW_COL32_G_SHIFT 16
#define HDRVIEW_COL32_B_SHIFT 8
#define HDRVIEW_COL32_A_SHIFT 0
#define HDRVIEW_COL32_A_MASK  0x000000FF

inline float4 color_u32_to_f128(uint32_t in)
{
    float s = 1.0f / 255.0f;
    return float4(((in >> HDRVIEW_COL32_R_SHIFT) & 0xFF) * s, ((in >> HDRVIEW_COL32_G_SHIFT) & 0xFF) * s,
                  ((in >> HDRVIEW_COL32_B_SHIFT) & 0xFF) * s, ((in >> HDRVIEW_COL32_A_SHIFT) & 0xFF) * s);
}

inline uint32_t color_f128_to_u32(const float4 &in)
{
    uint32_t out;
    out = ((uint32_t)(saturate(in.x) * 255.f + 0.5f)) << HDRVIEW_COL32_R_SHIFT;
    out |= ((uint32_t)(saturate(in.y) * 255.f + 0.5f)) << HDRVIEW_COL32_G_SHIFT;
    out |= ((uint32_t)(saturate(in.z) * 255.f + 0.5f)) << HDRVIEW_COL32_B_SHIFT;
    out |= ((uint32_t)(saturate(in.w) * 255.f + 0.5f)) << HDRVIEW_COL32_A_SHIFT;
    return out;
}

// /*!\brief Applies the transfer function as defined in Table 3 of ITU-T H.273 for the given TransferCharacteristics
//  * value.
//  *
//  * See Table 3 – Interpretation of transfer characteristics (TransferCharacteristics) value.
//  *
//  * @param value The TransferCharacteristics value (see H.273 Table 3)
//  * @param linear The linear input value
//  * @return The encoded value according to the transfer function
//  */
// inline float ApplyTransferCharacteristics(int value, float linear)
// {
//     switch (value)
//     {
//     case 0: // Reserved
//         return linear;
//     case 1:  // BT.709
//     case 6:  // SMPTE 170M
//     case 14: // BT.2020 10-bit
//     case 15: // BT.2020 12-bit
//         return OETF_ITU(linear);
//     case 2:            // Unspecified
//         return linear; // No-op
//     case 3:            // Reserved
//         return linear;
//     case 4: // Gamma 2.2 (IEC 61966-2-1)
//         return LinearToGamma(linear, 1.0f / 2.2f);
//     case 5: // Gamma 2.8 (ITU-R BT.470 System B G)
//         return LinearToGamma(linear, 1.0f / 2.8f);
//     case 7: // ST240/SMPTE 240M
//         return OETF_ST240(linear);
//     case 8: // linear transfer
//         return linear;
//     case 9: // Logarithmic transfer characteristic
//         return linear;
//     case 10: // Logarithmic transfer characteristic
//         return linear;
//     case 11: // IEC 61966-2-4
//         return OETF_IEC61966_2_4(linear);
//     case 12: // ITU-R BT.1361-0 extended color gamut system
//         return OETF_ITU(linear);
//     case 13: // IEC 61966-2-1 sRGB (with
//              // MatrixCoefficients equal to 0)
//              // IEC 61966-2-1 sYCC (with
//              // MatrixCoefficients equal to 5)
//         return LinearToSRGB(linear);
//     case 16: // PQ (ST 2084)
//         return inverse_EOTF_BT2100_PQ(linear);
//     case 17: // SMPTE ST 428-1 (DCI)
//         return inverse_EOTF_DCI_P3(linear);
//     case 18: // HLG (ARIB STD-B67)
//         return OETF_BT2100_HLG(linear);
//     default:
//         // For unsupported or reserved values, return input unchanged
//         return linear;
//     }
// }
