//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>
#include <string_view>

#include "fwd.h"
#include "image_loader.h"

#include "json.h"

// return a JSON object describing the jpeg backend (see schema in app-gui comments)
json get_jpg_info();

// should not throw
bool is_jpg_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_jpg_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
// throws on error
void save_jpg_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f, bool sRGB = true,
                    bool dither = true, int quality = 95, bool progressive = false);

struct JPGSaveOptions;
JPGSaveOptions *jpg_parameters_gui();
// throws on error
void save_jpg_image(const Image &img, std::ostream &os, std::string_view filename, const JPGSaveOptions *params);
