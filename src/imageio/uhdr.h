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

bool                  is_uhdr_image(std::istream &is);
std::vector<ImagePtr> load_uhdr_image(std::istream &is, const std::string &filename);
bool save_uhdr_image(const Image &img, std::ostream &os, const std::string &filename, float gain = 1.f);