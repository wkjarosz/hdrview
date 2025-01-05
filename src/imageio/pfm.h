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

bool is_pfm_image(std::istream &is) noexcept;
bool is_pfm_image(const std::string &filename) noexcept;

std::vector<ImagePtr>    load_pfm_image(std::istream &is, const std::string &filename);
std::unique_ptr<float[]> load_pfm_image(std::istream &is, const std::string &filename, int *width, int *height,
                                        int *num_channels);
std::unique_ptr<float[]> load_pfm_image(const std::string &filename, int *width, int *height, int *num_channels);

void write_pfm_image(const std::string &filename, int width, int height, int num_channels, const float data[]);
void write_pfm_image(std::ostream &os, const std::string &filename, int width, int height, int num_channels,
                     const float data[]);