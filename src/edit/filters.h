//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include "edit/progress.h"

/**
    Neighborhood filters over one channel.

    Each computes only \p region of \p src, reading past it as far as the kernel reaches, and returns a new
    array of region.size(). The blurs clamp at the channel edge; the resampling filters take a BorderMode.
    A canceled filter returns a partial result that the caller must discard.
*/

/// What a filter reads when it reaches past the edge of a channel.
/**
    Chosen per axis, since an image can tile along one and not the other.
*/
enum BorderMode : int
{
    BorderMode_Black = 0, ///< Nothing outside; samples read there are zero
    BorderMode_Edge,      ///< The nearest sample inside, extended outward
    BorderMode_Repeat,    ///< The channel tiles
    BorderMode_Mirror,    ///< The channel tiles, reflected each time, so no seam appears at the join

    BorderMode_COUNT
};

const char *border_mode_name(int mode);

/// Where \p p lands once \p mode has been applied to a channel of extent \p extent, or -1 for nothing.
int wrap_coord(int p, int extent, int mode);

/// How a filter reconstructs between samples.
/**
    All three agree at whole-sample offsets, so an integral offset gives the same result whichever is
    chosen.
*/
enum Sampler : int
{
    Sampler_Nearest = 0,
    Sampler_Bilinear,
    Sampler_Bicubic,

    Sampler_COUNT
};

const char *sampler_name(int sampler);

/// Move the samples by \p dx, \p dy, reading past the edges as \p border_x and \p border_y say.
/**
    Positive offsets move the image right and down. The offsets need not be whole numbers; \p sampler
    reconstructs between samples, and is ignored when both are integral. With BorderMode_Repeat this is the
    wrapping shift, which slides a tiling texture without breaking the tiling.
*/
Array2Df shifted(const Array2Df &src, const Box2i &region, float dx, float dy, int sampler = Sampler_Bilinear,
                 int border_x = BorderMode_Repeat, int border_y = BorderMode_Repeat);

/// Blur separably with a Gaussian of the given standard deviations, in samples; sigma 0 leaves that axis alone.
/**
    O(radius) per sample, with a truly sampled Gaussian kernel; see fast_gaussian_blurred() for the cheaper
    approximation.
*/
Array2Df gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y,
                          AtomicProgress progress = {});

/// Average over a box of the given half-widths, \p iterations times, in time independent of the widths.
/**
    Iterating widens the blur; to approximate a Gaussian of a stated width instead, use
    fast_gaussian_blurred().
*/
Array2Df box_blurred(const Array2Df &src, const Box2i &region, int half_width_x, int half_width_y, int iterations = 1,
                     AtomicProgress progress = {});

/// Approximate a Gaussian blur by repeated box blurs, in time independent of \p sigma_x and \p sigma_y.
/**
    The box widths are solved for \p sigma (see box_widths_for_sigma()), so \p iterations changes only how
    Gaussian the result looks, not how wide it is. Three passes are already hard to tell from a Gaussian.
*/
Array2Df fast_gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y,
                               int iterations = 6, AtomicProgress progress = {});

/// Median over the disc (or square, if !\p disc) of radius \p radius around each sample.
/**
    Removes fireflies without smearing them. No separable form, so O(radius^2) per sample, which is why it
    reports \p progress.
*/
Array2Df median_filtered(const Array2Df &src, const Box2i &region, float radius, bool disc = true,
                         AtomicProgress progress = {});

/// Replace every non-finite sample with the median of its finite neighbors.
/**
    Finite samples are left alone however extreme. \p replacement is used only where a sample has no finite
    neighbor at all.
*/
Array2Df zapped_gremlins(const Array2Df &src, const Box2i &region, float replacement = 0.f);

/// Sharpen by adding back \p amount times what a blur of \p sigma removed.
/**
    An \p amount of 0 leaves the image alone and 1 doubles that detail. Small \p sigma sharpens fine
    texture, large raises local contrast.
*/
Array2Df unsharp_masked(const Array2Df &src, const Box2i &region, float sigma, float amount,
                        AtomicProgress progress = {});
