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

#include <string>

using AlphaType_ = int;
enum AlphaType : AlphaType_
{
    AlphaType_None = 0,
    AlphaType_PremultipliedLinear,
    AlphaType_PremultipliedNonLinear, // values are premultiplied in e.g. gamma- or sRGB-encoded space
    AlphaType_Straight,
    AlphaType_Count
};

const char  *alpha_type_name(AlphaType_ at);
const char **alpha_type_names();

/**
 * Computes the luminance as ``l = 0.299r + 0.587g + 0.144b + 0.0a``.  If
 * the luminance is less than 0.5, white is returned.  If the luminance is
 * greater than or equal to 0.5, black is returned.  Both returns will have
 * an alpha component of 1.0.
 */
inline Color3 contrasting_color(const Color3 &c)
{
    float luminance = dot(c, Color3(0.299f, 0.587f, 0.144f));
    return Color3(luminance < 0.5f ? 1.f : 0.f);
}

/**
 * Computes the luminance as ``l = 0.299r + 0.587g + 0.144b + 0.0a``.  If
 * the luminance is less than 0.5, white is returned.  If the luminance is
 * greater than or equal to 0.5, black is returned.  Both returns will have
 * an alpha component of 1.0.
 */
inline Color4 contrasting_color(const Color4 &c) { return {contrasting_color(c.xyz()), 1.f}; }

struct Chromaticities
{
    // Default constructor produces chromaticities according to Rec. ITU-R BT.709-3 (sRGB)
    float2 red{0.64f, 0.33f};
    float2 green{0.30f, 0.60f};
    float2 blue{0.15f, 0.06f};
    float2 white{0.31271f, 0.32902f}; // D65

    bool operator==(const Chromaticities &c) const
    {
        return red == c.red && green == c.green && blue == c.blue && white == c.white;
    }

    bool operator!=(const Chromaticities &c) const
    {
        return red != c.red || green != c.green || blue != c.blue || white != c.white;
    }
};

template <typename T, int N>
inline bool approx_equal(const la::vec<T, N> &a, const la::vec<T, N> &b, T tol = T(1e-4))
{
    return la::all(la::less(la::abs(a - b), tol));
}

template <typename T, int N, int M>
inline bool approx_equal(const la::mat<T, N, M> &a, const la::mat<T, N, M> &b, T tol = T(1e-4))
{
    return la::all(la::less(la::abs(a - b), tol));
}

// approximate equality comparison for Chromaticities
inline bool approx_equal(const Chromaticities &a, const Chromaticities &b, float tol = 1e-4f)
{
    return approx_equal(a.red, b.red, tol) && approx_equal(a.green, b.green, tol) &&
           approx_equal(a.blue, b.blue, tol) && approx_equal(a.white, b.white, tol);
}

using WhitePoint = int;
enum WhitePoint_ : WhitePoint
{
    WhitePoint_FirstNamed = 0,
    WhitePoint_ACES       = WhitePoint_FirstNamed, // Academy Color Encoding System, ~6000k
    WhitePoint_D50,                                // horizon light, ICC profile PCS
    WhitePoint_D55,                                // mid-morning / mid-afternoon daylight
    WhitePoint_D65,                                // noon daylight: television, sRGB color space
    WhitePoint_D75,                                // North sky daylight
    WhitePoint_D93,                                // high-efficiency blue phosphor monitors, BT.2035
    WhitePoint_DCI,                                // ~6300 K
    WhitePoint_F1,                                 // daylight fluorescent
    WhitePoint_F2,                                 // cool white fluorescent
    WhitePoint_F3,                                 // white fluorescent
    WhitePoint_F4,                                 // warm white fluorescent
    WhitePoint_F5,                                 // daylight fluorescent
    WhitePoint_F6,                                 // light white fluorescent
    WhitePoint_F7,                                 // D65 simulator, daylight simulator
    WhitePoint_F8,                                 // D50 simulator, Sylvania F40 Design 50
    WhitePoint_F9,                                 // cool white deluxe fluorescent
    WhitePoint_F10,                                // Philips TL85, Ultralume 50
    WhitePoint_F11,                                // Philips TL84, Ultralume 40
    WhitePoint_F12,                                // Philips TL83, Ultralume 30
    WhitePoint_A,                                  // incandescent / tungsten
    WhitePoint_B,                                  // obsolete, direct sunlight at noon
    WhitePoint_C,                                  // obsolete, average / North sky daylight
    WhitePoint_E,                                  // equal energy
    WhitePoint_LastNamed = WhitePoint_E,
    WhitePoint_Unspecified, //!< unspecified, assuming D65
    WhitePoint_Custom,
    WhitePoint_Count
};

float2       white_point(WhitePoint_ wp);
const char  *white_point_name(WhitePoint_ wp);
const char **white_point_names();
WhitePoint_  named_white_point(float2 wp);

using ColorGamut = int;
enum ColorGamut_ : ColorGamut
{
    ColorGamut_FirstNamed = 0,
    ColorGamut_sRGB_BT709 = ColorGamut_FirstNamed,
    ColorGamut_BT470M,
    ColorGamut_BT470BG,
    ColorGamut_SMPTE170M_240M,
    ColorGamut_Film,
    ColorGamut_BT2020_2100,
    ColorGamut_SMPTE428,
    ColorGamut_DCI_P3_SMPTE431,
    ColorGamut_Display_P3_SMPTE432,
    ColorGamut_CICP_22,
    ColorGamut_ACES_AP0,
    ColorGamut_ACEScg_AP1,
    ColorGamut_AdobeRGB,
    ColorGamut_ProPhotoRGB,
    ColorGamut_CIE1931XYZ,
    ColorGamut_LastNamed = ColorGamut_CIE1931XYZ,
    ColorGamut_Unspecified,
    ColorGamut_Custom,
    ColorGamut_Count
};

//! Returns a description of the ColorGamut enum value.
const char  *color_gamut_name(const ColorGamut_ primaries);
const char **color_gamut_names();

//! Returns the Chromaticities corresponding to one of the predefined color primaries, or throws if the primaries
//! are not recognized.
Chromaticities gamut_chromaticities(ColorGamut_ primaries);
Chromaticities chromaticities_from_cicp(int cicp);
// Returns -1 if the chromaticities do not match one of the predefined CICP values
int         chromaticities_to_cicp(const Chromaticities &chr);
ColorGamut_ named_color_gamut(const Chromaticities &chr);

struct TransferFunction
{
    using Type = int;
    enum Type_ : Type
    {
        Unspecified = 0,
        Linear,
        Gamma,
        sRGB,
        ITU, //!< ITU-T BT.601, BT.709 and BT.2020 specifications, and SMPTE 170M
        BT2100_PQ,
        BT2100_HLG,
        ST240,
        Log100,
        Log100_Sqrt10,
        IEC61966_2_4,
        DCI_P3,
        Count
    };

    Type_ type  = Unspecified;
    float gamma = 2.2f; // Gamma always refers to the exponent of the to_linear transform, only used if type ==
                        // TransferFunction::Gamma

    TransferFunction(Type_ t, float g = 2.2f) : type(t), gamma(g) {}
};

std::string      transfer_function_name(TransferFunction tf);
TransferFunction transfer_function_from_cicp(int cicp);
int              transfer_function_to_cicp(TransferFunction tf);

using AdaptationMethod_ = int;
enum AdaptationMethod : AdaptationMethod_
{
    AdaptationMethod_Identity = 0, //!< Identity CAT
    AdaptationMethod_XYZScaling,   //!< XYZ scaling CAT
    AdaptationMethod_Bradford,     //!< Bradford CAT
    AdaptationMethod_VonKries,     //!< Von Kries CAT
    AdaptationMethod_Count
};

float3x3        RGB_to_XYZ(const Chromaticities &chroma, float Y);
inline float3x3 XYZ_to_RGB(const Chromaticities &chroma, float Y) { return inverse(RGB_to_XYZ(chroma, Y)); }
inline float3   computeYw(const Chromaticities &cr)
{
    auto m = RGB_to_XYZ(cr, 1.f);
    return float3{m[0][1], m[1][1], m[2][1]} / (m[0][1] + m[1][1] + m[2][1]);
}

const float3   &sRGB_Yw();
const float3x3 &sRGB_to_XYZ();
const float3x3 &XYZ_to_sRGB();

template <typename T>
struct TabulatedSpectrum
{
    std::vector<T> values;
    float          min_wavelength = 380.f; //!< minimum wavelength in nm
    float          max_wavelength = 830.f; //!< maximum wavelength in nm

    //! Return the location of the spanning bins (x,y components), and the lerp factor between them (z)
    float3 find_interval(float wavelength) const
    {
        float t = saturate(lerp_factor(min_wavelength, max_wavelength, wavelength));

        float width = (float)(values.size() - 1);
        float i0    = std::floor(t * values.size());
        float i1    = std::ceil(t * values.size());

        float ti;
        if (i0 != i1)
            ti = (t * width - i0) / (i1 - i0);
        else
            ti = 1.0f;

        return {i0, i1, ti};
    }

    T eval(float3 i) const { return lerp(values[(size_t)i.x], values[(size_t)i.y], i.z); }

    T eval(float wavelength) const
    {
        auto i = find_interval(wavelength);
        return lerp(values[(size_t)i.x], values[(size_t)i.y], i.z);
    }
};

/*! Compute Planckian locus from correlated color temperature (CCT) in Kelvin.

    Based on Kim et al. (2002), valid for 1667K <= T <= 25000K.

    See https://en.wikipedia.org/wiki/Planckian_locus#Approximation
*/
float2 Kelvin_to_xy(float T);
/*! Compute CIE daylight chromaticity coordinates (xD, yD) from correlated color temperature (CCT) in Kelvin

    See https://en.wikipedia.org/wiki/Standard_illuminant#Computation
*/
float2                           daylight_to_xy(float T);
const TabulatedSpectrum<float>  &white_point_spectrum(WhitePoint_ wp = WhitePoint_D65);
const TabulatedSpectrum<float3> &CIE_XYZ_spectra();

/*! Convert a wavelength in nanometers to a CIE 1931 chromaticity value.

    \param wavelength
        Wavelength in nanometers, must be in the range [380, 830].
    \returns
        CIE 1931 xy chromaticity value.
*/
inline float2 wavelength_to_xy(float wavelength)
{
    auto   xyz = white_point_spectrum(WhitePoint_D65).eval(wavelength) * CIE_XYZ_spectra().eval(wavelength);
    float2 xy  = xyz.xy() / la::sum(xyz);
    return xy;
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
bool color_conversion_matrix(float3x3 &M, const Chromaticities &src, const Chromaticities &dst,
                             AdaptationMethod CAT_method = AdaptationMethod_Identity);

// Function to deduce chromaticities and white point from a 3x3 RGB to XYZ matrix
Chromaticities primaries_from_matrix(const float3x3 &rgb_to_XYZ);

template <typename Real>
Real linear_to_sRGB_positive(Real linear)
{
    static constexpr Real inv_gamma = Real(1) / Real(2.4);
    if (linear <= Real(0.0031308))
        return Real(12.92) * linear;
    else
        return Real(1.055) * std::pow(linear, inv_gamma) - Real(0.055);
}

// to/from linear to sRGB
template <typename Real>
Real linear_to_sRGB(Real linear)
{
    return std::copysign(linear_to_sRGB_positive(std::fabs(linear)), linear);
}

template <typename Real>
Real linear_to_gamma_positive(Real linear, Real inv_gamma)
{
    return std::pow(linear, inv_gamma);
}

template <typename Real>
Real linear_to_gamma(Real linear, Real inv_gamma)
{
    return std::copysign(linear_to_gamma_positive(std::fabs(linear), inv_gamma), linear);
}

template <typename Real>
Real sRGB_to_linear_positive(Real sRGB)
{
    if (sRGB < Real(0.04045))
        return (Real(1) / Real(12.92)) * sRGB;
    else
        return std::pow((sRGB + Real(0.055)) * (Real(1) / Real(1.055)), Real(2.4));
}

template <typename Real>
Real sRGB_to_linear(Real sRGB)
{
    return std::copysign(sRGB_to_linear_positive(std::fabs(sRGB)), sRGB);
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

/*! Recommendation ITU-R BT.2100-2 Reference HLG inverse opto-optical transfer function (OOTF^-1).

    Computes the inverse of the HLG OOTF, mapping displayed linear values {R_D, G_D, B_D} in cd/m^2
    back to scene-referred linear values {R_S, G_S, B_S} normalized to [0,1].

    \param E_D Displayed linear value (R_D, G_D, B_D) in cd/m^2
    \param alpha Adjustable user gain (display “contrast”) representing L_W, the nominal peak luminance of achromatic
   pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns Scene-referred linear value {R_S, G_S, B_S} normalized to [0,1].

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
*/
template <typename Real>
inline Real inverse_OOTF_BT2100_HLG(Real E_D, Real Y_D, Real alpha = Real(1.0), Real gamma = Real(1.2))
{
    return (E_D / alpha) * std::pow(Y_D / alpha, Real(1.0) / gamma - Real(1.0));
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

/*! Recommendation ITU-R BT.2100-2 Reference HLG inverse electro-optical transfer function (EOTF^-1).

    Computes the inverse of the HLG EOTF, mapping displayed linear values {R'_D, G'_D, B'_D} in cd/m^2
    back to non-linear HLG signal values {R'_S, G'_S, B'_S} in [0,1].

    \param E_D Displayed linear value (R'_D, G'_D, B'_D) in cd/m^2
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns Non-linear HLG signal value {R'_S, G'_S, B'_S} in [0,1].

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
*/
template <typename Real>
inline Real inverse_EOTF_BT2100_HLG(Real E_D, Real L_B = Real(0), Real L_W = Real(1000), Real gamma = Real(1.2))
{
    const Real alpha = L_W - L_B;
    const Real beta  = L_B != 0 ? std::sqrt(Real(3) * std::pow(L_B / L_W, Real(1) / gamma)) : Real(0);

    return (OETF_BT2100_HLG(inverse_OOTF_BT2100_HLG(E_D, E_D, alpha, gamma)) - beta) / (Real(1) - beta);
}

template <typename Real>
inline la::vec<Real, 3> inverse_EOTF_BT2100_HLG(const la::vec<Real, 3> &E_D, Real L_B = Real(0), Real L_W = Real(1000),
                                                Real gamma = Real(1.2))
{
    const Real alpha = L_W - L_B;
    const Real beta  = L_B != 0 ? std::sqrt(Real(3) * std::pow(L_B / L_W, Real(1) / gamma)) : Real(0);

    Real             Y_D = dot(la::vec<Real, 3>(Real(0.2627), Real(0.6780), Real(0.0593)), E_D);
    la::vec<Real, 3> E_S{inverse_OOTF_BT2100_HLG(E_D[0], Y_D, alpha, gamma),
                         inverse_OOTF_BT2100_HLG(E_D[1], Y_D, alpha, gamma),
                         inverse_OOTF_BT2100_HLG(E_D[2], Y_D, alpha, gamma)};
    auto             E_p{la::apply(OETF_BT2100_HLG<float>, E_S)};
    return (E_p - beta) / (Real(1) - beta);
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

float3 YC_to_RGB(float3 input, float3 Yw);
float3 RGB_to_YC(float3 input, float3 Yw);
Color3 sRGB_to_linear(const Color3 &c);
Color4 sRGB_to_linear(const Color4 &c);
Color3 linear_to_sRGB(const Color3 &c);
Color4 linear_to_sRGB(const Color4 &c);
Color3 linear_to_gamma(const Color3 &c, const Color3 &inv_gamma);
Color4 linear_to_gamma(const Color4 &c, const Color3 &inv_gamma);

inline float from_linear(float linear, const TransferFunction tf)
{
    switch (tf.type)
    {
    case TransferFunction::Unspecified: [[fallthrough]];
    case TransferFunction::sRGB: return linear_to_sRGB(linear);
    case TransferFunction::Gamma: return linear_to_gamma(linear, 1.f / tf.gamma);
    case TransferFunction::ITU: return OETF_ITU(linear);
    case TransferFunction::BT2100_PQ: return inverse_EOTF_BT2100_PQ(linear * 219.f);
    case TransferFunction::BT2100_HLG: return inverse_EOTF_BT2100_HLG(linear * 219.f);
    case TransferFunction::ST240: return OETF_ST240(linear);
    case TransferFunction::Log100: return OETF_log100(linear);
    case TransferFunction::Log100_Sqrt10: return OETF_log100_sqrt10(linear);
    case TransferFunction::IEC61966_2_4: return OETF_IEC61966_2_4(linear);
    case TransferFunction::DCI_P3: return inverse_EOTF_DCI_P3(linear);
    case TransferFunction::Linear: [[fallthrough]];
    default: return linear;
    }
}

inline float3 from_linear(float3 linear, const TransferFunction tf)
{
    switch (tf.type)
    {
    case TransferFunction::Unspecified: [[fallthrough]];
    case TransferFunction::sRGB: return linear_to_sRGB(linear);
    case TransferFunction::Gamma: return linear_to_gamma(linear, float3{1.f / tf.gamma});
    case TransferFunction::ITU: return la::apply(OETF_ITU<float>, linear);
    case TransferFunction::BT2100_PQ: return la::apply(inverse_EOTF_BT2100_PQ<float>, linear * 219.f);
    case TransferFunction::BT2100_HLG: return inverse_EOTF_BT2100_HLG(linear * 219.f);
    case TransferFunction::ST240: return la::apply(OETF_ST240<float>, linear);
    case TransferFunction::Log100: return la::apply(OETF_log100<float>, linear);
    case TransferFunction::Log100_Sqrt10: return la::apply(OETF_log100_sqrt10<float>, linear);
    case TransferFunction::IEC61966_2_4: return la::apply(OETF_IEC61966_2_4<float>, linear);
    case TransferFunction::DCI_P3: return la::apply(inverse_EOTF_DCI_P3<float>, linear);
    case TransferFunction::Linear: [[fallthrough]];
    default: return linear;
    }
}

inline float to_linear(float encoded, const TransferFunction tf)
{
    switch (tf.type)
    {
    case TransferFunction::Unspecified: [[fallthrough]];
    case TransferFunction::sRGB: return sRGB_to_linear(encoded);
    case TransferFunction::Gamma: return linear_to_gamma(encoded, tf.gamma);
    case TransferFunction::ITU: return inverse_OETF_ITU(encoded);
    case TransferFunction::BT2100_PQ: return EOTF_BT2100_PQ(encoded) / 219.f;
    case TransferFunction::BT2100_HLG: return EOTF_BT2100_HLG(encoded) / 219.f;
    case TransferFunction::ST240: return EOTF_ST240(encoded);
    case TransferFunction::Log100: return EOTF_log100(encoded);
    case TransferFunction::Log100_Sqrt10: return EOTF_log100_sqrt10(encoded);
    case TransferFunction::IEC61966_2_4: return EOTF_IEC61966_2_4(encoded);
    case TransferFunction::DCI_P3: return EOTF_DCI_P3(encoded);
    case TransferFunction::Linear: [[fallthrough]];
    default: return encoded;
    }
}

inline float3 to_linear(const float3 &encoded, const TransferFunction tf)
{
    switch (tf.type)
    {
    case TransferFunction::Unspecified: [[fallthrough]];
    case TransferFunction::sRGB: return sRGB_to_linear(encoded);
    case TransferFunction::Gamma: return linear_to_gamma(encoded, float3{tf.gamma});
    case TransferFunction::ITU: return la::apply(inverse_OETF_ITU<float>, encoded);
    case TransferFunction::BT2100_PQ: return la::apply(EOTF_BT2100_PQ<float>, encoded) / 219.f;
    case TransferFunction::BT2100_HLG: return EOTF_BT2100_HLG(encoded) / 219.f;
    case TransferFunction::ST240: return la::apply(EOTF_ST240<float>, encoded);
    case TransferFunction::Log100: return la::apply(EOTF_log100<float>, encoded);
    case TransferFunction::Log100_Sqrt10: return la::apply(EOTF_log100_sqrt10<float>, encoded);
    case TransferFunction::IEC61966_2_4: return la::apply(EOTF_IEC61966_2_4<float>, encoded);
    case TransferFunction::DCI_P3: return la::apply(EOTF_DCI_P3<float>, encoded);
    case TransferFunction::Linear: [[fallthrough]];
    default: return encoded;
    }
}

void to_linear(float *r, float *g, float *b, int num_pixels, int num_channels, TransferFunction tf, int stride = 1);
inline void to_linear(float *pixels, int3 size, TransferFunction tf)
{
    int num_color_channels = size.z >= 3 ? 3 : 1;
    to_linear(pixels, size.z >= 3 ? pixels + 1 : nullptr, size.z >= 3 ? pixels + 2 : nullptr, size.x * size.y,
              num_color_channels, tf, size.z);
}
void from_linear(float *pixels, int3 size, TransferFunction tf);

inline Color3 tonemap(const Color3 color, float gamma, Tonemap_ tonemap_mode, Colormap_ colormap, bool reverse_colormap)
{
    switch (tonemap_mode)
    {
    default: [[fallthrough]];
    case Tonemap_Gamma: return linear_to_gamma(color, float3{1.f / gamma});
    case Tonemap_FalseColor: [[fallthrough]];
    case Tonemap_PositiveNegative:
    {
        auto  xform = tonemap_mode == Tonemap_FalseColor ? float2{1.f, 0.f} : float2{0.5f, 0.5f};
        float avg   = sum(color) * (1.f / 3.f);
        float t     = xform.x * avg + xform.y;
        if (reverse_colormap)
            t = 1.f - t;
        return sRGB_to_linear(float4{Colormap::sample(colormap, t)}.xyz());
    }
    }
}
inline Color4 tonemap(const Color4 color, float gamma, Tonemap_ tonemap_mode, Colormap_ colormap, bool reverse_colormap)
{
    return Color4(tonemap(color.xyz(), gamma, tonemap_mode, colormap, reverse_colormap), color.w);
}

inline float2 blend(float2 top, float2 bottom, BlendMode_ blend_mode)
{
    float diff  = top.x - bottom.x;
    float alpha = top.y + bottom.y * (1.f - top.y);
    switch (blend_mode)
    {
    // case BlendMode_Normal:
    default: return float2(top.x + bottom.x * (1.f - top.y), alpha);
    case BlendMode_Multiply: return float2(top.x * bottom.x, alpha);
    case BlendMode_Divide: return float2(top.x / bottom.x, alpha);
    case BlendMode_Add: return float2(top.x + bottom.x, alpha);
    case BlendMode_Average: return 0.5f * (top + bottom);
    case BlendMode_Subtract: return float2(diff, alpha);
    case BlendMode_Difference: return float2(abs(diff), alpha);
    case BlendMode_Relative_Difference: return float2(abs(diff) / (bottom.x + 0.01f), alpha);
    }
    return float2(0.f);
}

inline float blend(float top, float bottom, BlendMode_ blend_mode)
{
    float diff = top - bottom;
    switch (blend_mode)
    {
    default: [[fallthrough]];
    case BlendMode_Normal: return top;
    case BlendMode_Multiply: return top * bottom;
    case BlendMode_Divide: return top / bottom;
    case BlendMode_Add: return top + bottom;
    case BlendMode_Average: return 0.5f * (top + bottom);
    case BlendMode_Subtract: return diff;
    case BlendMode_Relative_Subtract: return diff / (bottom + 0.01f);
    case BlendMode_Difference: return abs(diff);
    case BlendMode_Relative_Difference: return abs(diff) / (bottom + 0.01f);
    }
    return float(0.f);
}

inline float4 blend(float4 top, float4 bottom, BlendMode_ blend_mode)
{
    float3 diff  = top.xyz() - bottom.xyz();
    float  alpha = top.w + bottom.w * (1.f - top.w);
    switch (blend_mode)
    {
    default: [[fallthrough]];
    case BlendMode_Normal: return float4(top.xyz() + bottom.xyz() * (1.f - top.w), alpha);
    case BlendMode_Multiply: return float4(top.xyz() * bottom.xyz(), alpha);
    case BlendMode_Divide: return float4(top.xyz() / bottom.xyz(), alpha);
    case BlendMode_Add: return float4(top.xyz() + bottom.xyz(), alpha);
    case BlendMode_Average: return 0.5f * (top + bottom);
    case BlendMode_Subtract: return float4(diff, alpha);
    case BlendMode_Relative_Subtract: return float4(diff / (bottom.xyz() + float3(0.01f)), alpha);
    case BlendMode_Difference: return float4(abs(diff), alpha);
    case BlendMode_Relative_Difference: return float4(abs(diff) / (bottom.xyz() + float3(0.01f)), alpha);
    }
    return float4(0.f);
}

//! see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_FULL
template <typename T>
inline float dequantize_full(T v)
{
    constexpr auto min_val   = std::numeric_limits<T>::min();
    constexpr auto max_val   = std::numeric_limits<T>::max();
    const float    inv_range = 1.f / float(max_val - min_val);
    if constexpr (std::is_signed<T>::value)
        // signed normalized, map [min, max] to [-1, 1]
        return (2.f * (float(v) - min_val) * inv_range) - 1.f;
    else
        // unsigned normalized, map [min, max] to [0, 1]
        return (float(v) - min_val) * inv_range;
}

template <>
inline float dequantize_full<float>(float v)
{
    return v;
}

//! see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_FULL
template <typename T>
T quantize_full(float v, int x = 0, int y = 0, bool dither = true)
{
    constexpr auto min_val = std::numeric_limits<T>::min();
    constexpr auto max_val = std::numeric_limits<T>::max();
    const float    range   = float(max_val) - float(min_val);
    if constexpr (std::is_signed<T>::value)
    {
        // signed normalized, map [-1, 1] to [min, max]
        float q = ((v + 1.f) * 0.5f) * range + min_val + (dither ? tent_dither(x, y) : 0.f);
        return (T)std::clamp(q, (float)min_val, (float)max_val);
    }
    else
    {
        // unsigned normalized, map [0, 1] to [min, max]
        float ci = v * max_val;
        // Symmetric triangular distribution on [-1, 1] for general case; uniform distribution on [-0.5,
        // 0.5] when near boundary
        float d =
            (ci - 1.f + 0.5f < 0.0 || ci + 1.f + 0.5f >= max_val + 1.0f) ? box_dither(x, y) : 2.f * tent_dither(x, y);
        return (T)std::clamp(ci + (dither ? d : 0.f) + 0.5f, (float)min_val, (float)max_val);
    }
}

//! see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_NARROW
template <typename T>
inline float dequantize_narrow(T v)
{
    constexpr float denom = 1.f / float(1u << (std::numeric_limits<T>::digits - 8));
    return (v * denom - 16.f) / 219.f;
}

template <>
inline float dequantize_narrow<float>(float v)
{
    return v;
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
//     case 1:  // BT.709, BT.1361-0, IEC 61966-2-1 sRGB or sYCC, IEC 61966-2-4, SMPTE RP 177 Annex B
//     case 6:  // SMPTE 170M
//     case 14: // BT.2020 10-bit
//     case 15: // BT.2020 12-bit
//         return OETF_ITU(linear);
//     case 2:            // Unspecified
//         return linear; // No-op
//     case 3:            // Reserved
//         return linear;
//     case 4: // Gamma 2.2 (IEC 61966-2-1)
//         return linear_to_gamma(linear, 1.0f / 2.2f);
//     case 5: // Gamma 2.8 (ITU-R BT.470 System B G)
//         return linear_to_gamma(linear, 1.0f / 2.8f);
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
//         return linear_to_sRGB(linear);
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
