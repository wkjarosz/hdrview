//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "common.h"
#include <cmath>

#include "dithermatrix256.h"

#include <ImfChromaticities.h>
#include <ImfRgbaYca.h>
#include <ImfStandardAttributes.h>

using namespace std;

namespace
{
constexpr float eps   = 216.0f / 24389.0f;
constexpr float kappa = 24389.0f / 27.0f;
constexpr float refX  = 0.950456f;
constexpr float refZ  = 1.08875f;
constexpr float refU  = (4.0f * refX) / (refX + 15.0f + 3.0f * refZ);
constexpr float refV  = 9.0f / (refX + 15.0f + 3.0f * refZ);
// const float minLab[] = {0, -86.1846, -107.864};
// const float maxLab[] = {100, 98.2542, 94.4825};
// use symmetric range so that scaling contrast of chromaticity
// channels to 0 results in neutral gray
constexpr float minLab[] = {0, -128, -128};
constexpr float maxLab[] = {100, 128, 128};

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
    M = M33f{1.f, 0.f, 0.f, //
             0.f, 1.f, 0.f, //
             0.f, 0.f, 1.f};
    return true;

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
float LinearToAdobeRGB(float a) { return pow(a, 1.f / 2.19921875f); }
float AdobeRGBToLinear(float a) { return pow(a, 2.19921875f); }

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
void AdobeRGBToLinear(float *r, float *g, float *b)
{
    *r = AdobeRGBToLinear(*r);
    *g = AdobeRGBToLinear(*g);
    *b = AdobeRGBToLinear(*b);
}
void LinearToAdobeRGB(float *r, float *g, float *b)
{
    *r = LinearToAdobeRGB(*r);
    *g = LinearToAdobeRGB(*g);
    *b = LinearToAdobeRGB(*b);
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

Color3 LinearToAdobeRGB(const Color3 &c)
{
    return Color3(LinearToAdobeRGB(c.x), LinearToAdobeRGB(c.y), LinearToAdobeRGB(c.z));
}

Color4 LinearToAdobeRGB(const Color4 &c) { return Color4(LinearToAdobeRGB(c.xyz()), c.w); }

Color3 AdobeRGBToLinear(const Color3 &c)
{
    return Color3(AdobeRGBToLinear(c.x), AdobeRGBToLinear(c.y), AdobeRGBToLinear(c.z));
}

Color4 AdobeRGBToLinear(const Color4 &c) { return Color4(AdobeRGBToLinear(c.xyz()), c.w); }

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

void XYZToLinearSGray(float *R, float *G, float *B, float X, float Y, float Z)
{
    XYZToLinearSRGB(R, G, B, X, Y, Z);
    *R = *G = *B = (*R + *G + *B) / 3.f;
}

void LinearSGrayToXYZ(float *X, float *Y, float *Z, float R, float G, float B) { LinearSRGBToXYZ(X, Y, Z, R, G, B); }

void XYZToLinearAdobeRGB(float *R, float *G, float *B, float X, float Y, float Z)
{
    *R = X * 2.04159f + Y * -0.56501f + Z * -0.34473f;
    *G = X * -0.96924f + Y * 1.87597f + Z * 0.03342f;
    *B = X * 0.01344f + Y * -0.11836f + Z * 1.34926f;
}

void LinearAdobeRGBToXYZ(float *X, float *Y, float *Z, float R, float G, float B)
{
    *X = R * 0.57667f + G * 0.18556f + B * 0.18823f;
    *Y = R * 0.29734f + G * 0.62736f + B * 0.07529f;
    *Z = R * 0.02703f + G * 0.07069f + B * 0.99134f;
}

void XYZToLab(float *L, float *a, float *b, float X, float Y, float Z)
{
    // normalize for D65 whitepoint
    X *= 1.0f / 0.95047f;
    Z *= 1.0f / 1.08883f;

    X = (X > eps) ? cbrt(X) : (kappa * X + 16.0f) / 116.0f;
    Y = (Y > eps) ? cbrt(Y) : (kappa * Y + 16.0f) / 116.0f;
    Z = (Z > eps) ? cbrt(Z) : (kappa * Z + 16.0f) / 116.0f;

    *L = (116.0f * Y) - 16.f;
    *a = 500.0f * (X - Y);
    *b = 200.0f * (Y - Z);
}

void LabToXYZ(float *X, float *Y, float *Z, float L, float a, float b)
{
    float yr = (L > kappa * eps) ? pow((L + 16.0f) / 116.0f, 3.f) : L / kappa;
    float fy = (yr > eps) ? (L + 16.0f) / 116.0f : (kappa * yr + 16.0f) / 116.0f;
    float fx = a / 500.0f + fy;
    float fz = fy - b / 200.0f;

    float fx3 = pow(fx, 3.f);
    float fz3 = pow(fz, 3.f);

    *X = (fx3 > eps) ? fx3 : (116.0f * fx - 16.0f) / kappa;
    *Y = yr;
    *Z = (fz3 > eps) ? fz3 : (116.0f * fz - 16.0f) / kappa;

    // unnormalize for D65 whitepoint
    *X *= 0.95047f;
    *Z *= 1.08883f;
}

void normalizeLab(float *L, float *a, float *b)
{
    *L -= minLab[0];
    *a -= minLab[1];
    *b -= minLab[2];

    *L /= maxLab[0] - minLab[0];
    *a /= maxLab[1] - minLab[1];
    *b /= maxLab[2] - minLab[2];
}

void unnormalizeLab(float *L, float *a, float *b)
{
    *L *= maxLab[0] - minLab[0];
    *a *= maxLab[1] - minLab[1];
    *b *= maxLab[2] - minLab[2];

    *L += minLab[0];
    *a += minLab[1];
    *b += minLab[2];
}

void XYZToLuv(float *L, float *u, float *v, float X, float Y, float Z)
{
    float denom = 1.0f / (X + 15.0f * Y + 3.0f * Z);
    *u          = (4.0f * X) * denom;
    *v          = (9.0f * Y) * denom;

    *L = (Y > eps) ? (116.0f * cbrtf(Y)) - 16.0f : kappa * Y;
    *u = 13.0f * *L * (*u - refU);
    *v = 13.0f * *L * (*v - refV);
}

void LuvToXYZ(float *X, float *Y, float *Z, float L, float u, float v)
{
    *Y = (L > kappa * eps) ? pow((L + 16.0f) / 116.0f, 3.f) : L / kappa;

    float a = (1.0f / 3.0f) * ((52.0f * L) / (u + 13.0f * L * refU) - 1.0f);
    float b = -5.0f * *Y;
    float d = *Y * ((39.0f * L) / (v + 13.0f * L * refV) - 5.0f);

    *X = (d - b) / (a + (1.0f / 3.0f));
    *Z = *X * a + b;
}

void XYZToxy(float *x, float *y, float X, float Y, float Z)
{
    float denom = X + Y + Z;
    if (denom == 0.0f)
    {
        // set chromaticity to D65 whitepoint
        *x = 0.31271f;
        *y = 0.32902f;
    }
    else
    {
        *x = X / denom;
        *y = Y / denom;
    }
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

//! Convert rgb floats ([0-1],[0-1],[0-1]) to hsv floats ([0-1],[0-1],[0-1])
/*!
    Original method as described in

    Foley et al. Computer Graphics: Principles and Practice.
        Second edition in C. 592-596. July 1997.

    Optimized approach taken from:
        https://web.archive.org/web/20230311213057/http://lolengine.net/blog/2013/01/13/fast-rgb-to-hsv
*/
void RGBToHSV(float *h, float *s, float *v, float r, float g, float b)
{
    float K = 0.f;

    if (g < b)
    {
        swap(g, b);
        K = -1.f;
    }

    if (r < g)
    {
        swap(r, g);
        K = -2.f / 6.f - K;
    }

    float chroma = r - std::min(g, b);
    *h           = fabsf(K + (g - b) / (6.f * chroma + 1e-20f));
    *s           = chroma / (r + 1e-20f);
    *v           = r;
}

//! Convert hsv floats ([0-1],[0-1],[0-1]) to rgb floats ([0-1],[0-1],[0-1])
/*!
    This is derived from sample code in:

    Foley et al. Computer Graphics: Principles and Practice.
        Second edition in C. 592-596. July 1997.
*/
void HSVToRGB(float *r, float *g, float *b, float h, float s, float v)
{
    if (s == 0.0f)
    {
        // achromatic case
        *r = *g = *b = v;
        return;
    }

    h       = fmodf(h, 1.0f) / (60.0f / 360.0f);
    int   i = (int)h;
    float f = h - (float)i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    // clang-format off
    switch (i)
    {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    case 5: default: *r = v; *g = p; *b = q; break;
    }
    // clang-format on
}

//! Convert a color in RGB colorspace to an equivalent color in HSL colorspace.
/*!
    This is derived from sample code in:

    Foley et al. Computer Graphics: Principles and Practice.
        Second edition in C. 592-596. July 1997.

    but extended to support rgb and hsl values outside of the [0..1] range
    in a reasonable way.
*/
void RGBToHSL(float *H, float *S, float *L, float R, float G, float B)
{
    float mn = std::min({R, G, B});
    float mx = std::max({R, G, B});

    // compute lightness: average of min and max rgb values
    float sum  = mn + mx;
    float diff = mx - mn;
    *L         = sum / 2;

    if (diff < 1e-6) // achromatic
    {
        *H = 0;
        *S = 0;
        return;
    }

    // compute saturation
    if (*L <= .5f)
        *S = (mn < 0) ? 1 - mn : diff / sum;
    else
        *S = (mx > 1) ? mx : diff / (2 - sum);

    // compute hue
    if (R == mx)
        *H = (G - B) / diff;
    else if (G == mx)
        *H = (B - R) / diff + 2;
    else
        *H = (R - G) / diff + 4;

    *H *= 1 / 6.;

    if (*H < 0 || *H > 1)
        *H -= floor(*H); // make sure hue is in range 0..1
}

void SatAdjust(float *R, float *G, float *B, float s)
{
    float mn = std::min({*R, *G, *B});
    float mx = std::max({*R, *G, *B});

    if (mn == mx) // achromatic
        return;

    // compute lightness: average of min and max rgb values
    float L = 0.5f * (mn + mx);

    // scale saturation, and find new min/max
    float S, y2;
    if (L <= 0.5f)
    {
        if (mn < 0)
            S = 1 - mn;
        else
            S = (mx - mn) / (mx + mn);
        S *= s;
        if (S > 1)
            y2 = 2 * L + S - 1;
        else
            y2 = L + L * S;
    }
    else
    {
        if (mx > 1)
            S = mx;
        else
            S = (mx - mn) / (2 - (mx + mn));
        S *= s;
        if (S > 1)
            y2 = S;
        else
            y2 = L + S - L * S;
    }
    float x2 = 2 * L - y2;

    // lerp old rgb to new
    float t    = 1.f / (mx - mn);
    float fac1 = (y2 - x2) * t;
    float fac2 = (mx * x2 - mn * y2) * t;
    *R         = *R * fac1 + fac2;
    *G         = *G * fac1 + fac2;
    *B         = *B * fac1 + fac2;
}

inline float HueToRGB(float x, float y, float hue)
{
    if (hue < 0 || hue > 1)
        hue -= floor(hue); // make sure hue is in range 0..1

    if (6 * hue < 1.f)
        return x + 6 * (y - x) * hue;
    if (2 * hue < 1.f)
        return y;
    if (3 * hue < 2.f)
        return x + 6 * (y - x) * (2.0f / 3.0f - hue);
    return x;
}

//! Convert a color in HSL colorspace to an equivalent color in RGB colorspace.
/*!
    This is derived from sample code in:

    Foley et al. Computer Graphics: Principles and Practice.
        Second edition in C. 592-596. July 1997.

    but extended to support rgb and hsl values outside of the [0..1] range
    in a reasonable way.
*/
void HSLToRGB(float *R, float *G, float *B, float H, float S, float L)
{
    float x, y;
    if (S <= 0) // achromatic
    {
        *R = *G = *B = L;
        return;
    }

    // find min/max rgb values
    if (L < 0.5f)
        y = (S > 1) ? 2 * L + S - 1 : L + L * S;
    else
        y = (S > 1) ? S : L + S - L * S;
    x = 2 * L - y;

    // reconstruct rgb from min,max,hue
    *R = HueToRGB(x, y, H + (1 / 3.));
    *G = HueToRGB(x, y, H);
    *B = HueToRGB(x, y, H - (1 / 3.));
}

void HSIAdjust(float *R, float *G, float *B, float h, float s, float i)
{
    if (h == 0 && i == 1)
    {
        SatAdjust(R, G, B, s);
        return;
    }

    float H, S, L;
    RGBToHSL(&H, &S, &L, *R, *G, *B);
    H += h * (1.0 / 360);
    S *= s;
    HSLToRGB(R, G, B, H, S, L);
    *R *= i;
    *G *= i;
    *B *= i;
}

void HSLAdjust(float *R, float *G, float *B, float h, float s, float l)
{
    if (h == 0 && l == 0)
    {
        SatAdjust(R, G, B, s);
        return;
    }

    float H, S, L;
    RGBToHSL(&H, &S, &L, *R, *G, *B);
    H += h * (1.0 / 360);
    S *= s;
    HSLToRGB(R, G, B, H, S, L);

    // now mix with black or white based on the desired lightness
    if (l < 0)
    {
        *R = lerp(*R, 0.f, -l);
        *G = lerp(*G, 0.f, -l);
        *B = lerp(*B, 0.f, -l);
    }
    else
    {
        *R = lerp(*R, 1.f, l);
        *G = lerp(*G, 1.f, l);
        *B = lerp(*B, 1.f, l);
    }
}

void XYZToHSL(float *H, float *S, float *L, float X, float Y, float Z)
{
    float R, G, B;
    XYZToLinearSRGB(&R, &G, &B, X, Y, Z);
    RGBToHSL(H, S, L, R, G, B);
}

void HSLToXYZ(float *X, float *Y, float *Z, float H, float S, float L)
{
    float R, G, B;
    HSLToRGB(&R, &G, &B, H, S, L);
    LinearSRGBToXYZ(X, Y, Z, R, G, B);
}

void XYZToHSV(float *H, float *S, float *V, float X, float Y, float Z)
{
    float R, G, B;
    XYZToLinearSRGB(&R, &G, &B, X, Y, Z);
    RGBToHSV(H, S, V, R, G, B);
}

void HSVToXYZ(float *X, float *Y, float *Z, float H, float S, float V)
{
    float R, G, B;
    HSVToRGB(&R, &G, &B, H, S, V);
    LinearSRGBToXYZ(X, Y, Z, R, G, B);
}

void convert_colorspace(EColorSpace dst, float *a, float *b, float *c, EColorSpace src, float A, float B, float C)
{
    // always convert between the color spaces by way of XYZ to reduce the combinations
    float X, Y, Z;
    switch (src)
    {
    case LinearSRGB_CS: LinearSRGBToXYZ(&X, &Y, &Z, A, B, C); break;
    case LinearSGray_CS: LinearSGrayToXYZ(&X, &Y, &Z, A, B, C); break;
    case LinearAdobeRGB_CS: LinearAdobeRGBToXYZ(&X, &Y, &Z, A, B, C); break;
    case CIELab_CS:
        unnormalizeLab(&A, &B, &C);
        LabToXYZ(&X, &Y, &Z, A, B, C);
        break;
    case CIELuv_CS: LuvToXYZ(&X, &Y, &Z, A, B, C); break;
    case CIExyY_CS:
        xyYToXZ(&X, &Z, A, B, C);
        Y = C;
        break;
    case HLS_CS: HSLToXYZ(&X, &Y, &Z, A, B, C); break;
    case HSV_CS: HSVToXYZ(&X, &Y, &Z, A, B, C); break;
    default:
        X = A;
        Y = B;
        Z = C; // XYZ
    }

    // now convert from XYZ to the destination color space
    switch (dst)
    {
    case LinearSRGB_CS: XYZToLinearSRGB(a, b, c, X, Y, Z); break;
    case LinearSGray_CS: XYZToLinearSGray(a, b, c, X, Y, Z); break;
    case LinearAdobeRGB_CS: XYZToLinearAdobeRGB(a, b, c, X, Y, Z); break;
    case CIELab_CS:
        XYZToLab(a, b, c, X, Y, Z);
        normalizeLab(a, b, c);
        break;
    case CIELuv_CS: XYZToLuv(a, b, c, X, Y, Z); break;
    case CIExyY_CS:
        XYZToxy(a, b, X, Y, Z);
        *c = Y;
        break;
    case HLS_CS: XYZToHSL(a, b, c, X, Y, Z); break;
    case HSV_CS: XYZToHSV(a, b, c, X, Y, Z); break;
    default:
        *a = X;
        *b = Y;
        *c = Z; // XYZ
    }
}

Color3 convert_colorspace(const Color3 &c, EColorSpace dst, EColorSpace src)
{
    Color3 ret;
    convert_colorspace(dst, &ret[0], &ret[1], &ret[2], src, c.x, c.y, c.z);
    return ret;
}

Color4 convert_colorspace(const Color4 &c, EColorSpace dst, EColorSpace src)
{
    Color4 ret = c;
    convert_colorspace(dst, &ret[0], &ret[1], &ret[2], src, c.x, c.y, c.z);
    return ret;
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
