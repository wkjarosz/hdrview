//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "image_loader.h"

#include <istream>
#include <string_view>

// should not throw
bool is_tiff_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_tiff_image(std::istream &is, const std::string_view filename,
                                      const ImageLoadOptions &opts = {});

struct TIFFSaveOptions;
TIFFSaveOptions *tiff_parameters_gui();
// throws on error
void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename, const TIFFSaveOptions *params);

// simplified wrapper with explicit parameters
void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f,
                     TransferFunction tf = TransferFunction::Linear, int compression = 1, int data_type = 0);
