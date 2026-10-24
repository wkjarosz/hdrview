/** \file edits.h
    The point-wise edits, which map a pixel's samples to new ones without regard to their neighbors, as the
    edit commands apply them and the image shader previews them. See shim.h for the dialect it is written in.

    An edit is an op, one of the EDIT_ constants below, with the parameters it reads from two vectors and two
    matrices. A per-sample op maps each of the subject's samples on its own, told which of the subject's
    channels it is; a color op maps a color group's four samples together. Both see the samples as stored:
    premultiplied where alpha means transparency, and in the image's own primaries.

    The ops after the color ops read more than a pixel's own samples, so only the image shader's preview dispatches
    on them, with four integers besides; the edits themselves are shifted(), remapped_envmap(), the bump map's
    filter and irradiance_envmap(), whose parameters shader_edit() packs into these slots.

    tests/test_colorspace.cpp and tests/test_colour_reference.cpp hold the color math to its inverses and to
    colour-science's values.
*/

#ifndef HDRVIEW_SHARED_EDITS_H
#define HDRVIEW_SHARED_EDITS_H

#ifdef __cplusplus
#include "colorspace.h"
#include "constants.h"
#include "shim.h"
#endif

SHARED_BEGIN

// clang-format off
SHARED_CONST int EDIT_NONE                 = 0;

// per-sample ops
SHARED_CONST int EDIT_INVERT               = 1; ///< 1 - v
SHARED_CONST int EDIT_CLAMP                = 2; ///< v clamped to [0, 1]
SHARED_CONST int EDIT_EXPOSURE_GAMMA       = 3; ///< p0: scale, offset, 1/gamma
SHARED_CONST int EDIT_FILL_REPLACE         = 4; ///< p0: the color each slot takes, by slot % 4
SHARED_CONST int EDIT_FILL_BLEND           = 5; ///< p0: the color; p1.x: the alpha slot, or -1

// color ops
SHARED_CONST int EDIT_BRIGHTNESS_CONTRAST  = 6; ///< p0: slope, midpoint, bias, shape; p1.x: what it curves
SHARED_CONST int EDIT_HUE_SATURATION       = 7; ///< p0.x: hue turns; m0: saturation; p0.z: lightness; p1.x: whether premultiplied
SHARED_CONST int EDIT_MATRIX               = 8; ///< m0: the matrix applied to the color
SHARED_CONST int EDIT_FLATTEN              = 9; ///< p0: the straight background composited under the color

/// The first color op: the ops below it map samples one at a time.
SHARED_CONST int EDIT_FIRST_COLOR          = EDIT_BRIGHTNESS_CONTRAST;

// ops that read more than a pixel, previewed only
SHARED_CONST int EDIT_SHIFT                = 10; ///< p0.xy: offset in samples; i0.x: sampler; i0.yz: border modes
SHARED_CONST int EDIT_REMAP                = 11; ///< p0: output size, sigma; m0: rotation; i0: src, dst, sampling, quality
SHARED_CONST int EDIT_BUMP_NORMAL          = 12; ///< p0.xy: bump_normal()'s scale; i0.xy: border modes
SHARED_CONST int EDIT_IRRADIANCE           = 13; ///< m0, m1, p0: the coefficients; p1.xy: output size; i0.x: mapping

// What brightness/contrast curves, in p1.x; the L*a*b* ones take m0 to XYZ, m1 back, and the white in p1.yzw.
SHARED_CONST int CURVE_RGB                 = 0;
SHARED_CONST int CURVE_LIGHTNESS           = 1;
SHARED_CONST int CURVE_CHROMATICITY        = 2;

// The shape of the brightness/contrast curve in p0.w, plus SHAPE_CLIP to clip what it gives to [0, 1].
SHARED_CONST int SHAPE_RATIONAL            = 0;
SHARED_CONST int SHAPE_LINE                = 1;
SHARED_CONST int SHAPE_CLIP                = 2;
// clang-format on

//
// Brightness and contrast
//

/// A straight line through (\p midpoint, 0.5) with slope \p slope, which runs past black and white.
/**
    The pair a brightness/contrast control produces: brightness moves the midpoint by b/2, and contrast sets the
    slope there, from horizontal at -1 (all gray) through the diagonal at 0 to vertical at 1 (black and white). It
    may leave [0, 1], so an HDR sample keeps its relation to its neighbors rather than being clamped.
*/
SHARED_FN float brightness_contrast_linear(float v, float slope, float midpoint)
{ return (v - midpoint) * slope + 0.5f; }

/// Schlick's rational bias, which sends 0.5 to \p a and keeps 0 and 1, from Graphics Gems IV (1994).
SHARED_FN float bias_Schlick(float t, float a) { return t / (((1.0f / a) - 2.0f) * (1.0f - t) + 1.0f); }

/// Schlick's rational gain, an s-curve over [0, 1] of slope (1 - a) / a at its midpoint and a / (1 - a) at its ends.
/**
    From "Fast Alternatives to Perlin's Bias and Gain Functions" (Graphics Gems IV, 1994): each half is bias_Schlick()
    of a doubled input. Unlike Perlin's power gain, its slope at the ends is finite and not zero, so it can be carried
    on past them.
*/
SHARED_FN float gain_Schlick(float t, float a)
{ return t < 0.5f ? 0.5f * bias_Schlick(2.0f * t, a) : 1.0f - 0.5f * bias_Schlick(2.0f - 2.0f * t, a); }

/// Schlick's rational bias then gain, of slope \p slope where it crosses 0.5, carried on past black and white.
/**
    The bias sends (1 - B) / 2, the line's midpoint for a brightness B = 2 bias - 1, to 0.5, with a slope there of
    1 / (4 bias (1 - bias)), so the gain is asked for that much less than \p slope and the two curves meet the
    midpoint at the same slope. A gain's ends have the reciprocal of its middle's slope, so raising the contrast
    flattens them as it steepens the middle. Past black and white the curve, whose two halves are hyperbolas that
    level off or reach a pole there, gives way to straight lines along its slopes at the ends, so its slope is
    continuous everywhere and an HDR sample keeps a share of its difference from its neighbors.
*/
SHARED_FN float brightness_contrast_rational(float v, float slope, float bias)
{
    float b = clamp(bias, 1e-6f, 1.0f - 1e-6f);
    float a = clamp(1.0f / (1.0f + slope * 4.0f * b * (1.0f - b)), 1e-6f, 1.0f - 1e-6f);
    // the slopes at the ends: the gain's, times the bias's there
    float g = a / (1.0f - a);
    if (v < 0.0f)
        return g * b / (1.0f - b) * v;
    if (v > 1.0f)
        return 1.0f + g * (1.0f - b) / b * (v - 1.0f);
    return gain_Schlick(bias_Schlick(v, b), a);
}

//
// CIE 1976 L*a*b*, where equal steps are meant to look equally different: L* is lightness, 100 at the reference
// white, and a* and b* the green-red and blue-yellow opponent axes, which carry none. Relative to a white given as
// XYZ with Y = 1, since "as light as white" is the only meaning L* has.
//

// The CIE's own values for the linear segment near black, not the rounded 0.008856 and 903.3.
SHARED_CONST float k_Lab_eps   = 216.0f / 24389.0f;
SHARED_CONST float k_Lab_kappa = 24389.0f / 27.0f;

// The range normalize_Lab() maps onto [0, 1]: the opponent axes get the widest an eight-bit encoding can reach,
// not the tighter bounds of the sRGB gamut, so nothing real is ever clipped.
SHARED_CONST vec3 k_Lab_min = vec3(0.0f, -128.0f, -128.0f);
SHARED_CONST vec3 k_Lab_max = vec3(100.0f, 128.0f, 128.0f);

/// The cube root with the CIE's linear segment near zero, which keeps the slope finite at black.
SHARED_FN float Lab_f(float t) { return t > k_Lab_eps ? pow(t, 1.0f / 3.0f) : (k_Lab_kappa * t + 16.0f) / 116.0f; }

/// Inverse of Lab_f().
SHARED_FN float Lab_f_inv(float t)
{
    float t3 = t * t * t;
    return t3 > k_Lab_eps ? t3 : (116.0f * t - 16.0f) / k_Lab_kappa;
}

/// L*a*b* of \p XYZ, relative to \p white.
SHARED_FN vec3 XYZ_to_Lab(vec3 XYZ, vec3 white)
{
    float fx = Lab_f(XYZ.x / white.x);
    float fy = Lab_f(XYZ.y / white.y);
    float fz = Lab_f(XYZ.z / white.z);
    return vec3(116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz));
}

/// Inverse of XYZ_to_Lab().
SHARED_FN vec3 Lab_to_XYZ(vec3 Lab, vec3 white)
{
    float fy = (Lab.x + 16.0f) / 116.0f;
    float fx = Lab.y / 500.0f + fy;
    float fz = fy - Lab.z / 200.0f;
    // Y has a closed form of its own: Lab_f_inv() tests the cube, while for Y the threshold is in L
    float yr = Lab.x > k_Lab_kappa * k_Lab_eps ? fy * fy * fy : Lab.x / k_Lab_kappa;
    return vec3(Lab_f_inv(fx) * white.x, yr * white.y, Lab_f_inv(fz) * white.z);
}

/// L*a*b* shifted and scaled into [0, 1]^3, the range brightness_contrast() expects.
SHARED_FN vec3 normalize_Lab(vec3 Lab) { return (Lab - k_Lab_min) / (k_Lab_max - k_Lab_min); }

/// Inverse of normalize_Lab().
SHARED_FN vec3 unnormalize_Lab(vec3 Lab) { return Lab * (k_Lab_max - k_Lab_min) + k_Lab_min; }

//
// Hue, saturation and lightness, with hue a turn in [0, 1)
//

/// The hue of \p rgb, whose largest component is \p mx and range \p C > 0, in turns around the hexcone, unwrapped.
SHARED_FN float hexcone_hue(vec3 rgb, float mx, float C)
{
    // the sextant the largest component names, plus how far around it the other two have moved
    float h = rgb.x == mx ? (rgb.y - rgb.z) / C : rgb.y == mx ? (rgb.z - rgb.x) / C + 2.0f : (rgb.x - rgb.y) / C + 4.0f;
    return h / 6.0f;
}

/// The double hexcone's hue, saturation and lightness of \p rgb, extended to values outside [0, 1].
/**
    From the sample code in Foley et al., Computer Graphics: Principles and Practice, second edition in C, 592-596,
    but for the values an HDR image is full of and the textbook has no answer for. Lightness is the midpoint of the
    smallest and largest component, unclamped; saturation is taken from whichever end has left the unit range,
    directly rather than as a fraction, so a color brighter than white or darker than black survives the round
    trip.
*/
SHARED_FN vec3 RGB_to_HSL(vec3 rgb)
{
    float mn   = min(rgb.x, min(rgb.y, rgb.z));
    float mx   = max(rgb.x, max(rgb.y, rgb.z));
    float sum  = mn + mx;
    float diff = mx - mn;
    float L    = 0.5f * sum;

    // achromatic: there is no hue to name, and no saturation to measure it with
    if (diff < 1e-6f)
        return vec3(0.0f, 0.0f, L);

    // which end of the range the saturation is measured against flips at mid lightness
    float S;
    if (L <= 0.5f)
        S = mn < 0.0f ? 1.0f - mn : diff / sum;
    else
        S = mx > 1.0f ? mx : diff / (2.0f - sum);

    float H = hexcone_hue(rgb, mx, diff);
    if (H < 0.0f || H > 1.0f)
        H -= floor(H);

    return vec3(H, S, L);
}

/// One component back out of the hexcone, given the two extremes \p x and \p y it runs between.
SHARED_FN float hue_to_RGB(float x, float y, float hue)
{
    float h = hue < 0.0f || hue > 1.0f ? hue - floor(hue) : hue;
    if (6.0f * h < 1.0f)
        return x + 6.0f * (y - x) * h;
    if (2.0f * h < 1.0f)
        return y;
    if (3.0f * h < 2.0f)
        return x + 6.0f * (y - x) * (2.0f / 3.0f - h);
    return x;
}

/// Inverse of RGB_to_HSL().
SHARED_FN vec3 HSL_to_RGB(vec3 hsl)
{
    float H = hsl.x, S = hsl.y, L = hsl.z;

    // achromatic, and the hue means nothing
    if (S <= 0.0f)
        return vec3(L, L, L);

    // the extremes the three components run between, undoing the choice RGB_to_HSL() made about which end the
    // saturation was measured against
    float y = L < 0.5f ? (S > 1.0f ? 2.0f * L + S - 1.0f : L + L * S) : (S > 1.0f ? S : L + S - L * S);
    float x = 2.0f * L - y;

    return vec3(hue_to_RGB(x, y, H + 1.0f / 3.0f), hue_to_RGB(x, y, H), hue_to_RGB(x, y, H - 1.0f / 3.0f));
}

/// \p rgb with its hue turned by \p turns around the hexcone, keeping its smallest and largest components.
/**
    Only where the middle component sits between those two changes, so the hue wheel translates by the turn, and a
    color of any brightness, above white or below black, keeps its range. Scaling a color by a positive factor scales
    the result by the same factor, so it commutes with premultiplied alpha. Exactly the color itself for no turn.
*/
SHARED_FN vec3 rotate_hue(vec3 rgb, float turns)
{
    float mn = min(rgb.x, min(rgb.y, rgb.z));
    float mx = max(rgb.x, max(rgb.y, rgb.z));
    float C  = mx - mn;
    // a gray has no hue to turn
    if (turns == 0.0f || C <= 0.0f)
        return rgb;

    float H = hexcone_hue(rgb, mx, C) + turns;

    // the hexcone's pure hue there, between 0 and 1, spread back over the color's own range
    vec3 hue = vec3(hue_to_RGB(0.0f, 1.0f, H + 1.0f / 3.0f), hue_to_RGB(0.0f, 1.0f, H),
                    hue_to_RGB(0.0f, 1.0f, H - 1.0f / 3.0f));
    return vec3(mn, mn, mn) + hue * C;
}

//
// The edits
//

/// The brightness/contrast curve that \p p0 describes, applied to \p v.
SHARED_FN float brightness_contrast(float v, vec4 p0)
{
    int   shape = int(p0.w);
    bool  clip  = shape >= SHAPE_CLIP;
    float y     = (clip ? shape - SHAPE_CLIP : shape) == SHAPE_LINE ? brightness_contrast_linear(v, p0.x, p0.y)
                                                                    : brightness_contrast_rational(v, p0.x, p0.z);
    return clip ? clamp(y, 0.0f, 1.0f) : y;
}

/// Sample \p v of the subject's channel \p slot, as the per-sample op \p op maps it.
SHARED_FN float edit_sample(int op, float v, int slot, vec4 p0, vec4 p1)
{
    if (op == EDIT_INVERT)
        return 1.0f - v;
    // max() first, which takes a NaN to 0 in C++
    if (op == EDIT_CLAMP)
        return min(1.0f, max(0.0f, v));
    // signed: a negative sample is meaningful in an HDR image, and the power is mirrored through the origin
    if (op == EDIT_EXPOSURE_GAMMA)
        return spow(p0.x * v + p0.y, p0.z);
    if (op == EDIT_FILL_REPLACE)
        return p0[slot % 4];
    // source-over: premultiplied storage makes it the same expression for color and alpha alike
    if (op == EDIT_FILL_BLEND)
        return p0.w * (slot == int(p1.x) ? 1.0f : p0[slot % 4]) + (1.0f - p0.w) * v;
    return v;
}

/// A color group's samples \p c, as the color op \p op maps them; alpha is left as it is but by Flatten.
SHARED_FN vec4 edit_color(int op, vec4 c, vec4 p0, vec4 p1, mat3 m0, mat3 m1)
{
    vec3 rgb = vec3(c.x, c.y, c.z);
    if (op == EDIT_BRIGHTNESS_CONTRAST)
    {
        int curve = int(p1.x);
        if (curve == CURVE_RGB)
            return vec4(brightness_contrast(c.x, p0), brightness_contrast(c.y, p0), brightness_contrast(c.z, p0), c.w);

        // through the image's own primaries, since L*a*b* is defined from XYZ
        vec3 white = vec3(p1.y, p1.z, p1.w);
        vec3 lab   = normalize_Lab(XYZ_to_Lab(mul(m0, rgb), white));
        if (curve == CURVE_LIGHTNESS)
            lab = vec3(brightness_contrast(lab.x, p0), lab.y, lab.z);
        else
            lab = vec3(lab.x, brightness_contrast(lab.y, p0), brightness_contrast(lab.z, p0));
        vec3 back = mul(m1, Lab_to_XYZ(unnormalize_Lab(lab), white));
        return vec4(back.x, back.y, back.z, c.w);
    }
    // The hue turned around the hexcone, saturation by m0, then lightness mixes toward black, or toward the brighter
    // of white and the color's own peak, measured on the straight color when premultiplied.
    if (op == EDIT_HUE_SATURATION)
    {
        vec3  mapped = mul(m0, rotate_hue(rgb, p0.x));
        float l      = p0.z;
        if (l < 0.0f)
            mapped = mapped * (1.0f + l);
        else if (l > 0.0f)
        {
            float a     = p1.x != 0.0f ? c.w : 1.0f;
            float peak  = max(mapped.x, max(mapped.y, mapped.z));
            float level = a * max(1.0f, a > k_small_alpha ? peak / a : 0.0f);
            mapped      = mapped + (vec3(level, level, level) - mapped) * l;
        }
        return vec4(mapped.x, mapped.y, mapped.z, c.w);
    }
    // premultiplied alpha is no obstacle: scaling every component by alpha commutes with a matrix
    if (op == EDIT_MATRIX)
    {
        vec3 mapped = mul(m0, rgb);
        return vec4(mapped.x, mapped.y, mapped.z, c.w);
    }
    // the samples are premultiplied, so "over" is an addition rather than a lerp
    if (op == EDIT_FLATTEN)
    {
        float under = p0.w * (1.0f - c.w);
        return vec4(c.x + p0.x * under, c.y + p0.y * under, c.z + p0.z * under, c.w + under);
    }
    return c;
}

//
// Bump to normal map
//

/// The normal of a height field at a sample, from its height \p h there and at the next sample across and down.
/**
    The surface z = h(x, y) has normal (-dh/dx, -dh/dy, 1) with y up, and the image's y runs down, so with green
    pointing up the slope down the image keeps its sign. \p scale scales the slopes across and down, its second
    negated for green pointing down. Stored in [0, 1], where flat is (0.5, 0.5, 1).
*/
SHARED_FN vec3 bump_normal(float h, float h_across, float h_down, vec2 scale)
{ return normalize(vec3(-scale.x * (h_across - h), scale.y * (h_down - h), 1.0f)) * 0.5f + 0.5f; }

SHARED_END

#endif // HDRVIEW_SHARED_EDITS_H
