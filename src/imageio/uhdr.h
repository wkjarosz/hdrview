//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>
#include <string_view>

#include "colorspace.h"
#include "fwd.h"
#include "image_loader.h"

#include "json.h"
// return a JSON object describing the libuhdr backend
json get_uhdr_info();

// should not throw
bool is_uhdr_image(std::istream &is) noexcept;
bool uhdr_supported_tf(TransferFunction tf) noexcept;

// throws on error
std::vector<ImagePtr> load_uhdr_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
/// Writes \p img as an Ultra HDR JPEG, over \p sdr_base or else a base libultrahdr tone-maps; throws on error.
void save_uhdr_image(const Image &img, std::ostream &os, std::string_view filename, int base_quality = 95,
                     int gainmap_quality = 95, bool use_multi_channel_gainmap = true, int gainmap_scale_factor = 1,
                     float gainmap_gamma = 1.f, const Image *sdr_base = nullptr);

struct UHDRSaveOptions;
struct SaveContext;
UHDRSaveOptions *uhdr_parameters_gui(SaveContext &ctx);
/// Writes \p img, an HDR rendition, as an Ultra HDR JPEG whose gain map recovers it from an SDR base.
/**
    The base is \p sdr_base, an SDR rendition of the same view, when the options ask for one; otherwise libultrahdr
    tone-maps \p img itself. The gain map reaches the peak \p img's mastering display names, if it names one. Throws
    on error.
*/
void save_uhdr_image(const Image &img, std::ostream &os, std::string_view filename, const UHDRSaveOptions *params,
                     const Image *sdr_base = nullptr);