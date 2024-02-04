//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>
#include <string>

bool is_pfm_image(std::istream &is) noexcept;
bool is_pfm_image(const std::string &filename) noexcept;

std::unique_ptr<float[]> load_pfm_image(std::istream &is, const std::string &filename, int *width, int *height,
                                        int *num_channels);
std::unique_ptr<float[]> load_pfm_image(const std::string &filename, int *width, int *height, int *num_channels);

void write_pfm_image(const std::string &filename, int width, int height, int num_channels, const float data[]);
void write_pfm_image(std::ostream &os, const std::string &filename, int width, int height, int num_channels,
                     const float data[]);