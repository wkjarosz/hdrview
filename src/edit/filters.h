//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include "edit/progress.h"

/*!
    Neighborhood filters over a single channel's samples.

    Separate from the per-sample edits because these read the samples around the one they are writing, so
    they cannot be expressed as a function of one value.

    Each takes the whole channel but computes only \p region, returning an array of just that size. The two
    are distinct on purpose: a filter applied to a selection has to *read* beyond it -- those are real
    samples and they belong in the answer -- but only as far as its kernel reaches, so the work stays
    proportional to the region rather than to the image. The blurs clamp reads past the channel's own edge
    to the nearest sample inside it, since blurring against black would darken the border of an image whose
    content runs to it; the ones that resample take a border mode instead, and say why below.

    Filtering in place is never right for these, so each returns a new array: a filter that overwrote its
    input as it went would read samples it had already changed.

    Each takes a AtomicProgress by value. Reporting costs a filter three lines -- say how many steps it
    has, increment once per step, and check whether it has been asked to stop -- and no branching on
    whether anyone is listening, since a default-constructed one is inert. A filter asked to stop returns
    whatever it had reached, which the caller must discard rather than apply.
*/

//! What a filter reads when it reaches past the edge of a channel.
/*!
    Only the filters that resample need this. The blurs clamp unconditionally, since blurring against
    black would darken the border of an image whose content runs to it, which is most of them -- but a
    shift has no such default: a tiling texture wants to wrap, a photograph wants to run off into nothing,
    and only the person shifting it knows which it is.

    Chosen per axis, because an image can tile along one and not the other -- a lat-long environment map
    wraps in longitude and does not in latitude.
*/
enum BorderMode : int
{
    BorderMode_Black = 0, //!< Nothing outside; samples read there are zero
    BorderMode_Edge,      //!< The nearest sample inside, extended outward
    BorderMode_Repeat,    //!< The channel tiles
    BorderMode_Mirror,    //!< The channel tiles, reflected each time, so no seam appears at the join

    BorderMode_COUNT
};

const char *border_mode_name(int mode);

//! How a filter reads between samples.
/*!
    A shift by a whole number of samples never asks, and then all three agree. Anything finer has to
    reconstruct: nearest keeps the samples exactly but moves the image in jumps, bilinear is the usual
    compromise, and bicubic keeps small detail sharper at the cost of a slight overshoot at hard edges.
*/
enum Sampler : int
{
    Sampler_Nearest = 0,
    Sampler_Bilinear,
    Sampler_Bicubic,

    Sampler_COUNT
};

const char *sampler_name(int sampler);

//! Move the samples by \p dx, \p dy, reading past the edges as \p border_x and \p border_y say.
/*!
    Positive offsets move the image right and down -- the content goes where the numbers say, which means
    the sample read for an output position is the one that far back.

    Offsets need not be whole numbers; \p sampler is what reconstructs between samples, and is ignored
    when both offsets are integral, where every output sample is exactly an input one.

    With BorderMode_Repeat this is the wrapping shift, which is the reason to have this at all: it slides
    a tiling texture without breaking the tiling, so the seam can be brought into the middle of the image
    where it can be seen and painted out.
*/
Array2Df shifted(const Array2Df &src, const Box2i &region, float dx, float dy, int sampler = Sampler_Bilinear,
                 int border_x = BorderMode_Repeat, int border_y = BorderMode_Repeat);

//! Blur separably with a Gaussian of the given standard deviations, in samples.
/*!
    Two passes, one per axis, which costs O(radius) per sample rather than the O(radius^2) a square kernel
    would -- the Gaussian is the filter that permits this, being the product of its own 1D form. Exact, in
    the sense that the kernel really is a sampled Gaussian; see fast_gaussian_blurred() for the cheaper
    approximation.

    A sigma of zero along an axis leaves that axis alone.
*/
Array2Df gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y,
                          AtomicProgress progress = {});

/*!
    Average over a box of the given half-widths, \p iterations times.

    Each pass costs the same per sample whatever the half-width: the sum over the box moves one sample at a
    time, adding the sample entering it and subtracting the one leaving. That is the whole reason to reach
    for boxes, and what makes repeating them cheap.

    Repeating widens the blur -- n passes of half-width w carry the variance of n of them -- because this is
    the box blur as an effect in its own right, where that is what was asked for. To approximate a Gaussian
    of a *stated* width instead, use fast_gaussian_blurred().
*/
Array2Df box_blurred(const Array2Df &src, const Box2i &region, int half_width_x, int half_width_y, int iterations = 1,
                     AtomicProgress progress = {});

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
                               int iterations = 6, AtomicProgress progress = {});

/*!
    Replace each sample with the median of the disc of radius \p radius around it.

    What a blur cannot do: remove a lone bright sample without smearing it into its neighbors. A mean is
    dragged by an outlier however far out it is, while a median ignores it entirely once it is outnumbered
    -- which is why this, not a blur, is what takes fireflies out of a render.

    \p disc takes the median over a circle rather than the square that encloses it, which 1.8 offered as an
    option and is usually what is wanted: a square window reaches a factor of root two farther at its
    corners than along its axes, which shows up as a faint squareness in what it removes.

    Unlike the blurs there is no separable form, so this costs the area of the window per sample and is the
    first filter slow enough to need \p progress. Pass one to have it report and to be able to stop it; a
    canceled run returns what it had reached, which the caller must discard.
*/
Array2Df median_filtered(const Array2Df &src, const Box2i &region, float radius, bool disc = true,
                         AtomicProgress progress = {});

/*!
    Replace every non-finite sample with the median of its finite neighbors.

    A NaN or an infinity is not a measurement, and one of either ruins every statistic computed over the
    channel it sits in. Writing a constant in its place leaves a hole that is just as visible; taking the
    median of the eight samples around it puts back something the neighborhood agrees with. Finite samples
    are untouched, however extreme -- a genuinely bright highlight is data.

    \p replacement is used only where a sample has no finite neighbour at all, which happens in the middle
    of a run of them.
*/
Array2Df zapped_gremlins(const Array2Df &src, const Box2i &region, float replacement = 0.f);

/*!
    Add back a multiple of what a blur removed, which sharpens.

    \p amount scales the difference between the samples and their blurred selves: 0 leaves the image alone,
    1 doubles the detail the blur would have taken out. \p sigma sets the size of the detail affected --
    small values sharpen fine texture, large ones raise local contrast.
*/
Array2Df unsharp_masked(const Array2Df &src, const Box2i &region, float sigma, float amount,
                        AtomicProgress progress = {});
