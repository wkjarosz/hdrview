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
// return a JSON object describing the libtiff backend
json get_tiff_info();

// should not throw
bool is_tiff_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_tiff_image(std::istream &is, const std::string_view filename,
                                      const ImageLoadOptions &opts = {});

/// The sample types a TIFF can be saved with, in the order the save dialog lists them.
using TIFFSampleType = int;
enum TIFFSampleType_ : TIFFSampleType
{
    TIFFSampleType_UInt8 = 0,
    TIFFSampleType_UInt16,
    TIFFSampleType_Float32,
    TIFFSampleType_Float16
};

/// The compressions a TIFF can be saved with, in the order the save dialog lists them.
using TIFFCompression = int;
enum TIFFCompression_ : TIFFCompression
{
    TIFFCompression_None = 0,
    TIFFCompression_LZW,
    TIFFCompression_ZIP,
    TIFFCompression_PackBits
};

struct TIFFSaveOptions;
struct SaveContext;
struct Caveat;
TIFFSaveOptions *tiff_parameters_gui(SaveContext &ctx);
/// Whether the TIFF save options offer \p tf for \p data_type (float: linear; integer: sRGB or ICC-labeled).
bool tiff_offers_tf(TransferFunction tf, TIFFSampleType data_type);
/// Which applications misread a TIFF with transfer function \p tf, over a gamut other than BT.709's if \p wide_gamut.
Caveat tiff_caveat(TransferFunction tf, bool wide_gamut);
// throws on error
void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename, const TIFFSaveOptions *params);

// simplified wrapper with explicit parameters: a TIFFCompression and a TIFFSampleType
void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename,
                     TransferFunction tf = TransferFunction::Linear, TIFFCompression compression = TIFFCompression_LZW,
                     TIFFSampleType data_type = TIFFSampleType_UInt8);
