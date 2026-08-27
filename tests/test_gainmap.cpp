//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/gainmap.h"
#include "imageio/heif.h"
#include "imageio/image_loader.h"
#include "imageio/uhdr.h"

#if HDRVIEW_ENABLE_LIBHEIF
#include <libheif/heif.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{

//! A flat RGB image, so that any variation after applying a gain map came from the map.
ImagePtr make_flat_image(int2 size, float value)
{
    auto img = std::make_shared<Image>(size, 3);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) img->channels[c](x, y) = value;
    return img;
}

//! A monochrome gain map holding one constant sRGB-encoded value.
GainmapImage make_flat_gainmap(int2 size, float encoded)
{
    GainmapImage gm;
    gm.size     = size;
    gm.channels = 1;
    gm.pixels.assign(size_t(size.x) * size.y, encoded);
    return gm;
}

int channel_index(const Image &img, const std::string &name)
{
    for (int i = 0; i < (int)img.channels.size(); ++i)
        if (img.channels[i].name == name)
            return i;
    return -1;
}

} // namespace

TEST_CASE("Apple gain-map strength follows the published piecewise fit")
{
    // The two branches of the fit are selected by whether the headroom field reaches 1.0, and within
    // each branch by whether the gain field is past the 0.01 knee.
    SUBCASE("low headroom, gain below the knee")
    {
        AppleGainmapParams p{0.5f, 0.f};
        CHECK(p.stops() == doctest::Approx(1.8f));
    }
    SUBCASE("low headroom, gain past the knee")
    {
        AppleGainmapParams p{0.5f, 1.f};
        CHECK(p.stops() == doctest::Approx(1.5f));
    }
    SUBCASE("high headroom, gain below the knee")
    {
        AppleGainmapParams p{1.5f, 0.f};
        CHECK(p.stops() == doctest::Approx(3.f));
    }
    SUBCASE("high headroom, gain past the knee")
    {
        AppleGainmapParams p{1.5f, 1.f};
        CHECK(p.stops() == doctest::Approx(2.f));
    }

    SUBCASE("the defaults are the weakest reconstruction Apple's software falls back to")
    {
        AppleGainmapParams p;
        CHECK(p.stops() == doctest::Approx(0.793f));
    }

    SUBCASE("the file on which this feature was developed asks for 1.8 stops")
    {
        // IMG_4816.HEIC: maker note 0x21 = 0.8432090282, 0x30 = 0.
        AppleGainmapParams p{0.8432090282f, 0.f};
        CHECK(p.stops() == doctest::Approx(1.8f));
    }
}

TEST_CASE("Applying an Apple gain map scales the base image by the reconstructed headroom")
{
    const int2 size{8, 4};

    SUBCASE("a fully-on map brightens by the full headroom")
    {
        auto img = make_flat_image(size, 0.25f);
        // 1.0 encodes to 1.0 under any sane transfer function, so the gain here is exactly 1.
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);

        // stops = 1.8, so headroom = 2^1.8, and gain 1 means the base is scaled by the whole of it.
        const float expected = 0.25f * std::exp2(1.8f);
        CHECK(img->channels[0](0, 0) == doctest::Approx(expected));
        CHECK(img->channels[2](7, 3) == doctest::Approx(expected));
    }

    SUBCASE("a fully-off map leaves the base image alone")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 0.f), AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
    }

    SUBCASE("a target of zero stops leaves the base image alone, but still yields the map")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, 0.f);

        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
        REQUIRE(channel_index(*img, "gainmap.Y") >= 0);
    }

    SUBCASE("the target caps a map that asks for more")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, 1.f);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f * 2.f)); // capped at 1 stop
    }

    SUBCASE("the target does not raise a map that asks for less")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, 10.f);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f * std::exp2(1.8f)));
    }

    SUBCASE("alpha is not a color and is not scaled")
    {
        auto img = std::make_shared<Image>(size, 4);
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[c](x, y) = 0.5f;

        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);

        REQUIRE(img->channels[3].name == "A");
        CHECK(img->channels[3](0, 0) == doctest::Approx(0.5f));
        CHECK(img->channels[0](0, 0) > 0.5f);
    }
}

TEST_CASE("A gain map is appended as its own channel group, resized to the base image")
{
    const int2 size{16, 8};
    auto       img = make_flat_image(size, 0.5f);

    // Quarter resolution in each axis, as gain maps are usually stored.
    apply_apple_gainmap(*img, make_flat_gainmap(int2{4, 2}, 1.f), AppleGainmapParams{0.5f, 0.f}, 0.f);

    const int gm = channel_index(*img, "gainmap.Y");
    REQUIRE(gm >= 0);
    CHECK(img->channels[gm].size() == size);

    // The map was uniform, so every resampled value should still be the linearized 1.0.
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) CHECK(img->channels[gm](x, y) == doctest::Approx(1.f));
}

TEST_CASE("A malformed gain map is ignored rather than corrupting the base image")
{
    const int2 size{4, 4};

    SUBCASE("empty map")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, GainmapImage{}, AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);
        CHECK(img->channels.size() == 3);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
    }

    SUBCASE("map claiming more pixels than it holds")
    {
        auto         img = make_flat_image(size, 0.25f);
        GainmapImage gm;
        gm.size     = int2{64, 64};
        gm.channels = 1;
        gm.pixels.assign(4, 1.f);
        apply_apple_gainmap(*img, gm, AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);
        CHECK(img->channels.size() == 3);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
    }
}

#if HDRVIEW_ENABLE_LIBHEIF
TEST_CASE("An Apple HEIC's gain map is found and applied" * doctest::skip(false))
{
    // Apple gain maps need a real capture to exercise: the aux-image plumbing, the maker-note read,
    // and the map itself all have to line up, and none of that is reachable from a synthetic file
    // this suite could build. Point HDRVIEW_TEST_APPLE_HEIC at an iPhone HEIC to run this.
    const char *path = std::getenv("HDRVIEW_TEST_APPLE_HEIC");
    if (!path)
    {
        MESSAGE("HDRVIEW_TEST_APPLE_HEIC is unset; skipping the real-capture gain-map test.");
        return;
    }

    std::ifstream is{path, std::ios_base::binary};
    REQUIRE_MESSAGE(is.good(), "cannot open HDRVIEW_TEST_APPLE_HEIC");

    // HEIC decoding needs an HEVC plugin, which some presets leave out on patent grounds
    // (HDRVIEW_ENABLE_HEIC=OFF). Nothing about the gain map is testable then.
    if (!heif_have_decoder_for_format(heif_compression_HEVC))
    {
        MESSAGE("this build has no HEVC decoder; skipping the real-capture gain-map test.");
        return;
    }

    ImageLoadOptions opts;
    opts.gainmap_headroom = k_full_gainmap_headroom;
    auto images           = load_heif_image(is, path, opts);
    REQUIRE(!images.empty());

    auto &img = *images.front();

    REQUIRE_MESSAGE(channel_index(img, "gainmap.Y") >= 0, "no gain map was extracted from the file");
    CHECK(img.metadata["header"].contains("Gain map"));
    CHECK(img.channels[channel_index(img, "gainmap.Y")].size() == img.channels[0].size());

    // Reloading with the map suppressed must give strictly darker (or equal) color pixels.
    is.clear();
    is.seekg(0);
    ImageLoadOptions base_opts;
    base_opts.gainmap_headroom = 0.f;
    auto base_images           = load_heif_image(is, path, base_opts);
    REQUIRE(!base_images.empty());

    auto &base = *base_images.front();
    REQUIRE(base.channels[0].size() == img.channels[0].size());

    // The gain is a scalar >= 1, so it scales every sample away from zero. Comparing magnitudes
    // rather than signed values matters: a wide-gamut capture carries some slightly negative
    // samples, and scaling those up makes them more negative, not larger.
    const int2 size      = img.channels[0].size();
    bool       amplified = false;
    bool       shrank    = false;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            const float with = std::abs(img.channels[0](x, y)), without = std::abs(base.channels[0](x, y));
            if (with < without * 0.999f)
                shrank = true;
            if (with > without * 1.01f)
                amplified = true;
        }

    // One CHECK apiece rather than one per pixel: twelve million assertions take longer to report
    // than the decode takes to run.
    CHECK_MESSAGE(!shrank, "applying the gain map darkened part of the image");
    CHECK_MESSAGE(amplified, "applying the gain map changed nothing");

    // No pixel may gain more than the map asks for: the gain is 1 + (2^stops - 1) * g with g in
    // [0,1], so 2^stops is the ceiling however bright the scene is.
    const float stops = img.metadata["header"]["Gain map headroom"]["value"].get<float>();
    float       peak  = 0.f;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            const float without = std::abs(base.channels[0](x, y));
            if (without > 1e-4f)
                peak = std::max(peak, std::abs(img.channels[0](x, y)) / without);
        }

    MESSAGE("gain map asks for ", stops, " stops; brightest pixel gained ", peak, "x");
    CHECK(peak <= doctest::Approx(std::exp2(stops)).epsilon(0.01));
    CHECK(peak > 1.f);
}
#endif

#if HDRVIEW_ENABLE_LIBUHDR
TEST_CASE("An UltraHDR gain map is extracted at the base image's resolution")
{
    // libuhdr hands the gain map back at the reduced resolution the file stores it at, so HDRView has
    // to expand it. These dimensions are chosen so the reduction rounds: 100x60 at a scale of 8 comes
    // back as 12x7, whose ratios to the base are not whole numbers. Expanding by an integer ratio
    // runs off the end of the decoded map, leaving the last rows and columns of the channel at zero
    // -- which reads as "this part of the image needs no brightening at all".
    const int2 size{100, 60};
    const int  scale = 8;

    auto img = std::make_shared<Image>(size, 3);
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            // A bright wedge on the left, so the map has something to encode everywhere down the frame.
            const float v = x < size.x / 2 ? 8.f : 0.25f;
            for (int c = 0; c < 3; ++c) img->channels[c](x, y) = v;
        }
    img->finalize(); // as_interleaved(), which the writer uses, reads img->groups

    std::stringstream ss;
    REQUIRE_NOTHROW(save_uhdr_image(*img, ss, "test.jpg", 1.f, 95, 95, false, scale));
    REQUIRE(ss.tellp() > 0);

    ss.clear();
    ss.seekg(0);
    auto loaded = load_uhdr_image(ss, "test.jpg");
    REQUIRE(loaded.size() == 1);

    auto     &out = *loaded.front();
    const int gm =
        channel_index(out, "gainmap.Y") >= 0 ? channel_index(out, "gainmap.Y") : channel_index(out, "gainmap.R");
    REQUIRE_MESSAGE(gm >= 0, "no gain map channel group was extracted");

    CHECK(out.channels[gm].size() == out.channels[0].size());

    // Every row has to have been written, including the last. A partial expansion leaves the bottom
    // of the channel at zero, which reads as "this part of the image needs no brightening at all".
    int empty_rows = 0;
    for (int y = 0; y < size.y; ++y)
    {
        float row = 0.f;
        for (int x = 0; x < size.x; ++x) row += std::abs(out.channels[gm](x, y));
        if (row == 0.f)
            ++empty_rows;
    }
    CHECK_MESSAGE(empty_rows == 0, "the expanded gain map has ", empty_rows, " unwritten row(s) of ", size.y);

    // The wedge should still be visible after the expansion.
    float left = 0.f, right = 0.f;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) (x < size.x / 2 ? left : right) += out.channels[gm](x, y);

    CHECK(left != doctest::Approx(right));
}
#endif
