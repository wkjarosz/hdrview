//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include <atomic>

/*!
    How far a filter has got, and whether it should stop.

    Passed to the slow filters so a long one can be watched and abandoned. Both members are atomic because
    the filter runs on worker threads while the frame loop reads them to draw the progress bar and to set
    the cancel flag.

    A canceled filter returns whatever it had reached, which the caller must then discard rather than
    apply -- it is a partial answer, not a shorter one.
*/
struct FilterProgress
{
    //! Fraction complete, in [0, 1].
    std::atomic<float> fraction{0.f};
    //! Set from the main thread to ask the filter to stop early.
    std::atomic<bool> canceled{false};

    //! True once cancellation has been requested; filters check this between units of work.
    bool stop() const { return canceled.load(std::memory_order_relaxed); }
    void advance(float f) { fraction.store(f, std::memory_order_relaxed); }
};

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
    would -- the Gaussian is the filter that permits this, being the product of its own 1D form. Exact, in
    the sense that the kernel really is a sampled Gaussian; see fast_gaussian_blurred() for the cheaper
    approximation.

    A sigma of zero along an axis leaves that axis alone.
*/
Array2Df gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y);

/*!
    Average over a box of the given half-widths, \p iterations times.

    Each pass costs the same per sample whatever the half-width: the sum over the box moves one sample at a
    time, adding the sample entering it and subtracting the one leaving. That is the whole reason to reach
    for boxes, and what makes repeating them cheap.

    Repeating widens the blur -- n passes of half-width w carry the variance of n of them -- because this is
    the box blur as an effect in its own right, where that is what was asked for. To approximate a Gaussian
    of a *stated* width instead, use fast_gaussian_blurred().
*/
Array2Df box_blurred(const Array2Df &src, const Box2i &region, int half_width_x, int half_width_y, int iterations = 1);

/*!
    Approximate a Gaussian blur by repeated box blurs, in time independent of \p sigma_x and \p sigma_y.

    Repeated box blurs converge on a Gaussian: the kernel of n of them is the Irwin-Hall distribution, whose
    variance is n times each box's. Solving that for the box width needed to land on a given sigma is what
    lets \p iterations change only how Gaussian the result looks, never how wide it is -- so raising it
    refines the approximation without forcing the blur to be re-tuned.

    Three passes are already hard to tell from a Gaussian; the default of six is what HDRView used before,
    and what Photoshop is generally held to use.
*/
Array2Df fast_gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y,
                               int iterations = 6);

/*!
    Replace each sample with the median of the disc of radius \p radius around it.

    What a blur cannot do: remove a lone bright sample without smearing it into its neighbors. A mean is
    dragged by an outlier however far out it is, while a median ignores it entirely once it is outnumbered
    -- which is why this, not a blur, is what takes fireflies out of a render.

    Unlike the blurs there is no separable form, so this costs the area of the disc per sample and is the
    first filter slow enough to need \p progress. Pass one to have it report and to be able to stop it; a
    canceled run returns what it had reached, which the caller must discard.
*/
Array2Df median_filtered(const Array2Df &src, const Box2i &region, float radius, FilterProgress *progress = nullptr);

/*!
    Add back a multiple of what a blur removed, which sharpens.

    \p amount scales the difference between the samples and their blurred selves: 0 leaves the image alone,
    1 doubles the detail the blur would have taken out. \p sigma sets the size of the detail affected --
    small values sharpen fine texture, large ones raise local contrast.
*/
Array2Df unsharp_masked(const Array2Df &src, const Box2i &region, float sigma, float amount);
