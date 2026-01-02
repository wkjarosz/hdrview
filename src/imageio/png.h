//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "image_loader.h"

#include "json.h"

// return a JSON object describing the libpng backend
json get_png_info();

#include <istream>
#include <string_view>

// should not throw
bool is_png_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_png_image(std::istream &is, const std::string_view filename,
                                     const ImageLoadOptions &opts = {});
// throws on error
void save_png_image(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                    bool dither = true, bool interlaced = false, bool sixteen_bit = false,
                    TransferFunction tf = TransferFunction::sRGB);

struct PNGSaveOptions;
PNGSaveOptions *png_parameters_gui();
// throws on error
void save_png_image(const Image &img, std::ostream &os, std::string_view filename, const PNGSaveOptions *params);