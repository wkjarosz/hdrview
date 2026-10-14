/** \file pipeline.h
    The steps the image shader applies to every pixel, compiled into it and into C++ from this one source; see
    shim.h for the dialect it is written in.

    Colors here are premultiplied by alpha, as Image::finalize() stores them. Only the colormap lookup stays
    outside: the shader reads a texture where C++ reads Colormap::sample(), so tonemap() takes the color the
    caller looked up at colormap_coordinate().
*/

#ifndef HDRVIEW_SHARED_PIPELINE_H
#define HDRVIEW_SHARED_PIPELINE_H

#ifdef __cplusplus
#include "colorspace.h"
#include "constants.h"
#endif

SHARED_BEGIN

/// The color channels of an RGBA color.
SHARED_FN vec3 rgb_of(vec4 c) { return vec3(c.x, c.y, c.z); }

/// A color shown opaque, undoing the premultiply when the file stored straight alpha.
/**
    A premultiplied file's alpha=0 pixels carry real color, so only straight alpha is divided back out.
*/
SHARED_FN vec4 isolate(vec4 rgba, bool straight_alpha)
{
    vec3 rgb = rgb_of(rgba);
    return vec4(straight_alpha ? rgb / max(k_small_alpha, rgba.w) : rgb, 1.0f);
}

/// The channels the viewer shows of one image, in sRGB/BT.709 primaries.
/**
    Applied to each image separately, before any blending, so an isolated channel is undone with its own
    file's alpha and comparison blend modes see unpremultiplied values. Channels_Y weighs with sRGB's
    luminance weights, since the colors have already been converted to its primaries.
*/
SHARED_FN vec4 choose_channel(vec4 rgba, Channels_ channel, bool straight_alpha)
{
    switch (channel)
    {
    default:
    case Channels_RGBA: return rgba;
    case Channels_RGB: return isolate(rgba, straight_alpha);
    case Channels_Red: return isolate(vec4(rgba.x, rgba.x, rgba.x, rgba.w), straight_alpha);
    case Channels_Green: return isolate(vec4(rgba.y, rgba.y, rgba.y, rgba.w), straight_alpha);
    case Channels_Blue: return isolate(vec4(rgba.z, rgba.z, rgba.z, rgba.w), straight_alpha);
    case Channels_Alpha: return vec4(rgba.w, rgba.w, rgba.w, 1.0f);
    case Channels_Y: return vec4(vec3(RGB_to_Y(rgb_of(rgba), sRGB_Yw)), rgba.w);
    }
}

/// The current image composited with the reference under one of the comparison modes.
SHARED_FN vec4 blend(vec4 top, vec4 bottom, BlendMode_ mode)
{
    vec3  t     = rgb_of(top);
    vec3  b     = rgb_of(bottom);
    vec3  diff  = t - b;
    float alpha = top.w + bottom.w * (1.0f - top.w);
    switch (mode)
    {
    default:
    case BlendMode_Normal: return vec4(t + b * (1.0f - top.w), alpha);
    case BlendMode_Multiply: return vec4(t * b, alpha);
    case BlendMode_Divide: return vec4(t / b, alpha);
    case BlendMode_Add: return vec4(t + b, alpha);
    case BlendMode_Average: return 0.5f * (top + bottom);
    case BlendMode_Subtract: return vec4(diff, alpha);
    case BlendMode_Relative_Subtract: return vec4(diff / (b + vec3(0.01f)), alpha);
    case BlendMode_Difference: return vec4(abs(diff), alpha);
    case BlendMode_Relative_Difference: return vec4(abs(diff) / (b + vec3(0.01f)), alpha);
    }
}

/// The exposure step: \p gain is 2^exposure, and the offset, a straight-color quantity, enters scaled by alpha.
SHARED_FN vec4 expose(vec4 value, float gain, float offset)
{ return vec4(rgb_of(value) * gain + vec3(offset * value.w), value.w); }

/// Where a color falls along the colormap of a colormap tonemap: 0 at its first color, 1 at its last.
SHARED_FN float colormap_coordinate(vec3 rgb, Tonemap_ mode, bool reverse)
{
    float avg = dot(rgb, vec3(1.0f / 3.0f));
    float t   = mode == Tonemap_PositiveNegative ? 0.5f * avg + 0.5f : avg;
    return reverse ? 1.0f - t : t;
}

/// The tonemap step, given the colormap's stored (sRGB-encoded) color at colormap_coordinate().
/**
    A colormap answers with a straight color while everything around it is premultiplied, so the two
    colormap modes premultiply what it gives; the gamma curve, applied to values that already are, does not.
    The gamma mode ignores \p colormap_sRGB.
*/
SHARED_FN vec4 tonemap(vec4 color, Tonemap_ mode, float gamma, vec3 colormap_sRGB)
{
    if (mode == Tonemap_FalseColor || mode == Tonemap_PositiveNegative)
        return vec4(sRGB_to_linear(colormap_sRGB) * color.w, color.w);
    return vec4(gamma_encode(rgb_of(color), gamma), color.w);
}

SHARED_END

#endif // HDRVIEW_SHARED_PIPELINE_H
