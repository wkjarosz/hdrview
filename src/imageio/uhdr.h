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

// should not throw
bool is_uhdr_image(std::istream &is) noexcept;
bool uhdr_supported_tf(TransferFunction tf) noexcept;

// throws on error
std::vector<ImagePtr> load_uhdr_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
// throws on error
void save_uhdr_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f,
                     int base_quality = 95, int gainmap_quality = 95, bool use_multi_channel_gainmap = false,
                     int gainmap_scale_factor = 1, float gainmap_gamma = 1.f);

struct UHDRSaveOptions;
UHDRSaveOptions *uhdr_parameters_gui();
// throws on error
void save_uhdr_image(const Image &img, std::ostream &os, std::string_view filename, const UHDRSaveOptions *params);
