//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"

/*!
    Neighborhood filters over a single channel's samples.

    Separate from the per-sample edits because these read the samples around the one they are writing, so
    they cannot be expressed as a function of one value.

    Each takes the whole channel but computes only \p region, returning an array of just that size. The two
    are distinct on purpose: a filter applied to a selection has to *read* beyond it -- those are real
    samples and they belong in the answer -- but only as far as its kernel reaches, so the work stays
    proportional to the region rather than to the image. Reads past the channel's own edge clamp to the
    nearest sample inside it; blurring against black would darken the border of an image whose content runs
    to it, which is most of them.

    Filtering in place is never right for these, so each returns a new array: a filter that overwrote its
    input as it went would read samples it had already changed.
*/

//! Blur separably with a Gaussian of the given standard deviations, in samples.
/*!
    Two passes, one per axis, which costs O(radius) per sample rather than the O(radius^2) a square kernel
    would -- the Gaussian is the filter that permits this, being the product of its own 1D form.

    A sigma of zero along an axis leaves that axis alone.
*/
Array2Df gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y);

//! Average over a box of the given half-widths, in samples; also separable.
Array2Df box_blurred(const Array2Df &src, const Box2i &region, int half_width_x, int half_width_y);

/*!
    Add back a multiple of what a blur removed, which sharpens.

    \p amount scales the difference between the samples and their blurred selves: 0 leaves the image alone,
    1 doubles the detail the blur would have taken out. \p sigma sets the size of the detail affected --
    small values sharpen fine texture, large ones raise local contrast.
*/
Array2Df unsharp_masked(const Array2Df &src, const Box2i &region, float sigma, float amount);
