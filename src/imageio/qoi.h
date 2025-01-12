//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>
#include <memory>
#include <string>

#include "fwd.h"

// should not throw
bool is_qoi_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_qoi_image(std::istream &is, const std::string &filename);
/// throws on error
void save_qoi_image(const Image &img, std::ostream &os, const std::string &filename, float gain = 1.f,
                    float gamma = 2.2f, bool sRGB = true, bool dither = true);