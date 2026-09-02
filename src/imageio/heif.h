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
    Any = 0, ///< whatever the build has, preferring HEVC and then AV1; the default for non-GUI callers
    HEIF,    ///< any codec a .heif may carry (HEVC, JPEG, JPEG 2000), but not AV1
    HEVC,    ///< HEVC specifically
    AV1,     ///< AV1, which libheif brands 'avif' whatever the file is named
};

void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f, int quality = 95,
                     bool lossless = false, bool use_alpha = true, HEIFCodec codec = HEIFCodec::Any,
                     TransferFunction tf = TransferFunction::sRGB);

/// Names of the encoders offered for `codec`, in the order the save dialog lists them.
/**
    Empty when the libheif build has none: distributions package each codec separately, so reading doesn't
    imply writing.
*/
std::vector<std::string> heif_encoder_names(HEIFCodec codec);

struct HEIFSaveOptions;
/// Draws the options for `codec`, listing only the encoders that implement it.
HEIFSaveOptions *heif_parameters_gui(HEIFCodec codec);
// throws on error
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, const HEIFSaveOptions *params);