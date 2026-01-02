//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>

#include "fwd.h"
#include "image_loader.h"

#include "json.h"

// return a JSON object describing the libheif backend (see schema in app-gui comments)
json get_heif_info();

// should not throw
bool is_heif_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_heif_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f, int quality = 95,
                     bool lossless = false, bool use_alpha = true, int format_index = 0,
                     TransferFunction tf = TransferFunction::sRGB);

struct HEIFSaveOptions;
HEIFSaveOptions *heif_parameters_gui();
// throws on error
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, const HEIFSaveOptions *params);