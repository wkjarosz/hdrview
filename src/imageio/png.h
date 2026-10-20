//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "image_loader.h"

#include "json.h"

// return a JSON object describing the libpng backend
json get_png_info();

#include <istream>
#include <string_view>

// should not throw
bool is_png_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_png_image(std::istream &is, const std::string_view filename,
                                     const ImageLoadOptions &opts = {});
// throws on error
void save_png_image(const Image &img, std::ostream &os, const std::string_view filename, bool dither = true,
                    bool interlaced = false, bool sixteen_bit = false, TransferFunction tf = TransferFunction::sRGB);

struct PNGSaveOptions;
struct SaveContext;
struct Caveat;
PNGSaveOptions *png_parameters_gui(SaveContext &ctx);
/// Whether the PNG save options offer \p tf: whether this build can label a PNG with it.
bool png_offers_tf(TransferFunction tf);
/// Which applications misread a PNG with the curve \p tf, and how.
Caveat png_caveat(TransferFunction tf);
// throws on error
void save_png_image(const Image &img, std::ostream &os, std::string_view filename, const PNGSaveOptions *params);