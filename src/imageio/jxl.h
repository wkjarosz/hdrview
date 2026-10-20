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
/// Which applications ignore a JPEG XL's gain map, over an HDR base if \p hdr_base and an SDR one otherwise.
Caveat jxl_gainmap_caveat(bool hdr_base);
// throws on error
std::vector<ImagePtr> load_jxl_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});
/// Writes \p img, with a gain map to \p sdr_rendition when one is given; throws on error.
/**
    \p img is then the HDR rendition and \p sdr_rendition the SDR one of the same view. With \p hdr_base the file's
    image is \p img and its gain map darkens it to \p sdr_rendition; otherwise the file's image is \p sdr_rendition
    and the map brightens it to \p img, up to the peak \p img's mastering display names. \p tf and \p data_type
    encode the file's image.
*/
void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, bool lossless = false,
                    float quality = 95.f, TransferFunction tf = TransferFunction::sRGB, int data_type = 0,
                    const Image *sdr_rendition = nullptr, bool hdr_base = false);

struct JXLSaveOptions;
struct SaveContext;
JXLSaveOptions *jxl_parameters_gui(SaveContext &ctx);
#if defined(HDRVIEW_ENABLE_GUI_TEST_ENGINE)
/// The options' Gain map choice, 0 none, 1 an SDR base, 2 an HDR base, which the GUI tests set without its combo.
int &jxl_gain_map_option();
#endif
/// Writes \p img as \p opts say, with a gain map to \p sdr_rendition when they ask for one; throws on error.
void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, JXLSaveOptions *opts,
                    const Image *sdr_rendition = nullptr);