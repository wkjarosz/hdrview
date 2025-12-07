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

// should not throw
bool is_stb_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_stb_image(std::istream &is, const std::string_view filename,
                                     const ImageLoadOptions &opts = {});
// throws on error
// void save_stb_image(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
//                     bool sRGB = true, bool dither = true);
void save_stb_hdr(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                  TransferFunction tf = TransferFunction::Linear);
void save_stb_hdr(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_jpg(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                  TransferFunction tf = TransferFunction::sRGB, bool dither = true, float quality = 95.f);
void save_stb_jpg(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_tga(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                  TransferFunction tf = TransferFunction::sRGB, bool dither = true);
void save_stb_tga(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_bmp(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                  TransferFunction tf = TransferFunction::sRGB, bool dither = true);
void save_stb_bmp(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

void save_stb_png(const Image &img, std::ostream &os, const std::string_view filename, float gain = 1.f,
                  TransferFunction tf = TransferFunction::sRGB, bool dither = true);
void save_stb_png(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);

STBSaveOptions *stb_parameters_gui(bool is_hdr, bool has_quality);
void save_stb_bmp(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts);
