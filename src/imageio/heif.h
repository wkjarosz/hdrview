//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>
#include <string>
#include <vector>

#include "fwd.h"
#include "image_loader.h"

#include "json.h"

// return a JSON object describing the libheif backend (see schema in app-gui comments)
json get_heif_info();

// should not throw
bool is_heif_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_heif_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
/// Which codec family goes inside the HEIF container. Several encoders may implement any one of these.
enum class HEIFCodec
{
    HEIF = 0, ///< any codec a .heif may carry (HEVC, JPEG, JPEG 2000), but not AV1
    HEVC,     ///< HEVC specifically
    AV1,      ///< AV1, which libheif brands 'avif' whatever the file is named
};

void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, int quality = 95,
                     bool lossless = false, bool use_alpha = true, HEIFCodec codec = HEIFCodec::HEIF,
                     TransferFunction tf = TransferFunction::sRGB, int bit_depth = 0);

struct HEIFSaveOptions;
struct SaveContext;
struct Caveat;
/// Draws the options for `codec`, listing only the encoders that implement it.
HEIFSaveOptions *heif_parameters_gui(HEIFCodec codec, SaveContext &ctx);
/// Whether the HEIF/AVIF save options offer \p tf: whether an nclx box can name it.
bool heif_offers_tf(TransferFunction tf);
/// Which applications misread a HEIF or AVIF with the transfer function \p tf, and how.
Caveat heif_caveat(TransferFunction tf);
// throws on error
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, const HEIFSaveOptions *params);