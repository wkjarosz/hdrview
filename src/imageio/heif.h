//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <istream>

#include "fwd.h"
#include "image_loader.h"

#include "json.h"

// return a JSON object describing the libheif backend (see schema in app-gui comments)
json get_heif_info();

// should not throw
bool is_heif_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_heif_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
//! Which codec goes inside the HEIF container.
/*!
    AVIF is AV1 by definition, and a .heic file is HEVC by definition; a .heif may hold either, or JPEG
    or JPEG 2000. This is the family, not the encoder: several encoders may implement one of these.
*/
enum class HEIFCodec
{
    Any = 0, //!< whatever is available, preferring HEVC
    HEVC,    //!< .heif / .heic
    AV1,     //!< .avif
};

void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f, int quality = 95,
                     bool lossless = false, bool use_alpha = true, HEIFCodec codec = HEIFCodec::Any,
                     TransferFunction tf = TransferFunction::sRGB);

struct HEIFSaveOptions;
//! Draws the options for `codec`, listing only the encoders that implement it.
HEIFSaveOptions *heif_parameters_gui(HEIFCodec codec);
// throws on error
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, const HEIFSaveOptions *params);