//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/exr.h"
#include "imageio/exr_save_options.h"

#include <fstream>
#include <sstream>

#ifdef HDRVIEW_TEST_OPENEXR_DIR
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#endif

namespace
{

// Builds an RGB image of the given size with a value per pixel/channel that uniquely identifies it, so a mixup
// between channels/pixels during save+load is immediately visible. data_window/display_window default to the
// whole image at the origin if left empty (matching Image::finalize()'s own default).
ImagePtr make_test_image(int2 size, Box2i data_window = {}, Box2i display_window = {})
{
    auto img = std::make_shared<Image>();
    for (auto name : {"R", "G", "B"})
    {
        img->channels.emplace_back(name, size);
        auto &c = img->channels.back();
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) c(x, y) = float(x + 100 * y) + (name[0] - 'R') * 0.01f;
    }
    img->data_window    = data_window;
    img->display_window = display_window;
    img->finalize();
    return img;
}

// Saves img to an in-memory buffer (enabling all channel groups) and reloads it, returning the reloaded parts.
std::vector<ImagePtr> save_and_reload(const Image &img, EXRSaveOptions opts)
{
    std::ostringstream out(std::ios::binary);
    save_exr_image(img, out, "test.exr", &opts);

    std::istringstream in(out.str(), std::ios::binary);
    return load_exr_image(in, "test.exr");
}

std::vector<ImagePtr> save_and_reload(const Image &img) { return save_and_reload(img, exr_default_save_options(img)); }

} // namespace

TEST_CASE("EXR save/load round-trips a non-origin data window and an independent display window exactly")
{
    auto img = make_test_image(int2{4, 3}, Box2i{int2{10, 20}, int2{14, 23}}, Box2i{int2{0, 0}, int2{14, 23}});

    auto reloaded = save_and_reload(*img);
    REQUIRE(reloaded.size() == 1);

    CHECK(reloaded[0]->data_window == img->data_window);
    CHECK(reloaded[0]->display_window == img->display_window);
}

TEST_CASE("EXR save/load round-trips chromaticities when present, and omits them when absent")
{
    SUBCASE("present")
    {
        auto img            = make_test_image(int2{2, 2});
        img->chromaticities = Chromaticities{{0.680f, 0.320f}, {0.265f, 0.690f}, {0.150f, 0.060f}, {0.3127f, 0.3290f}};

        auto reloaded = save_and_reload(*img);
        REQUIRE(reloaded.size() == 1);
        REQUIRE(reloaded[0]->chromaticities.has_value());
        CHECK(approx_equal(*reloaded[0]->chromaticities, *img->chromaticities));
    }

    SUBCASE("absent")
    {
        auto img = make_test_image(int2{2, 2});
        REQUIRE_FALSE(img->chromaticities.has_value());

        auto reloaded = save_and_reload(*img);
        REQUIRE(reloaded.size() == 1);
        CHECK_FALSE(reloaded[0]->chromaticities.has_value());
    }
}

TEST_CASE("EXR load respects channel_selector, including selectors matching nothing")
{
    auto img = make_test_image(int2{2, 2});

    SUBCASE("selecting a single channel")
    {
        std::ostringstream out(std::ios::binary);
        auto                opts = exr_default_save_options(*img);
        save_exr_image(*img, out, "test.exr", &opts);

        std::istringstream    in(out.str(), std::ios::binary);
        ImageLoadOptions       load_opts;
        load_opts.channel_selector = "R";
        auto reloaded              = load_exr_image(in, "test.exr", load_opts);

        REQUIRE(reloaded.size() == 1);
        REQUIRE(reloaded[0]->channels.size() == 1);
        CHECK(reloaded[0]->channels[0].name == "R");
    }

    SUBCASE("selector matching no channels yields no images")
    {
        std::ostringstream out(std::ios::binary);
        auto                opts = exr_default_save_options(*img);
        save_exr_image(*img, out, "test.exr", &opts);

        std::istringstream    in(out.str(), std::ios::binary);
        ImageLoadOptions       load_opts;
        load_opts.channel_selector = "NoSuchChannel";
        auto reloaded              = load_exr_image(in, "test.exr", load_opts);

        CHECK(reloaded.empty());
    }
}

TEST_CASE("EXR save/load precision depends on the chosen pixel type")
{
    // 0.1f has no exact half-precision representation, so it should survive HALF encoding only approximately, but
    // FLOAT encoding exactly (well within float epsilon).
    auto img = std::make_shared<Image>();
    img->channels.emplace_back("Y", int2{1, 1});
    img->channels.back()(0, 0) = 0.1f;
    img->finalize();

    SUBCASE("HALF (default)")
    {
        auto opts        = exr_default_save_options(*img);
        opts.pixel_type  = 1; // HALF
        auto reloaded    = save_and_reload(*img, opts);
        REQUIRE(reloaded.size() == 1);
        float value = reloaded[0]->channels[0](0, 0);
        CHECK(value != doctest::Approx(0.1f).epsilon(1e-6));  // not exact
        CHECK(value == doctest::Approx(0.1f).epsilon(1e-3));  // but close, within half precision
    }

    SUBCASE("FLOAT")
    {
        auto opts       = exr_default_save_options(*img);
        opts.pixel_type = 0; // FLOAT
        auto reloaded   = save_and_reload(*img, opts);
        REQUIRE(reloaded.size() == 1);
        CHECK(reloaded[0]->channels[0](0, 0) == doctest::Approx(0.1f).epsilon(1e-6));
    }
}

TEST_CASE("is_exr_image correctly identifies real EXR bytes and rejects garbage")
{
    auto img = make_test_image(int2{1, 1});

    std::ostringstream out(std::ios::binary);
    auto                opts = exr_default_save_options(*img);
    save_exr_image(*img, out, "test.exr", &opts);

    std::istringstream valid(out.str(), std::ios::binary);
    CHECK(is_exr_image(valid, "test.exr"));

    std::istringstream garbage("this is definitely not an EXR file", std::ios::binary);
    CHECK_FALSE(is_exr_image(garbage, "garbage.exr"));

    std::istringstream empty("", std::ios::binary);
    CHECK_FALSE(is_exr_image(empty, "empty.exr"));
}

// The following tests need real-world EXR files that HDRView's own save_exr_image can't produce (it never writes
// multi-part files or subsampled channels), so they use OpenEXR's own vendored test image set instead. Only
// compiled in when CMake found that data (see the HDRVIEW_TEST_OPENEXR_DIR check in CMakeLists.txt) - i.e. only
// for -cpm/-universal presets, which are the only ones that fetch OpenEXR's source (and therefore its test
// images) at all.
#ifdef HDRVIEW_TEST_OPENEXR_DIR

namespace
{

std::ifstream open_test_exr(const char *filename)
{
    std::string path = std::string(HDRVIEW_TEST_OPENEXR_DIR) + "/" + filename;
    std::ifstream file(path, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "Could not open vendored test EXR: ", path);
    return file;
}

} // namespace

TEST_CASE("EXR load splits a real multi-part file into one Image per part, with correct part names")
{
    auto file     = open_test_exr("multipart.0001.exr");
    auto reloaded = load_exr_image(file, "multipart.0001.exr");

    REQUIRE(reloaded.size() == 10);

    std::set<std::string> expected_names = {"rgba_right",        "depth_left",  "forward_left", "whitebarmask_left",
                                            "rgba_left",         "depth_right", "forward_right", "disparityL",
                                            "disparityR",        "whitebarmask_right"};
    std::set<std::string> actual_names;
    for (const auto &img : reloaded) actual_names.insert(img->partname);
    CHECK(actual_names == expected_names);

    auto depth_left =
        std::find_if(reloaded.begin(), reloaded.end(), [](const ImagePtr &img) { return img->partname == "depth_left"; });
    REQUIRE(depth_left != reloaded.end());
    REQUIRE((*depth_left)->channels.size() == 1);
    CHECK((*depth_left)->channels[0].name == "Z");
}

TEST_CASE("EXR load up-samples subsampled channels to full resolution with block-consistent nearest-neighbor values")
{
    auto file     = open_test_exr("Flowers.exr");
    auto reloaded = load_exr_image(file, "Flowers.exr");

    REQUIRE(reloaded.size() == 1);
    auto &img = *reloaded[0];

    auto find_channel = [&](const char *name) -> Channel &
    {
        auto it = std::find_if(img.channels.begin(), img.channels.end(),
                               [&](const Channel &c) { return c.name == name; });
        REQUIRE(it != img.channels.end());
        return *it;
    };

    Channel &y  = find_channel("Y");  // full resolution (sampling 1x1)
    Channel &ry = find_channel("RY"); // subsampled 2x2 in the file
    Channel &by = find_channel("BY"); // subsampled 2x2 in the file

    // all channels end up at the same, full data-window resolution regardless of native EXR sampling rate
    CHECK(y.size().x == ry.size().x);
    CHECK(y.size().y == ry.size().y);
    CHECK(y.size().x == by.size().x);
    CHECK(y.size().y == by.size().y);

    // nearest-neighbor up-res of a 2x2-subsampled channel means each 2x2 block of output pixels shares one source
    // value; check a couple of blocks well inside the image bounds
    for (int2 block_origin : {int2{10, 10}, int2{40, 60}})
    {
        INFO("block_origin = ", block_origin.x, ",", block_origin.y);
        float v00 = ry(block_origin.x, block_origin.y);
        CHECK(ry(block_origin.x + 1, block_origin.y) == doctest::Approx(v00));
        CHECK(ry(block_origin.x, block_origin.y + 1) == doctest::Approx(v00));
        CHECK(ry(block_origin.x + 1, block_origin.y + 1) == doctest::Approx(v00));
    }
}

TEST_CASE("EXR load doesn't crash across the full vendored real-world test image set")
{
    namespace fs = std::filesystem;

    int checked = 0;
    for (const auto &entry : fs::directory_iterator(HDRVIEW_TEST_OPENEXR_DIR))
    {
        if (entry.path().extension() != ".exr")
            continue;
        // deep images (multi-sample-per-pixel) are a fundamentally different EXR image type that HDRView doesn't
        // support loading (it only handles scanline/tiled images via the regular FrameBuffer API); skip them
        // rather than asserting HDRView can do something it never claimed to.
        if (entry.path().filename().string().find(".deep.") != std::string::npos)
            continue;

        INFO("file = ", entry.path().filename().string());
        std::ifstream file(entry.path(), std::ios::binary);
        REQUIRE(file.good());

        auto reloaded = load_exr_image(file, entry.path().filename().string());
        CHECK_FALSE(reloaded.empty());
        for (const auto &img : reloaded) CHECK_FALSE(img->channels.empty());

        ++checked;
    }
    CHECK(checked > 0); // sanity: make sure the directory iteration actually found and checked something
}

#endif // HDRVIEW_TEST_OPENEXR_DIR
