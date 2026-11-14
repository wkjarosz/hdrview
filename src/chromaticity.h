//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include <cstdint>
#include <vector>

/// A grid of square texels over CIE 1931 xy, on the lattice of texels from xy (0, 0).
/**
    Every grid shares that lattice, so a texel of one covers exactly a texel of another: the chromaticity diagram's
    background, fitted to the spectral locus, and the image's chromaticity density, which reaches past it to the
    image's primaries. Each spans what it covers plus one texel on each side, so bilinear filtering fades the
    content's edge out within the drawn texture rather than cutting it off at the texture's border.

    Texel (0, 0) holds the lowest x and y; rows run toward increasing y.
*/
struct ChromaticityGrid
{
    /// Width and height of a texel in xy.
    /**
        Finer shows the density in more detail, at the cost of more texels and fewer pixels counted in each.
    */
    static constexpr float texel = 1.f / 256.f;

    int2   first{0}; ///< Where texel (0, 0) lies on the lattice
    int2   size{0};  ///< Texels across and down
    float2 min{0.f}; ///< xy at the outer corner of texel (0, 0)
    float2 max{0.f}; ///< xy at the outer corner of texel (size - 1)

    /// The smallest grid holding every xy from \p lo to \p hi, with a texel to spare on each side.
    static ChromaticityGrid covering(float2 lo, float2 hi);

    /// The texel holding \p xy, which lies outside the grid when \p xy does.
    /**
        Clamped to one texel past each edge before converting, since a pixel with a tiny luminance can have a
        chromaticity too far out for an int.
    */
    int2 texel_of(float2 xy) const
    {
        return int2(la::clamp(la::floor((xy - min) / texel), float2{-1.f}, float2(size)));
    }

    /// The xy at the center of texel \p t.
    float2 center(int2 t) const { return min + (float2(t) + 0.5f) * texel; }

    bool contains(int2 t) const { return t.x >= 0 && t.y >= 0 && t.x < size.x && t.y < size.y; }

    /// Whether the center of texel \p t lies inside spectral_locus().
    bool in_locus(int2 t) const;
};

/// Which layers the chromaticity diagram draws, and how; saved with the app's settings.
struct ChromaticityDiagramLayers
{
    bool  density         = true; ///< The image's chromaticity density, fading the colors it holds little of
    bool  gamut           = true; ///< The image's primaries and white point, which can be dragged
    bool  wavelengths     = true; ///< Wavelength ticks along the spectral locus
    bool  daylight        = true; ///< The daylight locus, with ticks at correlated color temperatures
    bool  hovered         = true; ///< The chromaticity of the pixel under the cursor
    float density_opacity = 0.7f; ///< How strongly the density shows, within the locus and beyond it
};

/// The grid over spectral_locus(), on which the chromaticity diagram's background is drawn.
const ChromaticityGrid &chromaticity_grid();

/// The grid over spectral_locus() and the gamut of \p M_RGB_to_XYZ, on which an image's chromaticities are counted.
/**
    A pixel with no negative component lies within the triangle of the primaries whenever the white point does, so
    the grid holds every such pixel of any gamut, imaginary primaries included. It reaches no further than
    xy in [-0.5, 1.5], so a degenerate gamut cannot make it huge; the pixels it misses count as outside.
*/
ChromaticityGrid chromaticity_grid(const float3x3 &M_RGB_to_XYZ);

/// The CIE 1931 spectral locus from 380 to 700 nm, as an xy polygon closed by the line of purples.
const std::vector<float2> &spectral_locus();

/// Whether \p xy lies inside spectral_locus(), the region of physically realizable chromaticities.
bool inside_spectral_locus(float2 xy);
