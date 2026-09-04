//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/image_loader.h"
#include "imageio/jxl.h"

#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace hdrview_test;

#if HDRVIEW_ENABLE_LIBJXL && defined(HDRVIEW_TEST_LIBJXL_DIR)

// libjxl's own codestreams, which reach features no round trip through HDRView's writer produces: splines,
// a PQ gradient, a frame blended onto a crop, a container box whose size is written the extended way, and a
// JPEG reconstructed from one, carrying the EXIF and XMP that came with it.
TEST_CASE("libjxl's own codestreams decode, metadata and all")
{
    namespace fs = std::filesystem;

    const fs::path root = std::string(HDRVIEW_TEST_LIBJXL_DIR) + "/jxl";
    if (!fs::exists(root))
        return;

    int files = 0;
    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        const auto path = entry.path();
        if (!entry.is_regular_file() || path.extension() != ".jxl")
            continue;

        const std::string name = path.filename().string();
        CAPTURE(name);

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

        std::istringstream    stream(bytes, std::ios::binary);
        std::vector<ImagePtr> images;
        REQUIRE_NOTHROW(images = load_jxl_image(stream, name));
        REQUIRE_FALSE(images.empty());

        for (const auto &img : images)
        {
            img->finalize();
            CHECK(img->size().x > 0);
            CHECK(img->size().y > 0);
            CHECK_FALSE(img->channels.empty());
        }

        // the one file here reconstructed from a JPEG carries that JPEG's metadata, which has to survive the
        // reconstruction rather than being dropped with the container it came in
        if (name.find("exif_xmp") != std::string::npos)
        {
            CHECK(images.front()->metadata.contains("exif"));
            CHECK_FALSE(images.front()->xmp_data.empty());
        }
        ++files;
    }
    CHECK(files > 0);
}

#endif // HDRVIEW_ENABLE_LIBJXL && HDRVIEW_TEST_LIBJXL_DIR
