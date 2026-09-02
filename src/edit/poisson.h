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
    Seamless paste by gradient-domain compositing (Perez et al., "Poisson Image Editing", SIGGRAPH 2003):
    solve for values inside the mask whose Laplacian matches the source, pinned to the background at the
    border, so a pasted patch takes on the surrounding illumination. Conjugate gradient on the discrete
    Poisson equation, which is large but sparse and symmetric positive-definite.
*/

//! Values matching \p source's gradients inside \p mask, and \p background exactly outside it.
/*!
    All three arrays must be the same size, and \p mask must be zero along the border, which pins the
    solution to the background. \p iterations bounds the work; the solve stops early once the residual has
    fallen by \p tolerance relative to where it started. A canceled solve returns a partial estimate that
    the caller must discard.
*/
Array2Df poisson_blended(const Array2Df &background, const Array2Df &source, const Array2Df &mask, int iterations = 300,
                         float tolerance = 1e-4f, AtomicProgress progress = {});

//! The discrete Laplacian of \p src (eight-neighbor form), reading past its edges by clamping. The same
//! operator the solve inverts, so it also produces the guidance field.
Array2Df laplacian(const Array2Df &src);
