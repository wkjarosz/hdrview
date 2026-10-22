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
    return vec4(linear_to_gamma(rgb_of(color), 1.0f / gamma), color.w);
}

/// Where the highlight rolloff's curve leaves the identity, in stops above SDR white; see rolloff_stops().
/**
    Half the gap between \p peak and \p ceiling below the ceiling: where BT.2390's EETF (now in BT.2408 Annex 5)
    puts it, at 1.5 times the target less half the source peak in PQ, and the highest a Hermite leaving the knee at
    slope 1 can start and stay monotone. Taken in stops of SDR white rather than PQ, it lands within a tenth of a
    stop of the same place for the headrooms and peaks displays have, and needs no absolute luminance. Without a
    peak, or with one that fits under the ceiling, there is nothing to roll off, and the knee is out of reach.
*/
SHARED_FN float rolloff_knee(float ceiling, bool has_peak, float peak)
{
    if (!has_peak || peak <= ceiling)
        return 3.0e38f;
    return ceiling - 0.5f * (peak - ceiling);
}

/// The highlight rolloff's curve, BT.2390's EETF, from and to stops of display light above SDR white.
/**
    Stops below rolloff_knee() pass unchanged; above it a cubic Hermite leaves the knee at slope 1, so there is no
    kink, and carries \p peak exactly onto \p ceiling, arriving flat. Nothing ends above the ceiling, even with no
    rolloff needed, which keeps the curve continuous as the peak crosses the ceiling.
*/
SHARED_FN float rolloff_stops(float stops, float ceiling, bool has_peak, float peak)
{
    float knee = rolloff_knee(ceiling, has_peak, peak);
    if (stops <= knee)
        return min(stops, ceiling);
    if (stops >= peak)
        return ceiling;
    float span = peak - knee;
    float t    = (stops - knee) / span;
    float t2   = t * t;
    float t3   = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * knee + (t3 - 2.0f * t2 + t) * span + (3.0f * t2 - 2.0f * t3) * ceiling;
}

/// A display-linear value through rolloff_stops(), returned bit for bit where the curve is the identity.
SHARED_FN float roll_off_value(float v, float ceiling, bool has_peak, float peak)
{
    if (v <= 0.0f)
        return v;
    float stops  = log2(v);
    float rolled = rolloff_stops(stops, ceiling, has_peak, peak);
    return rolled >= stops ? v : exp2(rolled);
}

/// The highlight rolloff of a tonemapped color, keeping its hue to the degree \p preserve_hue asks.
/**
    At \p preserve_hue 1 the curve takes the brightest channel and the others are scaled by the same ratio, which
    keeps hue and saturation; at 0 each channel goes through the curve on its own, so bright channels are
    compressed more and saturated highlights shift toward white, as a film shoulder does. Between, the two results
    are mixed, as BT.2408 Annex 5 suggests for controlling desaturation; either keeps every channel under the
    ceiling, so any mix does too. \p color is display-linear and premultiplied, so the curve applies to the
    straight color. Only the gamma tonemap is rolled off: a colormap's colors are a legend for the data.
*/
SHARED_FN vec4 roll_off(vec4 color, Tonemap_ mode, float preserve_hue, float ceiling, bool has_peak, float peak)
{
    if (mode != Tonemap_Gamma)
        return color;
    float a         = color.w > k_small_alpha ? color.w : 1.0f;
    vec3  straight  = rgb_of(color) / a;
    float brightest = max(straight.x, max(straight.y, straight.z));
    float rolled    = roll_off_value(brightest, ceiling, has_peak, peak);
    // below the knee, where the brightest channel is, so is every other
    if (rolled == brightest)
        return color;
    vec3 together = straight * (rolled / brightest);
    vec3 each =
        vec3(roll_off_value(straight.x, ceiling, has_peak, peak), roll_off_value(straight.y, ceiling, has_peak, peak),
             roll_off_value(straight.z, ceiling, has_peak, peak));
    return vec4((each + (together - each) * preserve_hue) * a, color.w);
}

/// \p color, premultiplied, with each channel of its straight color clipped at \p level.
SHARED_FN vec4 clip_highlights(vec4 color, float level)
{
    float a = color.w > k_small_alpha ? color.w : 1.0f;
    return vec4(min(color.x, level * a), min(color.y, level * a), min(color.z, level * a), color.w);
}

// What a save dialog's preview has the viewport show: nothing different, the data as a file keeps it, or the
// rendition as one keeps it
SHARED_CONST int PREVIEW_OFF       = 0;
SHARED_CONST int PREVIEW_DATA      = 1; ///< Save as: the current image's values, before the view's own steps
SHARED_CONST int PREVIEW_RENDITION = 2; ///< Export rendition: the view, its highlights handled for the export's target

/// \p color, premultiplied and in BT.709's primaries, as a file keeps it.
/**
    Unpremultiplied, taken into the file's primaries by \p to_file, clipped as encoding_clip() says and at \p ceiling,
    and brought back by \p from_file. A color under zero alpha is left alone: the file keeps no color there.
*/
SHARED_FN vec4 kept_by_file(vec4 color, mat3 to_file, mat3 from_file, int clip, int tf, float gamma, float ceiling)
{
    if (color.w <= 0.0f)
        return color;
    vec3 rgb = encoding_clip(mul(to_file, rgb_of(color) / color.w), clip, tf, gamma);
    rgb      = mul(from_file, vec3(min(rgb.x, ceiling), min(rgb.y, ceiling), min(rgb.z, ceiling))) * color.w;
    return vec4(rgb, color.w);
}

SHARED_END

#endif // HDRVIEW_SHARED_PIPELINE_H
