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
#include "shared/edits.h"
#include "shared/pipeline.h"
#include <cmath>
#include <string>
#include <vector>

using TransparencyType_ = int;
enum TransparencyType : TransparencyType_
{
    TransparencyType_None = 0,
    TransparencyType_PremultipliedLinear,
    TransparencyType_PremultipliedNonLinear, // values are premultiplied in e.g. gamma- or sRGB-encoded space
    TransparencyType_Straight,
    TransparencyType_Count
};

const char *transparency_type_name(TransparencyType_ at);
/// Name for the alpha-override combo, which needs to say more than the metadata panel's one-word label.
const char  *transparency_override_name(TransparencyType_ at);
const char **transparency_type_names();

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

    bool valid() const
    {
        return (red.x >= 0 && red.y >= 0 && green.x >= 0 && green.y >= 0 && blue.x >= 0 && blue.y >= 0 &&
                white.x >= 0 && white.y >= 0);
    }

    static Chromaticities Invalid() { return {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}; }
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
    WhitePoint_Unspecified, ///< unspecified, assuming D65
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

/// Returns a description of the ColorGamut enum value.
const char  *color_gamut_name(const ColorGamut_ primaries);
const char **color_gamut_names();

/// The Chromaticities of one of the predefined color primaries; throws if the primaries are not recognized.
Chromaticities gamut_chromaticities(ColorGamut_ primaries);
Chromaticities chromaticities_from_CICP(int cp);
// Returns -1 if the chromaticities do not match one of the predefined CICP values
int         chromaticities_to_CICP(const Chromaticities &chr);
ColorGamut_ named_color_gamut(const Chromaticities &chr);

/// Smallest gamma that still describes a power curve.
/**
    It is inverted before use, so zero divides by zero and a negative value sends a black pixel to
    infinity; nothing above this needs a bound.
*/
constexpr float MIN_GAMMA = 1e-4f;

struct TransferFunction
{
    using Type = int;
    enum Type_ : Type
    {
        Unspecified = 0,
        Linear,
        Gamma,
        sRGB,
        ITU, ///< ITU-T BT.601, BT.709 and BT.2020 specifications, and SMPTE 170M
        BT2100_PQ,
        BT2100_HLG,
        ST240,
        Log100,
        Log100_Sqrt10,
        IEC61966_2_4,
        DCI_P3,
        Count,
        Invalid = Count
    };

    Type_ type  = Unspecified;
    float gamma = 2.2f; // Gamma always refers to the exponent of the to_linear transform, only used if type ==
                        // TransferFunction::Gamma

    TransferFunction(Type_ t, float g = 2.2f) : type(t), gamma(g) {}
};

std::string      transfer_function_name(TransferFunction tf);
TransferFunction transfer_function_from_CICP(int cicp);
int              transfer_function_to_CICP(TransferFunction tf);

std::string color_profile_name(ColorGamut_ gamut, TransferFunction tf);
std::string color_profile_name(const Chromaticities &chroma, TransferFunction tf);

using AdaptationMethod_ = int;
enum AdaptationMethod : AdaptationMethod_
{
    AdaptationMethod_Identity = 0, ///< Identity CAT
    AdaptationMethod_XYZScaling,   ///< XYZ scaling CAT
    AdaptationMethod_Bradford,     ///< Bradford CAT
    AdaptationMethod_VonKries,     ///< Von Kries CAT
    AdaptationMethod_Count
};

const char *adaptation_method_name(AdaptationMethod method);

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
    float          min_wavelength = 380.f; ///< minimum wavelength in nm
    float          max_wavelength = 830.f; ///< maximum wavelength in nm

    /// Return the location of the spanning bins (x,y components), and the lerp factor between them (z)
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

/** Compute Planckian locus from correlated color temperature (CCT) in Kelvin.

    Based on Kim et al. (2002), valid for 1667K <= T <= 25000K.

    See https://en.wikipedia.org/wiki/Planckian_locus#Approximation
*/
float2 Kelvin_to_xy(float T);
/** Compute CIE daylight chromaticity coordinates (xD, yD) from correlated color temperature (CCT) in Kelvin

    See https://en.wikipedia.org/wiki/Standard_illuminant#Computation
*/
float2                           daylight_to_xy(float T);
const TabulatedSpectrum<float>  &white_point_spectrum(WhitePoint_ wp = WhitePoint_D65);
const TabulatedSpectrum<float3> &CIE_XYZ_spectra();

/** Convert a wavelength in nanometers to a CIE 1931 chromaticity value.

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

/**
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

// The transfer functions, which the shaders share, are in src/shared/colorspace.h.

float3 RGB_to_YC(float3 input, float3 Yw);

// The point-wise edits' color math, which the shaders share (src/shared/edits.h): hue, saturation and lightness,
// L*a*b*, and the brightness/contrast curves.
using shader::adjust_HSL;
using shader::bias_Schlick;
using shader::brightness_contrast_linear;
using shader::brightness_contrast_nonlinear;
using shader::gain_Perlin;
using shader::HSL_to_RGB;
using shader::Lab_to_XYZ;
using shader::normalize_Lab;
using shader::RGB_to_HSL;
using shader::unnormalize_Lab;
using shader::XYZ_to_Lab;

/// A hue turned by \p turns and a saturation scaled by \p saturation, as one matrix that keeps gray and luminance.
/**
    The turn is Haeberli's, from "Matrix Operations for Image Processing" (1993), with the image's own luminance
    weights \p Yw: a rotation about the gray axis, carried onto the plane of constant luminance, so that
    H = 1 Yw^T + (I - 1 Yw^T)(R - 1 1^T / 3) fixes 1 and keeps Yw^T. The saturation blends toward or away from the gray
    of the same luminance, 1 Yw^T + s (I - 1 Yw^T), which acts on that plane alone and so commutes with the turn. Both
    are linear, so they mean the same above white and below black, and commute with premultiplied alpha.
*/
float3x3 hue_saturation_matrix(float turns, float saturation, float3 Yw);

/// XYZ for a chromaticity, scaled so Y is one: the form XYZ_to_Lab() wants a white in.
inline float3 XYZ_from_xy(float2 xy)
{
    return xy.y > 0.f ? float3{xy.x / xy.y, 1.f, (1.f - xy.x - xy.y) / xy.y} : float3{1.f, 1.f, 1.f};
}

/// The white L*a*b* is taken relative to when an image names none: D65's.
inline float3 Lab_reference_white() { return XYZ_from_xy(white_point(WhitePoint_D65)); }

// The shaders' own functions (src/shared/colorspace.h), so that a color computed here matches the one on screen
using shader::HDR_REFERENCE_WHITE_NITS;
using shader::linear_to_sRGB;
using shader::spow;
using shader::sRGB_to_linear;
using shader::YC_to_RGB;

float  from_linear(float linear, const TransferFunction tf);
float3 from_linear(float3 linear, const TransferFunction tf);
float  to_linear(float encoded, const TransferFunction tf);
float3 to_linear(const float3 &encoded, const TransferFunction tf);

void to_linear(float *r, float *g, float *b, int num_pixels, int num_channels, TransferFunction tf, int stride = 1);
inline void to_linear(float *pixels, int3 size, TransferFunction tf)
{
    int num_color_channels = size.z >= 3 ? 3 : 1;
    to_linear(pixels, size.z >= 3 ? pixels + 1 : nullptr, size.z >= 3 ? pixels + 2 : nullptr, size.x * size.y,
              num_color_channels, tf, size.z);
}
void from_linear(float *pixels, int3 size, TransferFunction tf);

void convert_primaries(float *pixels, int3 size, const Chromaticities &src, const Chromaticities &dst,
                       AdaptationMethod m = AdaptationMethod_Bradford);

/// The image shader's tonemap step, with the colormap read through Colormap::sample() instead of a texture.
Color4 tonemap(Color4 color, float gamma, Tonemap_ tonemap_mode, Colormap_ colormap, bool reverse_colormap);

float2 blend(float2 top, float2 bottom, BlendMode_ blend_mode);

// inline, unlike its float2 sibling: called per pixel from PixelStats::calculate(), in another translation
// unit, and the project builds without LTO.
inline float blend(float top, float bottom, BlendMode_ blend_mode)
{
    using std::abs; // ::abs is the integer one on libstdc++, and would truncate

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
    // return float(0.f);
}

// the image shader's own, for RGBA (src/shared/pipeline.h); the overloads above are for single channels
using shader::blend;

/// see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_FULL
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

/// see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_FULL
template <typename T>
T quantize_full(float v, int x = 0, int y = 0, bool dither = true)
{
    constexpr auto min_val = std::numeric_limits<T>::min();
    constexpr auto max_val = std::numeric_limits<T>::max();
    const float    range   = float(max_val) - float(min_val);
    // std::clamp lets a NaN through, both of its comparisons being false for one, and converting that to an
    // integer type is undefined.
    if constexpr (!std::is_floating_point_v<T>)
        if (std::isnan(v))
            v = 0.f;
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

/// see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_NARROW
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

/// see https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#QUANTIZATION_NARROW
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

// /**\brief Applies the transfer function as defined in Table 3 of ITU-T H.273 for the given TransferCharacteristics
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
