//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>
#include <string_view>

#include "colorspace.h"
#include "fwd.h"
#include "image_loader.h"

// should not throw
bool is_jxl_image(std::istream &is) noexcept;
bool jxl_supported_tf(TransferFunctionWithParams tf) noexcept;
// throws on error
std::vector<ImagePtr> load_jxl_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
// throws on error
void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f,
                    bool lossless = false, float quality = 95.f, TransferFunctionWithParams tf = TransferFunction_sRGB,
                    int data_type = 0);

struct JXLSaveOptions;
JXLSaveOptions *jxl_parameters_gui();
// throws on error
void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, JXLSaveOptions *opts);