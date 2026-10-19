//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "save_options.h"

#include <spdlog/spdlog.h>

TransferFunction labeled_curve(const TransferFunction &asked, TransferFunction::Type_ curve, const char *format)
{
    if (asked.type != curve)
        spdlog::warn("{} cannot record the {} transfer function; writing {} instead.", format,
                     transfer_function_name(asked), transfer_function_name(curve));
    return curve;
}
