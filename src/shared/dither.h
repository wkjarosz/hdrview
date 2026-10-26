/** \file dither.h
    The noise HDRView adds before quantizing, compiled into the shaders and into C++ from this one source; see
    shim.h for the dialect it is written in.

    Both read a blue-noise texture of 8-bit ranks, the shaders through a sampler and C++ through
    dither_texture_data(); these turn a rank into the noise.
*/

#ifndef HDRVIEW_SHARED_DITHER_H
#define HDRVIEW_SHARED_DITHER_H

#ifdef __cplusplus
#include "shim.h"
#endif

SHARED_BEGIN

/// Uniform noise in (-0.5, 0.5), centered on zero, from one of the dither texture's ranks, 0 to 255.
SHARED_FN float box_from_rank(float rank) { return (rank + 0.5f) / 256.0f - 0.5f; }

/// Triangular noise in (-0.5, 0.5), peaked at zero, from uniform noise \p box in (-0.5, 0.5).
/**
    Inverts the triangle's distribution function on each side of zero. The sum of two independent uniform
    variables has this distribution, which, at twice this width, leaves quantization error independent of the
    signal in both its mean and its variance.
*/
SHARED_FN float tent_from_box(float box)
{
    const float r = 2.0f * box;
    return 0.5f * copysign(1.0f - sqrt(1.0f - abs(r)), r);
}

SHARED_END

#endif
