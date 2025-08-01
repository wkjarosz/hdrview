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

// does not throw
bool is_exr_image(std::istream &is, std::string_view filename) noexcept;
// throws on error
std::vector<ImagePtr> load_exr_image(std::istream &is, std::string_view filename,
                                     std::string_view channel_selector = std::string_view{});
// throws on error
void save_exr_image(const Image &img, std::ostream &os, std::string_view filename);