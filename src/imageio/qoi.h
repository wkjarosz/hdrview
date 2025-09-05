//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include "image_loader.h"

#include <istream>
#include <string>

// should not throw
bool is_qoi_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_qoi_image(std::istream &is, const std::string_view filename,
                                     const ImageLoadOptions &opts = {});
/// throws on error
void save_qoi_image(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                    bool sRGB = true, bool dither = true);