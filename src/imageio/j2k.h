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

#include "json.h"

// return a JSON object describing the OpenJPEG backend
json get_j2k_info();

// should not throw
bool is_j2k_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_j2k_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});

/// Which of the JPEG 2000 file syntaxes to write.
enum class J2KContainer
{
    JP2 = 0, ///< the JP2 file format: boxes carrying color and metadata around the codestream
    J2K,     ///< a bare codestream, with no boxes and so nowhere to record a color space
};

struct J2KSaveOptions;
J2KSaveOptions *j2k_parameters_gui();
/// The extension for the syntax `params` asks for, since the two share one entry in the save dialog.
const char *j2k_extension(const J2KSaveOptions *params);
// throws on error
void save_j2k_image(const Image &img, std::ostream &os, std::string_view filename, const J2KSaveOptions *params);
// throws on error
void save_j2k_image(const Image &img, std::ostream &os, std::string_view filename, float gain = 1.f,
                    bool lossless = true, int bit_depth = 16, J2KContainer container = J2KContainer::JP2,
                    TransferFunction tf = TransferFunction::sRGB, bool dither = true);
