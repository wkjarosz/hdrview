//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include <vector>

/// A grid of square texels over CIE 1931 xy, fitted to the spectral locus.
/**
    The chromaticity diagram's background texture and the image's chromaticity density are both laid out on
    it, so a texel of one covers exactly a texel of the other. It spans every texel the locus touches plus one
    more on each side, so bilinear filtering fades the locus's edge out within the drawn texture rather than
    cutting it off at the texture's border.

    Texel (0, 0) holds the lowest x and y; rows run toward increasing y.
*/
struct ChromaticityGrid
{
    /// Width and height of a texel in xy.
    static constexpr float texel = 1.f / 256.f;

    int2   size{0};  ///< Texels across and down
    float2 min{0.f}; ///< xy at the outer corner of texel (0, 0)
    float2 max{0.f}; ///< xy at the outer corner of texel (size - 1)

    /// The texel holding \p xy, which lies outside the grid when \p xy does.
    int2 texel_of(float2 xy) const { return int2(la::floor((xy - min) / texel)); }

    /// The xy at the center of texel \p t.
    float2 center(int2 t) const { return min + (float2(t) + 0.5f) * texel; }

    bool contains(int2 t) const { return t.x >= 0 && t.y >= 0 && t.x < size.x && t.y < size.y; }
};

/// The grid, fitted once to spectral_locus().
const ChromaticityGrid &chromaticity_grid();

/// The CIE 1931 spectral locus from 380 to 700 nm, as an xy polygon closed by the line of purples.
const std::vector<float2> &spectral_locus();

/// Whether \p xy lies inside spectral_locus(), the region of physically realizable chromaticities.
bool inside_spectral_locus(float2 xy);
