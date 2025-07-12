//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

#include "colorspace.h"
#include "fwd.h"
#include <string>

namespace icc
{

/*!
    \brief Linearize a (potentially interleaved) array of floating-point pixel values using the transfer function of the
    provided ICC profile.

    This function tries to apply only the inverse transfer function of the ICC profile to the pixel values. It does not
    perform color transformations.

    \param[inout] pixels
        The pixel values to linearize in place.
    \param[in] size
        The dimensions of the pixels array in width, height, and number of channels. If size.z > 1 the pixel array is
        interleaved.
    \param[in] icc_profile
        A byte array containing the ICC profile to transform by.
    \param[out] tf_description
        A description of the transfer function used to linearize the pixel values will be written to this string.
    \param[out] red, green, blue, white
        If not nullptr, the chromaticities of the ICC profile will be written to these variables.
    \returns
        True if the pixel values were successfully linearized.
*/
bool linearize_colors(float *pixels, int3 size, const std::vector<uint8_t> &icc_profile,
                      std::string *tf_description = nullptr, Chromaticities *chr = nullptr);
} // namespace icc
