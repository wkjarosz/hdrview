/** \file colorspace.h
    Per-pixel color math that the image shader and the colorpass run, compiled into them and into C++ from
    this one source; see shim.h for the dialect it is written in.

    The templates in src/colorspace.h, which also run in double precision, remain the reference, and
    tests/test_colorspace.cpp holds these functions to them. The odd extensions below test `x < 0` rather
    than multiplying by sign(x), so that a NaN passes through as it does through std::copysign.
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

SHARED_FN float linear_to_sRGB_positive(float a)
{ return a <= 0.0031308f ? 12.92f * a : 1.055f * pow(a, 1.0f / 2.4f) - 0.055f; }

/// The sRGB encoding curve, extended to negative (out-of-gamut) values by odd symmetry.
SHARED_FN float linear_to_sRGB(float a) { return a < 0.0f ? -linear_to_sRGB_positive(-a) : linear_to_sRGB_positive(a); }
SHARED_FN vec3  linear_to_sRGB(vec3 c) { return vec3(linear_to_sRGB(c.x), linear_to_sRGB(c.y), linear_to_sRGB(c.z)); }
SHARED_FN vec4  linear_to_sRGB(vec4 c) { return vec4(linear_to_sRGB(vec3(c.x, c.y, c.z)), c.w); }

SHARED_FN float sRGB_to_linear_positive(float a)
{ return a < 0.04045f ? (1.0f / 12.92f) * a : pow((a + 0.055f) * (1.0f / 1.055f), 2.4f); }

/// The sRGB decoding curve, extended to negative values by odd symmetry.
SHARED_FN float sRGB_to_linear(float a) { return a < 0.0f ? -sRGB_to_linear_positive(-a) : sRGB_to_linear_positive(a); }
SHARED_FN vec3  sRGB_to_linear(vec3 c) { return vec3(sRGB_to_linear(c.x), sRGB_to_linear(c.y), sRGB_to_linear(c.z)); }
SHARED_FN vec4  sRGB_to_linear(vec4 c) { return vec4(sRGB_to_linear(vec3(c.x, c.y, c.z)), c.w); }

/// Luminance of a linear RGB color, given the luminance weights of its primaries.
SHARED_FN float RGB_to_Y(vec3 rgb, vec3 weights) { return dot(weights, rgb); }

/// Luminance weights of linear sRGB/BT.709: the middle row of the classic rounded sRGB-to-XYZ matrix.
SHARED_CONST vec3 sRGB_Yw = vec3(0.212671f, 0.715160f, 0.072169f);

/// The BT.2100 PQ inverse EOTF, from absolute luminance in nits.
/**
    PQ is absolute, so unlike from_linear() this takes nits rather than values relative to a reference white,
    and normalizes by 10000 itself. PQ has no negative representation, so luminance below zero is clipped.
*/
SHARED_FN float pq_encode(float nits)
{
    const float m1  = 2610.0f / 16384.0f;
    const float m2  = 2523.0f * 128.0f / 4096.0f;
    const float c1  = 3424.0f / 4096.0f;
    const float c2  = 2413.0f / 4096.0f * 32.0f;
    const float c3  = 2392.0f / 4096.0f * 32.0f;
    float       Ym1 = pow(max(nits, 0.0f) / 10000.0f, m1);
    return pow((c1 + c2 * Ym1) / (1.0f + c3 * Ym1), m2);
}
SHARED_FN vec3 pq_encode(vec3 nits) { return vec3(pq_encode(nits.x), pq_encode(nits.y), pq_encode(nits.z)); }

/// A plain power curve, x^(1/gamma), extended to negative values by odd symmetry.
SHARED_FN float gamma_encode(float relative, float gamma)
{ return relative < 0.0f ? -pow(-relative, 1.0f / gamma) : pow(relative, 1.0f / gamma); }
SHARED_FN vec3 gamma_encode(vec3 relative, float gamma)
{ return vec3(gamma_encode(relative.x, gamma), gamma_encode(relative.y, gamma), gamma_encode(relative.z, gamma)); }

SHARED_FN float itu_encode_positive(float a)
{
    const float alpha = 1.09929682680944f;
    const float beta  = 0.018053968510807f;
    return a < beta ? 4.5f * a : alpha * pow(a, 0.45f) - (alpha - 1.0f);
}

/// The ITU-T BT.601/BT.709/BT.2020 OETF, extended to negative values by odd symmetry (IEC 61966-2-4, xvYCC).
SHARED_FN float itu_encode(float relative)
{ return relative < 0.0f ? -itu_encode_positive(-relative) : itu_encode_positive(relative); }
SHARED_FN vec3 itu_encode(vec3 relative)
{ return vec3(itu_encode(relative.x), itu_encode(relative.y), itu_encode(relative.z)); }

SHARED_END

#endif // HDRVIEW_SHARED_COLORSPACE_H
