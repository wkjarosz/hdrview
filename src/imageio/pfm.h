//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>
#include <string>

#include "colorspace.h"
#include "fwd.h"
#include "image_loader.h"

// should not throw
bool is_pfm_image(std::istream &is) noexcept;

std::vector<ImagePtr>    load_pfm_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
std::unique_ptr<float[]> load_pfm_image(std::istream &is, std::string_view filename, int *width, int *height,
                                        int *num_channels);
// throws on error
void write_pfm_image(std::ostream &os, std::string_view filename, int width, int height, int num_channels,
                     const float data[]);
void save_pfm_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f,
                    TransferFunction_ tf = TransferFunction_Linear, float gamma = 1.f);

struct PFMSaveOptions;
PFMSaveOptions *pfm_parameters_gui();
// throws on error
void save_pfm_image(const Image &img, std::ostream &os, std::string_view filename, const PFMSaveOptions *opts);