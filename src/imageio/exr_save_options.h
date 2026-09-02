//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <ImfCompression.h>
#include <vector>

struct Image;

// Full definition of EXRSaveOptions, split out of exr.h so files that only pass this around as an opaque
// pointer don't pay for parsing OpenEXR's headers.
struct EXRSaveOptions
{
    std::vector<bool> group_enabled;                     // size = img.groups.size()
    int               pixel_type  = 1;                   // 0 = Imf::FLOAT, 1 = Imf::HALF
    Imf::Compression  compression = Imf::PIZ_COMPRESSION; // Default compression
    bool              tiled       = false;
    int               tile_width  = 64;
    int               tile_height = 64;
    float             dwa_quality = 45.0f; // Only for DWAA/DWAB
};

// Enables every channel group in img by default; usable without an active ImGui frame.
EXRSaveOptions exr_default_save_options(const Image &img);
