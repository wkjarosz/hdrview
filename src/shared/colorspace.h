/** \file colorspace.h
    Color math that the shaders and C++ compile from this one source: luminance-chroma decoding and every
    transfer function HDRView reads or writes. See shim.h for the dialect it is written in.

    Float only, since shading languages have no double; tests/test_colorspace.cpp holds these functions to
    values the standards publish, and to their inverses and symmetries.
*/

#ifndef HDRVIEW_SHARED_COLORSPACE_H
#define HDRVIEW_SHARED_COLORSPACE_H

#ifdef __cplusplus
#include "shim.h"
#endif

SHARED_BEGIN

/// Luminance-chroma {RY, Y, BY} to RGB, given the luminance weights Yw of the RGB primaries.
SHARED_FN vec3 YC_to_RGB(vec3 c, vec3 Yw)
{
    // both chroma channels zero: return the luminance itself, which the general formula only rounds toward
    if (c.x == 0.0f && c.z == 0.0f)
        return vec3(c.y, c.y, c.y);

    float r = (c.x + 1.0f) * c.y;
    float b = (c.z + 1.0f) * c.y;
    float g = (c.y - r * Yw.x - b * Yw.z) / Yw.y;
    return vec3(r, g, b);
}

/// Luminance of a linear RGB color, given the luminance weights of its primaries.
SHARED_FN float RGB_to_Y(vec3 rgb, vec3 weights) { return dot(weights, rgb); }

/// Luminance weights of linear sRGB/BT.709: the middle row of the classic rounded sRGB-to-XYZ matrix.
SHARED_CONST vec3 sRGB_Yw = vec3(0.212671f, 0.715160f, 0.072169f);

SHARED_FN float linear_to_sRGB_positive(float linear)
{
    const float inv_gamma = 1.0f / 2.4f;
    if (linear <= 0.0031308f)
        return 12.92f * linear;
    else
        return 1.055f * pow(linear, inv_gamma) - 0.055f;
}

/** Apply \p f to the magnitude of \p v and restore v's sign -- the odd extension of \p f.

    Out-of-gamut color is carried as negative components, so a transfer function has to be mirrored around
    the origin rather than clamped: clamping would silently collapse whichever channels a gamut conversion
    pushed below zero. This is the convention OpenColorIO's LIN_TO_PQ ("negative values are mirrored around
    the origin") and colour-science's signed-power ST 2084 both follow. It also keeps pow, sqrt
    and log away from the negative inputs for which they have no real result.

    A macro rather than a function, since GLSL cannot take \p f as an argument. It tests `v < 0` rather than
    multiplying by sign(v), so that a NaN passes through, and evaluates \p v more than once.
*/
#define SHARED_ODD(f, v) ((v) < 0.0f ? -f(-(v)) : f(v))

/// Signed power: |v| raised to \p p, carrying v's sign. colour-science calls this spow.
SHARED_FN float spow(float v, float p) { return v < 0.0f ? -pow(-v, p) : pow(v, p); }

// to/from linear to sRGB
SHARED_FN float linear_to_sRGB(float linear) { return SHARED_ODD(linear_to_sRGB_positive, linear); }
SHARED_PER_COMPONENT(linear_to_sRGB)
SHARED_FN vec4 linear_to_sRGB(vec4 c) { return vec4(linear_to_sRGB(vec3(c.x, c.y, c.z)), c.w); }

SHARED_FN float linear_to_gamma_positive(float linear, float inv_gamma) { return pow(linear, inv_gamma); }

SHARED_FN float linear_to_gamma(float linear, float inv_gamma)
{ return linear < 0.0f ? -linear_to_gamma_positive(-linear, inv_gamma) : linear_to_gamma_positive(linear, inv_gamma); }
SHARED_FN vec3 linear_to_gamma(vec3 linear, float inv_gamma)
{
    return vec3(linear_to_gamma(linear.x, inv_gamma), linear_to_gamma(linear.y, inv_gamma),
                linear_to_gamma(linear.z, inv_gamma));
}

SHARED_FN float sRGB_to_linear_positive(float sRGB)
{
    if (sRGB < 0.04045f)
        return (1.0f / 12.92f) * sRGB;
    else
        return pow((sRGB + 0.055f) * (1.0f / 1.055f), 2.4f);
}

SHARED_FN float sRGB_to_linear(float sRGB) { return SHARED_ODD(sRGB_to_linear_positive, sRGB); }
SHARED_PER_COMPONENT(sRGB_to_linear)
SHARED_FN vec4 sRGB_to_linear(vec4 c) { return vec4(sRGB_to_linear(vec3(c.x, c.y, c.z)), c.w); }

SHARED_FN float inverse_OETF_ITU(float bt2020)
{
    const float alpha   = 1.09929682680944f;
    const float alpham1 = alpha - 1.0f;
    const float gamma   = 0.081242858298635f; // alpha * pow(beta, 0.45) - (alpha-1)

    if (bt2020 < gamma)
        return bt2020 / 4.5f;
    else
        return pow((bt2020 + alpham1) / alpha, 1.0f / 0.45f);
}
SHARED_PER_COMPONENT(inverse_OETF_ITU)

/** Defines the ITU-T BT.601, BT.709 and BT.2020 opto-electronic transfer function (OETF).

    The opto-electronic transfer function (OETF) in ITU-T BT.601, BT.709 and BT.2020 specifications (for standard
   definition television, HDTV and UHDTV respectively), and SMPTE 170M, which defines NTSC broadcasts.

    \param linear A linear color value R_S, G_S, B_S in the range of [0,1]

    \returns A non-linear color value R'_S, G'_S, B'_S in the range of [0,1]

    see:
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_ITU
*/
SHARED_FN float OETF_ITU(float linear)
{
    const float alpha = 1.09929682680944f;
    const float beta  = 0.018053968510807f;
    if (linear < beta)
        return 4.5f * linear;
    else
        return alpha * pow(linear, 0.45f) - (alpha - 1.0f);
}
SHARED_PER_COMPONENT(OETF_ITU)

/** HDR reference (diffuse) white luminance in cd/m^2, per Recommendation ITU-R BT.2408.

    The PQ and HLG EOTFs produce absolute luminance in cd/m^2, whereas HDRView works in values relative to
    diffuse white. Dividing EOTF output by this constant (and multiplying before the inverse EOTF) maps
    reference white to 1.0.

    see: https://www.itu.int/pub/R-REP-BT.2408
*/
SHARED_CONST float HDR_REFERENCE_WHITE_NITS = 203.0f;

SHARED_FN float EOTF_BT2100_PQ_positive(float a)
{
    const float m1inv = 16384.0f / 2610.0f;
    const float m2inv = 4096.0f / (2523.0f * 128.0f);
    const float c1    = 3424.0f / 4096.0f;         // 0.8359375f;
    const float c2    = 2413.0f / 4096.0f * 32.0f; // 18.8515625f;
    const float c3    = 2392.0f / 4096.0f * 32.0f; // 18.6875f;

    // The inner max() is unrelated to the mirroring: it keeps the PQ numerator from going negative for
    // very small inputs.
    const float a_m2 = pow(a, m2inv);
    return 10000.0f * pow(max(0.0f, a_m2 - c1) / (c2 - c3 * a_m2), m1inv);
}

/** Defines Recommendation ITU-R BT.2100-2 Reference PQ electro-optical transfer function (EOTF).

    \param E_p Denotes a non-linear color value R', G', B' in PQ space.

    \returns Luminance of a displayed linear component R_D, G_D, B_D in cd/m^2

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_PQ
*/
SHARED_FN float EOTF_BT2100_PQ(float E_p) { return SHARED_ODD(EOTF_BT2100_PQ_positive, E_p); }
SHARED_PER_COMPONENT(EOTF_BT2100_PQ)

SHARED_FN float inverse_EOTF_BT2100_PQ_positive(float a)
{
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f * 128.0f / 4096.0f;
    const float c1 = 3424.0f / 4096.0f;         // 0.8359375f;
    const float c2 = 2413.0f / 4096.0f * 32.0f; // 18.8515625f;
    const float c3 = 2392.0f / 4096.0f * 32.0f; // 18.6875f;

    const float Y_m1 = pow(a / 10000.0f, m1);
    return pow((c1 + c2 * Y_m1) / (1.0f + c3 * Y_m1), m2);
}

/** Defines Recommendation ITU-R BT.2100-2 Reference PQ inverse electro-optical transfer function (EOTF^-1).

    \param F_D Luminance of a displayed linear component R_D, G_D, B_D in cd/m^2

    \returns A non-linear color value R', G', B' in PQ space.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_PQ
*/
SHARED_FN float inverse_EOTF_BT2100_PQ(float F_D) { return SHARED_ODD(inverse_EOTF_BT2100_PQ_positive, F_D); }
SHARED_PER_COMPONENT(inverse_EOTF_BT2100_PQ)

SHARED_FN float inverse_OETF_BT2100_HLG_positive(float v)
{
    // Constants defined by the HLG standard
    const float a = 0.17883277f;
    const float b = 0.28466892f; // 1 - 4*a;
    const float c = 0.55991073f; // 0.5 - a * std::log(4 * a)

    return (v < 0.5f) ? (v * v) / 3.0f : (exp((v - c) / a) + b) / 12.0f;
}

/** Recommendation ITU-R BT.2100-2 Reference HLG inverse opto-electronic transfer function (OETF^-1).

    \param x A non-linear color value R', G', B' in HLG space in the range of [0,1]

    \returns The R_S, G_S, B_S color component of linear scene light, normalized to [0,1]

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
SHARED_FN float inverse_OETF_BT2100_HLG(float x)
{
    // Mirrored to match OETF_BT2100_HLG: squaring a negative input would otherwise return a positive
    // value and lose the sign.
    return SHARED_ODD(inverse_OETF_BT2100_HLG_positive, x);
}

SHARED_FN float OETF_BT2100_HLG_positive(float v)
{
    // Constants defined by the HLG standard
    const float a = 0.17883277f;
    const float b = 0.28466892f; // 1 - 4*a;
    const float c = 0.55991073f; // 0.5 - a * std::log(4 * a)

    return (v <= 1.0f / 12.0f) ? sqrt(3.0f * v) : (a * log(12.0f * v - b) + c);
}

/** Recommendation ITU-R BT.2100-2 Reference HLG opto-electronic transfer function (OETF).

    \param E The R_S, G_S, B_S color component of linear scene light, normalized to [0,1]

    \returns The resulting non-linear color value R'_S, G'_S, B'_S in HLG space in the range of [0,1]

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
SHARED_FN float OETF_BT2100_HLG(float E) { return SHARED_ODD(OETF_BT2100_HLG_positive, E); }
SHARED_PER_COMPONENT(OETF_BT2100_HLG)

/** Recommendation ITU-R BT.2100-2 Reference HLG opto-optical transfer function (OOTF).

    \param E_S   Signal for a color component R_S, G_S, B_S proportional to scene linear light normalized to the range
                 [0,1].
    \param Y_S   Normalized scene luminance, defined as: 0.2627 × R_S + 0.6780 × G_S + 0.0593 × B_S
    \param alpha Adjustable user gain (display “contrast”) representing L_W, the nominal peak luminance of achromatic
                 pixels.
    \param gamma System gamma value. 1.2 at the nominal display peak luminance of 1000 cd/m^2.

    \returns Luminance of a displayed linear component {R_D, G_D, or B_D}, in cd/m^2.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
SHARED_FN float OOTF_BT2100_HLG(float E_S, float Y_S, float alpha, float gamma)
{
    // Only the magnitude of the scene luminance scales the result; E_S carries the sign. Mirrors
    // inverse_OOTF_BT2100_HLG.
    return (alpha * pow(abs(Y_S), gamma - 1.0f) * E_S);
}

/** Recommendation ITU-R BT.2100-2 Reference HLG inverse opto-optical transfer function (OOTF^-1).

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
SHARED_FN float inverse_OOTF_BT2100_HLG(float E_D, float Y_D, float alpha, float gamma)
{
    // The luminance only scales the result, so its magnitude is what matters, as colour-science's inverse
    // OOTF also takes. E_D carries the sign; a zero luminance would diverge, the exponent being negative.
    const float Y_abs = abs(Y_D);
    if (!(Y_abs > 0.0f))
        return 0.0f;
    return (E_D / alpha) * pow(Y_abs / alpha, 1.0f / gamma - 1.0f);
}

/** Recommendation ITU-R BT.2100-2 HLG reference system gamma for a given display peak luminance.

    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.

    \returns The system gamma, which is 1.2 at the nominal peak luminance of 1000 cd/m^2.

    see: https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
*/
SHARED_FN float HLG_system_gamma(float L_W) { return 1.2f + 0.42f * log10(L_W / 1000.0f); }

/// The HLG black lift: beta, by which a display with a black level L_B > 0 raises the signal's floor.
SHARED_FN float HLG_beta(float L_B, float L_W, float gamma)
{ return L_B != 0.0f ? sqrt(3.0f * pow(L_B / L_W, 1.0f / gamma)) : 0.0f; }

/** Recommendation ITU-R BT.2100-2 Reference HLG electro-optical transfer function (EOTF).

    \param E_p Denotes a non-linear color value R', G', B' in HLG space.
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.

    \returns Luminance of a displayed linear component {R_D, G_D, or B_D} in cd/m^2.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
SHARED_FN float EOTF_BT2100_HLG(float E_p, float L_B, float L_W)
{
    const float gamma = HLG_system_gamma(L_W);
    const float alpha = L_W - L_B;
    const float beta  = HLG_beta(L_B, L_W, gamma);
    // The black-lift beta applies to the magnitude, so it raises the floor without folding the sign;
    // inverse_OETF_BT2100_HLG is itself mirrored, carrying the sign through the rest of the chain.
    const float E_p_lifted = E_p < 0.0f ? -((1.0f - beta) * -E_p + beta) : (1.0f - beta) * E_p + beta;
    const float E_s        = inverse_OETF_BT2100_HLG(E_p_lifted);
    return OOTF_BT2100_HLG(E_s, E_s, alpha, gamma);
}
/// EOTF_BT2100_HLG() for a display with no black lift and the nominal 1000 cd/m^2 peak.
SHARED_FN float EOTF_BT2100_HLG(float E_p) { return EOTF_BT2100_HLG(E_p, 0.0f, 1000.0f); }

/** Recommendation ITU-R BT.2100-2 Reference HLG electro-optical transfer function (EOTF).

    \param E_p A non-linear color value {R', G', B'} in HLG space.
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.

    \returns Displayed linear color {R_D, G_D, B_D} in cd/m^2.

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
    https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.inline.html#TRANSFER_HLG
*/
SHARED_FN vec3 EOTF_BT2100_HLG(vec3 E_p, float L_B, float L_W)
{
    const float gamma = HLG_system_gamma(L_W);
    const float alpha = L_W - L_B;
    const float beta  = HLG_beta(L_B, L_W, gamma);

    vec3  E_s = vec3(inverse_OETF_BT2100_HLG(max(0.0f, (1.0f - beta) * E_p.x + beta)),
                     inverse_OETF_BT2100_HLG(max(0.0f, (1.0f - beta) * E_p.y + beta)),
                     inverse_OETF_BT2100_HLG(max(0.0f, (1.0f - beta) * E_p.z + beta)));
    float Y_s = dot(vec3(0.2627f, 0.6780f, 0.0593f), E_s);

    return vec3(OOTF_BT2100_HLG(E_s.x, Y_s, alpha, gamma), OOTF_BT2100_HLG(E_s.y, Y_s, alpha, gamma),
                OOTF_BT2100_HLG(E_s.z, Y_s, alpha, gamma));
}
/// EOTF_BT2100_HLG() for a display with no black lift and the nominal 1000 cd/m^2 peak.
SHARED_FN vec3 EOTF_BT2100_HLG(vec3 E_p) { return EOTF_BT2100_HLG(E_p, 0.0f, 1000.0f); }

/** Recommendation ITU-R BT.2100-2 Reference HLG inverse electro-optical transfer function (EOTF^-1).

    Computes the inverse of the HLG EOTF, mapping displayed linear values {R_D, G_D, B_D} in cd/m^2
    back to non-linear HLG signal values {R'_S, G'_S, B'_S} in [0,1].

    \param E_D Displayed linear value (R_D, G_D, B_D) in cd/m^2
    \param L_B Display luminance for black in cd/m^2.
    \param L_W Nominal peak luminance of the display in cd/m^2 for achromatic pixels.

    \returns Non-linear HLG signal value {R'_S, G'_S, B'_S} in [0,1].

    see:
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-E.pdf
*/
SHARED_FN float inverse_EOTF_BT2100_HLG(float E_D, float L_B, float L_W)
{
    const float gamma = HLG_system_gamma(L_W);
    const float alpha = L_W - L_B;
    const float beta  = HLG_beta(L_B, L_W, gamma);

    return (OETF_BT2100_HLG(inverse_OOTF_BT2100_HLG(E_D, E_D, alpha, gamma)) - beta) / (1.0f - beta);
}
/// inverse_EOTF_BT2100_HLG() for a display with no black lift and the nominal 1000 cd/m^2 peak.
SHARED_FN float inverse_EOTF_BT2100_HLG(float E_D) { return inverse_EOTF_BT2100_HLG(E_D, 0.0f, 1000.0f); }

SHARED_FN vec3 inverse_EOTF_BT2100_HLG(vec3 E_D, float L_B, float L_W)
{
    const float gamma = HLG_system_gamma(L_W);
    const float alpha = L_W - L_B;
    const float beta  = HLG_beta(L_B, L_W, gamma);

    float Y_D = dot(vec3(0.2627f, 0.6780f, 0.0593f), E_D);
    vec3  E_S =
        vec3(inverse_OOTF_BT2100_HLG(E_D.x, Y_D, alpha, gamma), inverse_OOTF_BT2100_HLG(E_D.y, Y_D, alpha, gamma),
             inverse_OOTF_BT2100_HLG(E_D.z, Y_D, alpha, gamma));
    vec3 E_p = OETF_BT2100_HLG(E_S);
    return (E_p - beta) / (1.0f - beta);
}
/// inverse_EOTF_BT2100_HLG() for a display with no black lift and the nominal 1000 cd/m^2 peak.
SHARED_FN vec3 inverse_EOTF_BT2100_HLG(vec3 E_D) { return inverse_EOTF_BT2100_HLG(E_D, 0.0f, 1000.0f); }

/** ST240/SMPTE240M OETF (Opto-Electronic Transfer Function).

    Since the OOTF is linear, this function also implements EOTF^-1.
*/
SHARED_FN float OETF_ST240(float linear)
{
    const float alpha = 1.1115f;
    const float beta  = 0.0228f;
    if (linear < beta)
        return 4.0f * linear;
    else
        return alpha * pow(linear, 0.45f) - (alpha - 1.0f);
}
SHARED_PER_COMPONENT(OETF_ST240)

/** ST240/SMPTE240M EOTF (Electro-Optical Transfer Function).

    Since the OOTF is linear, this function also implements OETF^-1.
*/
SHARED_FN float EOTF_ST240(float nonlinear)
{
    const float alpha = 1.1115f;
    const float beta  = 0.0913f;
    if (nonlinear < beta)
        return nonlinear / 4.0f;
    else
        return pow((nonlinear + (alpha - 1.0f)) / alpha, 1.0f / 0.45f);
}
SHARED_PER_COMPONENT(EOTF_ST240)

SHARED_FN float EOTF_log100(float nonlinear)
{ return nonlinear > 0.0f ? exp((nonlinear - 1.0f) * 2.0f * log(10.0f)) : 0.0f; }
SHARED_PER_COMPONENT(EOTF_log100)

SHARED_FN float OETF_log100(float linear) { return linear > 0.0f ? (log(linear) / (2.0f * log(10.0f)) + 1.0f) : 0.0f; }
SHARED_PER_COMPONENT(OETF_log100)

SHARED_FN float EOTF_log100_sqrt10(float nonlinear)
{ return nonlinear > 0.0f ? exp((nonlinear - 1.0f) * 2.5f * log(10.0f)) : 0.0f; }
SHARED_PER_COMPONENT(EOTF_log100_sqrt10)

SHARED_FN float OETF_log100_sqrt10(float linear)
{ return linear > 0.0f ? (log(linear) / (2.5f * log(10.0f)) + 1.0f) : 0.0f; }
SHARED_PER_COMPONENT(OETF_log100_sqrt10)

/** IEC 61966-2-4 OETF (Opto-Electronic Transfer Function).

    Implements the transfer function as specified in IEC 61966-2-4.

    This is simply the ITU OETF which mirrors negative values.

    \param linear Linear input value (R, G, or B channel)
    \returns Non-linear encoded value
*/
SHARED_FN float OETF_IEC61966_2_4(float linear) { return SHARED_ODD(OETF_ITU, linear); }
SHARED_PER_COMPONENT(OETF_IEC61966_2_4)

/** IEC 61966-2-4 EOTF (Electro-Optical Transfer Function).

    Implements the inverse transfer function as specified in IEC 61966-2-4.

    This is simply the ITU inverse OETF which mirrors negative values.

    \param nonlinear Non-linear encoded value (R', G', or B' channel)
    \returns Linear output value
*/
SHARED_FN float EOTF_IEC61966_2_4(float nonlinear) { return SHARED_ODD(inverse_OETF_ITU, nonlinear); }
SHARED_PER_COMPONENT(EOTF_IEC61966_2_4)

/** DCI-P3 EOTF (Electro-Optical Transfer Function).

    Applies the DCI-P3 EOTF as specified in SMPTE ST 428-1 (DCI):
    X = X'^2.6 * 52.37
    Y = Y'^2.6 * 52.37
    Z = Z'^2.6 * 52.37

    \param nonlinear Non-linear encoded value (R', G', or B' channel)
    \returns Linear output value
*/
SHARED_FN float EOTF_DCI_P3(float nonlinear)
{
    // Mirrored around the origin, as for PQ and sRGB.
    return spow(nonlinear, 2.6f) * 52.37f;
}
SHARED_PER_COMPONENT(EOTF_DCI_P3)

/** DCI-P3 inverse EOTF (EOTF^-1).

    Applies the inverse DCI-P3 EOTF as specified in SMPTE ST 428-1 (DCI):
    X' = (X / 52.37)^(1/2.6)
    Y' = (Y / 52.37)^(1/2.6)
    Z' = (Z / 52.37)^(1/2.6)

    \param linear Linear input value (R, G, or B channel)
    \returns Non-linear encoded value
*/
SHARED_FN float inverse_EOTF_DCI_P3(float linear) { return spow(linear / 52.37f, 1.0f / 2.6f); }
SHARED_PER_COMPONENT(inverse_EOTF_DCI_P3)

/// TransferFunction::Type_, as plain ints the shaders can switch on; colorspace.cpp asserts that they agree.
/**
    The colorpass gets the display's transfer function already translated out of wp_color_manager_v1's
    numbering (see display_colorspace.h), so no protocol code points appear here.
*/
SHARED_CONST int TF_UNSPECIFIED   = 0;
SHARED_CONST int TF_LINEAR        = 1;
SHARED_CONST int TF_GAMMA         = 2;
SHARED_CONST int TF_SRGB          = 3;
SHARED_CONST int TF_ITU           = 4;
SHARED_CONST int TF_BT2100_PQ     = 5;
SHARED_CONST int TF_BT2100_HLG    = 6;
SHARED_CONST int TF_ST240         = 7;
SHARED_CONST int TF_LOG100        = 8;
SHARED_CONST int TF_LOG100_SQRT10 = 9;
SHARED_CONST int TF_IEC61966_2_4  = 10;
SHARED_CONST int TF_DCI_P3        = 11;

/// Encodes values relative to reference white with the transfer function \p tf; \p gamma only for TF_GAMMA.
SHARED_FN float from_linear(float linear, int tf, float gamma)
{
    switch (tf)
    {
    case TF_UNSPECIFIED:
    case TF_SRGB: return linear_to_sRGB(linear);
    case TF_GAMMA: return linear_to_gamma(linear, 1.0f / gamma);
    case TF_ITU: return OETF_ITU(linear);
    case TF_BT2100_PQ: return inverse_EOTF_BT2100_PQ(linear * HDR_REFERENCE_WHITE_NITS);
    case TF_BT2100_HLG: return inverse_EOTF_BT2100_HLG(linear * HDR_REFERENCE_WHITE_NITS);
    case TF_ST240: return OETF_ST240(linear);
    case TF_LOG100: return OETF_log100(linear);
    case TF_LOG100_SQRT10: return OETF_log100_sqrt10(linear);
    case TF_IEC61966_2_4: return OETF_IEC61966_2_4(linear);
    case TF_DCI_P3: return inverse_EOTF_DCI_P3(linear);
    case TF_LINEAR:
    default: return linear;
    }
}

/// from_linear() for a color, which HLG encodes as a whole: its OOTF depends on the color's luminance.
SHARED_FN vec3 from_linear(vec3 linear, int tf, float gamma)
{
    switch (tf)
    {
    case TF_UNSPECIFIED:
    case TF_SRGB: return linear_to_sRGB(linear);
    case TF_GAMMA: return linear_to_gamma(linear, 1.0f / gamma);
    case TF_ITU: return OETF_ITU(linear);
    case TF_BT2100_PQ: return inverse_EOTF_BT2100_PQ(linear * HDR_REFERENCE_WHITE_NITS);
    case TF_BT2100_HLG: return inverse_EOTF_BT2100_HLG(linear * HDR_REFERENCE_WHITE_NITS);
    case TF_ST240: return OETF_ST240(linear);
    case TF_LOG100: return OETF_log100(linear);
    case TF_LOG100_SQRT10: return OETF_log100_sqrt10(linear);
    case TF_IEC61966_2_4: return OETF_IEC61966_2_4(linear);
    case TF_DCI_P3: return inverse_EOTF_DCI_P3(linear);
    case TF_LINEAR:
    default: return linear;
    }
}

/// Decodes values encoded with the transfer function \p tf to values relative to reference white.
SHARED_FN float to_linear(float encoded, int tf, float gamma)
{
    switch (tf)
    {
    case TF_UNSPECIFIED:
    case TF_SRGB: return sRGB_to_linear(encoded);
    case TF_GAMMA: return linear_to_gamma(encoded, gamma);
    case TF_ITU: return inverse_OETF_ITU(encoded);
    case TF_BT2100_PQ: return EOTF_BT2100_PQ(encoded) / HDR_REFERENCE_WHITE_NITS;
    case TF_BT2100_HLG: return EOTF_BT2100_HLG(encoded) / HDR_REFERENCE_WHITE_NITS;
    case TF_ST240: return EOTF_ST240(encoded);
    case TF_LOG100: return EOTF_log100(encoded);
    case TF_LOG100_SQRT10: return EOTF_log100_sqrt10(encoded);
    case TF_IEC61966_2_4: return EOTF_IEC61966_2_4(encoded);
    case TF_DCI_P3: return EOTF_DCI_P3(encoded);
    case TF_LINEAR:
    default: return encoded;
    }
}

/// to_linear() for a color, which HLG decodes as a whole: its OOTF depends on the color's luminance.
SHARED_FN vec3 to_linear(vec3 encoded, int tf, float gamma)
{
    switch (tf)
    {
    case TF_UNSPECIFIED:
    case TF_SRGB: return sRGB_to_linear(encoded);
    case TF_GAMMA: return linear_to_gamma(encoded, gamma);
    case TF_ITU: return inverse_OETF_ITU(encoded);
    case TF_BT2100_PQ: return EOTF_BT2100_PQ(encoded) / HDR_REFERENCE_WHITE_NITS;
    case TF_BT2100_HLG: return EOTF_BT2100_HLG(encoded) / HDR_REFERENCE_WHITE_NITS;
    case TF_ST240: return EOTF_ST240(encoded);
    case TF_LOG100: return EOTF_log100(encoded);
    case TF_LOG100_SQRT10: return EOTF_log100_sqrt10(encoded);
    case TF_IEC61966_2_4: return EOTF_IEC61966_2_4(encoded);
    case TF_DCI_P3: return EOTF_DCI_P3(encoded);
    case TF_LINEAR:
    default: return encoded;
    }
}

SHARED_END

#endif // HDRVIEW_SHARED_COLORSPACE_H
