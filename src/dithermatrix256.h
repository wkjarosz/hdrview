/*
    dither matrix generated with force-random-dither. This is basically a large
    matrix of size N containing a permutation of the integers between 0 and N-1.
    Used by HDRView for nicely dithering 32-bit floating-point images down to
    8-bits per channel.
*/

#pragma once

#include "shared/dither.h"

#include <cstdint>

void create_dither_texture();
/// The dither texture's width and height, a power of two.
int            dither_texture_width();
const uint8_t *dither_texture_data();

/// Zero-mean dither uniformly distributed in range (-0.5, 0.5).
/**
    Inline, since saving quantizes every sample through it: the texture, made once, is held here, and its width,
    a power of two, wraps \p x and \p y with a mask.
*/
inline float box_dither(int x, int y)
{
    static const uint8_t *data = dither_texture_data();
    static const int      mask = dither_texture_width() - 1;
    return shader::box_from_rank(data[(x & mask) + (y & mask) * (mask + 1)]);
}

/// Zero-mean dither with a triangle-shaped distribution in range (-0.5, 0.5).
inline float tent_dither(int x, int y) { return shader::tent_from_box(box_dither(x, y)); }