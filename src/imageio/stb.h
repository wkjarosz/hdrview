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

struct STBSaveOptions;
struct SaveContext;

// should not throw
bool is_stb_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_stb_image(std::istream &is, const std::string_view filename,
                                     const ImageLoadOptions &opts = {});
// throws on error
// void save_stb_image(const Image &img, std::ostream &os, const std::string_view filename, bool sRGB = true,
//                     bool dither = true);
void save_stb_hdr(const Image &img, std::ostream &os, const std::string_view filename);
void save_stb_hdr(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_jpg(const Image &img, std::ostream &os, const std::string_view filename, bool dither = true,
                  float quality = 95.f);
void save_stb_jpg(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_tga(const Image &img, std::ostream &os, const std::string_view filename, bool dither = true);
void save_stb_tga(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_bmp(const Image &img, std::ostream &os, const std::string_view filename, bool dither = true);
void save_stb_bmp(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_png(const Image &img, std::ostream &os, const std::string_view filename, bool dither = true);
void save_stb_png(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

STBSaveOptions *stb_parameters_gui(bool is_hdr, bool has_quality, SaveContext &ctx);
/// Whether the stb save options offer \p tf: linear for .hdr and sRGB otherwise, which readers assume.
bool stb_offers_tf(TransferFunction tf, bool is_hdr);
