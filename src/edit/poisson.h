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
    Gradient-domain compositing: paste what a region *varies* like rather than what it *is*.

    Pasting values leaves a seam wherever the two images disagree at the border, and they nearly always
    do. Poisson image editing (Perez et al., "Poisson Image Editing", 2003) pastes the source's gradients
    instead and solves for the values that have them, pinned at the border to the background that is
    already there. The seam cannot appear, because the border values are the background's own; what
    changes instead is the interior, which drifts to meet them -- a pasted patch takes on the surrounding
    illumination rather than carrying its own.

    The system is the discrete Poisson equation over the masked region, which is large (one unknown per
    sample) but sparse and symmetric positive-definite, so it is solved iteratively rather than directly.
*/

//! Values matching \p source's gradients inside \p mask, and \p background exactly outside it.
/*!
    All three arrays must be the same size; \p mask is the coverage that says which samples are being
    solved for, and must be zero along the border, which is what pins the solution to the background.

    \p iterations bounds the work. Conjugate gradient converges in far fewer than the number of unknowns
    for a system this well-conditioned, and it stops early once the residual has fallen by \p tolerance
    relative to where it started, a fixed iteration count being a great deal of work after the answer stops
    changing.

    \p progress is reported per iteration and honored: a canceled solve returns the estimate it had
    reached, which the caller must discard rather than apply.
*/
Array2Df poisson_blended(const Array2Df &background, const Array2Df &source, const Array2Df &mask, int iterations = 300,
                         float tolerance = 1e-4f, AtomicProgress progress = {});

//! The discrete Laplacian of \p src, reading past its edges by clamping.
/*!
    The eight-neighbor form: the four along the axes and the four diagonals, less eight times
    the center. It is the operator the solve inverts, so the same one has to produce the guidance field --
    a Laplacian that does not match the one being solved against leaves an error the iteration cannot
    remove.
*/
Array2Df laplacian(const Array2Df &src);
