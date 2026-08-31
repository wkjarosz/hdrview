//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"

/*!
    Neighborhood filters over a single channel's samples.

    Separate from the per-sample edits because these read the samples around the one they are writing, so
    they cannot be expressed as a function of one value. Each returns a new array rather than filtering in
    place, since a filter that overwrote its own input as it went would read samples it had already
    changed.

    All of them clamp at the edges: a sample beyond the border reads as the nearest one inside it. Blurring
    against black would darken the border of an image that runs to its edge, which is most of them.
*/

//! Blur separably with a Gaussian of the given standard deviations, in samples.
/*!
    Two passes, one per axis, which costs O(radius) per sample rather than the O(radius^2) a square kernel
    would -- the Gaussian is the filter that permits this, being the product of its own 1D form.

    A sigma of zero along an axis leaves that axis alone.
*/
Array2Df gaussian_blurred(const Array2Df &src, float sigma_x, float sigma_y);

//! Average over a box of the given half-widths, in samples; also separable.
Array2Df box_blurred(const Array2Df &src, int half_width_x, int half_width_y);

/*!
    Add back a multiple of what a blur removed, which sharpens.

    \p amount scales the difference between the samples and their blurred selves: 0 leaves the image alone,
    1 doubles the detail the blur would have taken out. \p sigma sets the size of the detail affected --
    small values sharpen fine texture, large ones raise local contrast.
*/
Array2Df unsharp_masked(const Array2Df &src, float sigma, float amount);
