/*
    dither matrix generated with force-random-dither. This is basically a large
    matrix of size N containing a permutation of the integers between 0 and N-1.
    Used by HDRView for nicely dithering 32-bit floating-point images down to
    8-bits per channel.
*/

#pragma once

#include <cmath>

void           create_dither_texture();
int            dither_texture_width();
const uint8_t *dither_texture_data();

/// Zero-mean dither uniformly distributed in range (-0.5, 0.5)
float box_dither(int x, int y);

/// Zero-mean dither with a triangle-shaped distribution in range (-0.5,0.5)
float tent_dither(int x, int y);