//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "image_loader.h"

#include <istream>
#include <string_view>

#include "json.h"
// return a JSON object describing the libwebp backend
json get_webp_info();

// should not throw
bool is_webp_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_webp_image(std::istream &is, const std::string_view filename,
                                      const ImageLoadOptions &opts = {});
// throws on error
void save_webp_image(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                     float quality = 95.f, bool lossless = false, TransferFunction tf = TransferFunction::sRGB);

struct WebPSaveOptions;
WebPSaveOptions *webp_parameters_gui();
// throws on error
void save_webp_image(const Image &img, std::ostream &os, std::string_view filename, const WebPSaveOptions *params);
