//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"

/// What a format's options tell the save dialog about the encoding they chose.
struct SaveContext
{
    bool hdr = false; ///< Whether the encoding keeps values above SDR white
};

/// Whether an encoding keeps values above SDR white: an HDR transfer function, or floating-point samples.
inline bool is_hdr_encoding(const TransferFunction &tf, bool floating_point = false)
{ return floating_point || tf.type == TransferFunction::BT2100_PQ || tf.type == TransferFunction::BT2100_HLG; }

/// \p curve, the one a format can label, warning when \p asked was another; a reader would misread that one.
TransferFunction labeled_curve(const TransferFunction &asked, TransferFunction::Type_ curve, const char *format);
