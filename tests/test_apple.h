//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

// macOS's own decoder, ImageIO, as an independent reader of the files HDRView writes. Apple's headers stay in
// test_apple.cpp, out of the tests that include this.

#if defined(__APPLE__)

#include "fwd.h"
#include <optional>
#include <string>
#include <vector>

namespace hdrview_test
{

/// Which rendition of a gain-map file to ask ImageIO for.
enum class AppleDecode
{
    Default, ///< what an application gets without asking: the base image
    SDR,     ///< kCGImageSourceDecodeToSDR: the SDR rendition
    HDR,     ///< kCGImageSourceDecodeToHDR: the HDR rendition, a gain map applied in full
};

/// An image as ImageIO decodes it, top row first.
struct AppleImage
{
    int2                size{0};
    std::vector<float4> pixels;

    float4 operator()(int x, int y) const { return pixels[size_t(y) * size.x + x]; }
};

/// The major version of the macOS the tests run on, e.g. 27, or 0 if it cannot be read.
int apple_os_major();

/// \p bytes as macOS's ImageIO decodes them, the decoder Preview and every macOS application use.
/**
    Linear BT.709 light with 1.0 at SDR white, drawn into a float bitmap in extended linear sRGB, so the file's labels
    are honored, or ignored, as every macOS application would. None when ImageIO cannot decode the file, or when
    \p request asks for a rendition before macOS 14, which introduced them.
*/
std::optional<AppleImage> apple_decode(const std::string &bytes, AppleDecode request = AppleDecode::Default);

} // namespace hdrview_test

#endif
