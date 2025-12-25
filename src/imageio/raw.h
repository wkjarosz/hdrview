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

// should not throw
bool is_raw_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_raw_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
