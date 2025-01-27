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

// return a map of common color spaces, indexed by name
const std::map<std::string, Imf::Chromaticities> &color_space_chromaticities();

// return a pointer to the chromaticities of a named color space, or nullptr if not found
const Imf::Chromaticities &color_space_chromaticity(const std::string &name);

// return a map of common white points, indexed by name
const std::map<std::string, Imath::V2f> &white_points();

// return a pointer to a named white point, or nullptr if not found
const Imath::V2f &white_point(const std::string &name);

inline const std::vector<std::string> &color_space_names()
{
    // clang-format off
    static const std::vector<std::string> names = {"ACES 2065-1 (Academy Color Encoding System, AP0)",
                                                   "ACEScg (Academy Color Encoding System, AP1)",
                                                   "Adobe RGB (1998)",
                                                   "Apple RGB",
                                                   "Best RGB",
                                                   "Beta RGB",
                                                   "Bruce RGB",
                                                   "BT 2020/2100",
                                                   "CIE RGB",
                                                   "CIE XYZ",
                                                   "ColorMatch RGB",
                                                   "Display P3",
                                                   "Don RGB 4",
                                                   "ECI RGB v2",
                                                   "Ekta Space PS5",
                                                   "NTSC RGB",
                                                   "PAL/SECAM RGB",
                                                   "ProPhoto RGB",
                                                   "SMPTE-C RGB",
                                                   "sRGB/BT 709",
                                                   "Wide Gamut RGB"};
    // clang-format on
    return names;
}

inline const std::vector<std::string> &white_point_names()
{
    static const std::vector<std::string> names = {"C", "D50", "D65", "E"};
    return names;
}

// approximate equality comparison for Imf::Chromaticities
inline bool approx_equal(const Imf::Chromaticities &a, const Imf::Chromaticities &b)
{
    return (a.red - b.red).length2() + (a.green - b.green).length2() + (a.blue - b.blue).length2() +
               (a.white - b.white).length2() <
           1e-6f;
};

/*!
 * @brief		Generic color space conversion
 *
 * Converts from a source color space \a src to a destination color space \a dst,
 * each specified using the \a EColorSpace enumeration
 *
 * @param[in] dst 	Destination color space
 * @param[out] a 	First component of the destination color
 * @param[out] b	Second component of the destination color
 * @param[out] c	Third component of the destination color
 * @param[in] src	Source color space
 * @param[in] A  	First component of the source color
 * @param[in] B  	Second component of the source color
 * @param[in] C  	Third component of the source color
 */
void   convert_colorspace(EColorSpace dst, float *a, float *b, float *c, EColorSpace src, float A, float B, float C);
Color3 convert_colorspace(const Color3 &c, EColorSpace dst, EColorSpace src);
Color4 convert_colorspace(const Color4 &c, EColorSpace dst, EColorSpace src);

template <typename Real>
Real LinearToSRGB_positive(Real linear)
{
    static constexpr Real inv_gamma = Real(1) / Real(2.4);
    if (linear <= Real(0.0031308))
        return 12.92 * linear;
    else
        return Real(1.055) * std::pow(linear, inv_gamma) - Real(0.055);
}

// to/from linear to sRGB and AdobeRGB
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
void   SRGBToLinear(float *r, float *g, float *b);
Color3 SRGBToLinear(const Color3 &c);
Color4 SRGBToLinear(const Color4 &c);
void   LinearToSRGB(float *r, float *g, float *b);
Color3 LinearToSRGB(const Color3 &c);
Color4 LinearToSRGB(const Color4 &c);
Color3 LinearToGamma(const Color3 &c, const Color3 &inv_gamma);
Color4 LinearToGamma(const Color4 &c, const Color3 &inv_gamma);
float  AdobeRGBToLinear(float a);
void   AdobeRGBToLinear(float *r, float *g, float *b);
Color3 AdobeRGBToLinear(const Color3 &c);
Color4 AdobeRGBToLinear(const Color4 &c);
float  LinearToAdobeRGB(float a);
void   LinearToAdobeRGB(float *r, float *g, float *b);
Color3 LinearToAdobeRGB(const Color3 &c);
Color4 LinearToAdobeRGB(const Color4 &c);

// to and from XYZ
void XYZToLinearSRGB(float *R, float *G, float *B, float X, float Y, float z);
void LinearSRGBToXYZ(float *X, float *Y, float *Z, float R, float G, float B);
void XYZToLinearSGray(float *R, float *G, float *B, float X, float Y, float z);
void LinearSGrayToXYZ(float *X, float *Y, float *Z, float R, float G, float B);
void XYZToLinearAdobeRGB(float *R, float *G, float *B, float X, float Y, float z);
void LinearAdobeRGBToXYZ(float *X, float *Y, float *Z, float R, float G, float B);
void XYZToLab(float *L, float *a, float *b, float X, float Y, float Z);
void LabToXYZ(float *X, float *Y, float *Z, float L, float a, float b);
void XYZToLuv(float *L, float *u, float *v, float X, float Y, float Z);
void LuvToXYZ(float *X, float *Y, float *Z, float L, float u, float v);
void XYZToxy(float *x, float *y, float X, float Y, float Z);
void xyYToXZ(float *X, float *Z, float x, float y, float Y);
void XYZToHSL(float *H, float *L, float *S, float X, float Y, float Z);
void HSLToXYZ(float *X, float *Y, float *Z, float H, float L, float S);
void XYZToHSV(float *H, float *S, float *V, float X, float Y, float Z);
void HSVToXYZ(float *X, float *Y, float *Z, float H, float S, float V);

//! Normalize the L,a,b values to each fall within the range [0,1]
void normalizeLab(float *L, float *a, float *b);
//! Take normalized L,a,b values and undo the normalization back to the original range
void unnormalizeLab(float *L, float *a, float *b);

// HLS and HSV are more naturally defined as transformations to/from RGB, so
// define those explicitly
void RGBToHSL(float *H, float *L, float *S, float R, float G, float B);
void HSLToRGB(float *R, float *G, float *B, float H, float L, float S);
void RGBToHSV(float *H, float *S, float *V, float R, float G, float B);
void HSVToRGB(float *R, float *G, float *B, float H, float S, float V);
void HSIAdjust(float *R, float *G, float *B, float h, float s, float i);
void HSLAdjust(float *R, float *G, float *B, float h, float s, float l);
void SatAdjust(float *R, float *G, float *B, float s);

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