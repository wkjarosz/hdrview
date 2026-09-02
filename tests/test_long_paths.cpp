//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/image_loader.h"
#include "imageio/pfm.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{

namespace fs = std::filesystem;

// Nested subdirectories under the temp directory adding up to more than Win32's MAX_PATH of 260. On Windows
// this needs both a long-path-aware manifest (resources/windows/HDRView.manifest) and the system-wide
// "Enable Win32 long paths" policy; failing to create it is a hard failure so a runner missing the policy says so.
fs::path make_long_test_dir()
{
    fs::path dir = fs::temp_directory_path() / "hdrview_long_path_test";
    const std::string segment(50, 'a');
    while (dir.u8string().size() < 300)
        dir /= segment;
    return dir;
}

} // namespace

TEST_CASE("BackgroundImageLoader opens an image at a path longer than 260 characters")
{
    fs::path dir = make_long_test_dir();
    REQUIRE(dir.u8string().size() > 260);

    std::error_code ec;
    fs::create_directories(dir, ec);
    REQUIRE_MESSAGE(!ec, "Could not create a >260 character path (", ec.message(),
                     "). This requires the system-wide \"Enable Win32 long paths\" policy to be enabled.");

    fs::path file = dir / "test.pfm";

    const int         width = 2, height = 2, channels = 1;
    const float       pixels[width * height * channels] = {0.0f, 0.25f, 0.5f, 1.0f};
    {
        std::ofstream os{file, std::ios::binary};
        REQUIRE(os.good());
        REQUIRE_NOTHROW(write_pfm_image(os, file.u8string(), width, height, channels, pixels));
    }

    // BackgroundImageLoader is the path that touches fs:: with the long path; load_image() only ever sees an
    // already-open istream.
    BackgroundImageLoader loader;
    REQUIRE_NOTHROW(loader.background_load(file.u8string()));

    std::vector<ImagePtr>    loaded;
    const auto                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (loaded.empty() && loader.num_pending_images() > 0 && std::chrono::steady_clock::now() < deadline)
    {
        REQUIRE_NOTHROW(loader.get_loaded_images(
            [&loaded](ImagePtr img, ImagePtr /*to_replace*/, bool /*should_select*/)
            {
                if (img)
                    loaded.push_back(img);
            }));
        if (loaded.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(loaded.size() == 1);
    CHECK(loaded.front()->size() == int2{width, height});

    fs::remove_all(fs::temp_directory_path() / "hdrview_long_path_test", ec);
}
