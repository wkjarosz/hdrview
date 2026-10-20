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

#include "json.h"
// return a JSON object describing the libjxl backend
json get_jxl_info();

// should not throw
bool is_jxl_image(std::istream &is) noexcept;
/// Whether the JPEG XL save options offer \p tf: whether its color encoding or an ICC profile can state it.
bool jxl_offers_tf(TransferFunction tf) noexcept;
struct Caveat;
/// Which applications misread a JPEG XL with the transfer function \p tf, lossless or not, and how.
Caveat jxl_caveat(TransferFunction tf, bool lossless);
// throws on error
std::vector<ImagePtr> load_jxl_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
// throws on error
void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, bool lossless = false,
                    float quality = 95.f, TransferFunction tf = TransferFunction::sRGB, int data_type = 0);

struct JXLSaveOptions;
struct SaveContext;
JXLSaveOptions *jxl_parameters_gui(SaveContext &ctx);
// throws on error
void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, JXLSaveOptions *opts);