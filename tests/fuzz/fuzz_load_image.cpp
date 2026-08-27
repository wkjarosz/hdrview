//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// libFuzzer entry point for the image-loading path. load_image() sniffs magic bytes to pick a loader, so a
// single target reaches every format in default_loaders() -- and, through the metadata parsers, the EXIF,
// ICC, and XMP readers too.
//
// Build:  cmake --preset macos-arm64-cpm -B build/fuzz -DHDRVIEW_BUILD_FUZZERS=ON \
//                -DUSE_SANITIZER="Address;Undefined" -DCMAKE_BUILD_TYPE=RelWithDebInfo
// Run:    ./build/fuzz/hdrview_fuzz_load_image corpus/ -max_len=65536

#include "image.h"
#include "imageio/image_loader.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <sstream>
#include <string>

namespace
{

// Loading logs copiously at info/warn level; at fuzzing rates that dominates the runtime and buries the
// sanitizer's own output.
struct QuietLogs
{
    QuietLogs() { spdlog::set_level(spdlog::level::off); }
};
QuietLogs quiet_logs;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Enormous declared dimensions are a decode-time allocation, not a parser bug; capping the input keeps the
    // fuzzer off inputs whose only interesting property is that they ask for gigabytes.
    if (size > 1u << 20)
        return 0;

    std::string        bytes(reinterpret_cast<const char *>(data), size);
    std::istringstream is(bytes, std::ios::binary);

    try
    {
        auto images = load_image(is, "fuzz.bin");

        // load_image() finalizes each image itself, so channel grouping, alpha premultiplication, and the
        // color transform are all already exercised by the call above.
        for (auto &img : images)
            if (img)
            {
                // Touch the data the GUI reads on every frame for the selected group.
                if (!img->groups.empty() && img->is_valid_group(img->selected_group))
                    (void)img->raw_pixel(img->data_window.min);
            }
    }
    catch (const std::exception &)
    {
        // Rejecting malformed input by throwing is correct behavior, not a finding.
    }
    catch (...)
    {
    }

    return 0;
}
