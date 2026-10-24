/** \file resample.h
    Reading an image between its samples and past its edges, as the Shift edit does on the CPU and the image shader's
    preview of it does on the GPU, compiled from this one source. See shim.h for the dialect it is written in.

    The two differ only in where the samples come from, so the includer supplies that before including this file:
    - `ResampleValue`, what one sample read gives: one channel's float on the CPU, the four shown channels' vec4 on
      the GPU;
    - `struct ResampleSource`, with at least the field `ivec2 size`, the image's size;
    - `ResampleValue resample_fetch(ResampleSource src, ivec2 p)`, the sample \p p, which is always inside the image.

    The border modes and samplers are the values of BorderMode and Sampler in src/edit/filters.h.
*/

#ifndef HDRVIEW_SHARED_RESAMPLE_H
#define HDRVIEW_SHARED_RESAMPLE_H

#ifdef __cplusplus
#include "shim.h"
#endif

SHARED_BEGIN

// clang-format off
SHARED_CONST int BORDER_BLACK  = 0; ///< Nothing outside; samples read there are zero
SHARED_CONST int BORDER_EDGE   = 1; ///< The nearest sample inside, extended outward
SHARED_CONST int BORDER_REPEAT = 2; ///< The image tiles
SHARED_CONST int BORDER_MIRROR = 3; ///< The image tiles, reflected each time, so no seam appears at the join

SHARED_CONST int SAMPLER_NEAREST  = 0;
SHARED_CONST int SAMPLER_BILINEAR = 1;
SHARED_CONST int SAMPLER_BICUBIC  = 2;
// clang-format on

/// Where \p p lands once border mode \p mode has been applied to an image \p extent samples across, or -1 for nothing.
SHARED_FN int resample_wrap(int p, int extent, int mode)
{
    if (p >= 0 && p < extent)
        return p;
    if (mode == BORDER_EDGE)
        return clamp(p, 0, extent - 1);
    if (mode == BORDER_REPEAT)
        return floor_mod(p, extent);
    if (mode == BORDER_MIRROR)
    {
        // the reflection has period twice the extent and folds in the middle of it, so -1 lands on 0 and extent
        // lands on extent-1
        int q = floor_mod(p, 2 * extent);
        return q < extent ? q : 2 * extent - 1 - q;
    }
    return -1;
}

/// One tap of the interpolating cubic with a = -0.75, as Photoshop's "bicubic".
SHARED_FN float resample_cubic_weight(float d)
{
    float A = -0.75f, x = abs(d);
    return x <= 1.0f ? ((A + 2.0f) * x - (A + 3.0f)) * x * x + 1.0f
                     : ((A * x - 5.0f * A) * x + 8.0f * A) * x - 4.0f * A;
}

/// Sample (\p x, \p y) of \p src read through border modes \p mx and \p my; zero where they say there is nothing.
SHARED_FN ResampleValue resample_bordered(ResampleSource src, int x, int y, int mx, int my)
{
    int u = resample_wrap(x, src.size.x, mx), v = resample_wrap(y, src.size.y, my);
    if (u < 0 || v < 0)
        return ResampleValue(0.0f);
    return resample_fetch(src, ivec2(u, v));
}

/// Value of \p src at \p s, in units of samples with sample (i, j) at (i, j), reconstructed by \p interp.
SHARED_FN ResampleValue resample_at(ResampleSource src, vec2 s, int interp, int mx, int my)
{
    if (interp == SAMPLER_NEAREST)
        return resample_bordered(src, int(floor(s.x + 0.5f)), int(floor(s.y + 0.5f)), mx, my);

    float fx = floor(s.x), fy = floor(s.y);
    float tx = s.x - fx, ty = s.y - fy;
    int   x0 = int(fx), y0 = int(fy);

    if (interp == SAMPLER_BILINEAR)
        return (1.0f - ty) * ((1.0f - tx) * resample_bordered(src, x0, y0, mx, my) +
                              tx * resample_bordered(src, x0 + 1, y0, mx, my)) +
               ty * ((1.0f - tx) * resample_bordered(src, x0, y0 + 1, mx, my) +
                     tx * resample_bordered(src, x0 + 1, y0 + 1, mx, my));

    ResampleValue value = ResampleValue(0.0f);
    float         total = 0.0f;
    for (int j = -1; j <= 2; ++j)
    {
        float wy = resample_cubic_weight(ty - float(j));
        for (int i = -1; i <= 2; ++i)
        {
            float w = resample_cubic_weight(tx - float(i)) * wy;
            value += w * resample_bordered(src, x0 + i, y0 + j, mx, my);
            total += w;
        }
    }
    // the taps sum to one analytically; dividing keeps that true against rounding
    return total != 0.0f ? value / total : value;
}

/// What sample \p p of \p src holds once the image is moved by \p d, read past its edges as \p mx and \p my say.
/**
    Positive offsets move the image right and down. A whole-sample offset reads the sample exactly, whichever
    \p interp, so shifting one way and back returns what was there, and no non-finite neighbor leaks in through a
    weight of zero.
*/
SHARED_FN ResampleValue resample_shifted(ResampleSource src, ivec2 p, vec2 d, int interp, int mx, int my)
{
    // the sample that has to travel to here is the one that far back
    vec2 s = vec2(float(p.x) - d.x, float(p.y) - d.y);
    if (d.x == floor(d.x) && d.y == floor(d.y))
        return resample_bordered(src, int(s.x), int(s.y), mx, my);
    return resample_at(src, s, interp, mx, my);
}

SHARED_END

#endif // HDRVIEW_SHARED_RESAMPLE_H
