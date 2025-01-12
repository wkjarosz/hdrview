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
bool is_pfm_image(std::istream &is) noexcept;

std::vector<ImagePtr>    load_pfm_image(std::istream &is, const std::string &filename);
std::unique_ptr<float[]> load_pfm_image(std::istream &is, const std::string &filename, int *width, int *height,
                                        int *num_channels);
// throws on error
void write_pfm_image(std::ostream &os, const std::string &filename, int width, int height, int num_channels,
                     const float data[]);
void save_pfm_image(const Image &img, std::ostream &os, const std::string &filename, float gain = 1.f);