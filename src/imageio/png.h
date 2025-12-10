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
bool is_png_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_png_image(std::istream &is, const std::string_view filename,
                                     const ImageLoadOptions &opts = {});
// throws on error
void save_png_image(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                    bool dither = true, bool interlaced = false, bool sixteen_bit = false,
                    TransferFunction tf = TransferFunction::sRGB);

struct PNGSaveOptions;
PNGSaveOptions *png_parameters_gui();
// throws on error
void save_png_image(const Image &img, std::ostream &os, std::string_view filename, const PNGSaveOptions *params);

// helpers to check which features are supported in this build
#ifdef PNG_TEXT_SUPPORTED
#define PNG_TEXT_SUPPORTED_ENABLED 1
#else
#define PNG_TEXT_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_eXIf_SUPPORTED
#define PNG_eXIf_SUPPORTED_ENABLED 1
#else
#define PNG_eXIf_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_EASY_ACCESS_SUPPORTED
#define PNG_EASY_ACCESS_SUPPORTED_ENABLED 1
#else
#define PNG_EASY_ACCESS_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_READ_ALPHA_MODE_SUPPORTED
#define PNG_READ_ALPHA_MODE_SUPPORTED_ENABLED 1
#else
#define PNG_READ_ALPHA_MODE_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_GAMMA_SUPPORTED
#define PNG_GAMMA_SUPPORTED_ENABLED 1
#else
#define PNG_GAMMA_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_USER_CHUNKS_SUPPORTED
#define PNG_USER_CHUNKS_SUPPORTED_ENABLED 1
#else
#define PNG_USER_CHUNKS_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_APNG_SUPPORTED
#define PNG_APNG_SUPPORTED_ENABLED 1
#else
#define PNG_APNG_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_PROGRESSIVE_READ_SUPPORTED
#define PNG_PROGRESSIVE_READ_SUPPORTED_ENABLED 1
#else
#define PNG_PROGRESSIVE_READ_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_cICP_SUPPORTED
#define PNG_cICP_SUPPORTED_ENABLED 1
#else
#define PNG_cICP_SUPPORTED_ENABLED 0
#endif
#ifdef PNG_iCCP_SUPPORTED
#define PNG_iCCP_SUPPORTED_ENABLED 1
#else
#define PNG_iCCP_SUPPORTED_ENABLED 0
#endif