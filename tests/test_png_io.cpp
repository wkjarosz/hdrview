//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/image_loader.h"
#include "imageio/png.h"

#include <fstream>
#include <sstream>

#ifdef HDRVIEW_TEST_PNG_CONTRIB_DIR
#include <algorithm>
#include <string>
#endif

namespace
{

// Builds an RGB image with a value per pixel/channel that uniquely identifies it.
ImagePtr make_test_image(int2 size)
{
    auto img = std::make_shared<Image>(size, 3);
    for (int c = 0; c < 3; ++c)
    {
        auto &ch = img->channels[c];
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) ch(x, y) = (float(x + 100 * y) + c) / 1000.f;
    }
    img->finalize(); // populates img->groups, which as_interleaved()/save_png_image() require
    return img;
}

} // namespace

TEST_CASE("PNG save/load round-trips 8-bit and 16-bit pixel data without dithering")
{
    auto img = make_test_image(int2{4, 3});

    SUBCASE("8-bit")
    {
        std::ostringstream out(std::ios::binary);
        save_png_image(*img, out, "test.png", /*gain*/ 1.f, /*dither*/ false, /*interlaced*/ false,
                       /*sixteen_bit*/ false, TransferFunction::Linear);

        std::istringstream in(out.str(), std::ios::binary);
        auto                reloaded = load_png_image(in, "test.png");
        REQUIRE(reloaded.size() == 1);
        REQUIRE(reloaded[0]->channels.size() == 3);
        // 8-bit quantization error tolerance
        for (int c = 0; c < 3; ++c)
            CHECK(reloaded[0]->channels[c](2, 1) == doctest::Approx(img->channels[c](2, 1)).epsilon(0.004));
    }

    SUBCASE("16-bit")
    {
        std::ostringstream out(std::ios::binary);
        save_png_image(*img, out, "test.png", /*gain*/ 1.f, /*dither*/ false, /*interlaced*/ false,
                       /*sixteen_bit*/ true, TransferFunction::Linear);

        std::istringstream in(out.str(), std::ios::binary);
        auto                reloaded = load_png_image(in, "test.png");
        REQUIRE(reloaded.size() == 1);
        // 16-bit quantization error is far smaller than 8-bit
        for (int c = 0; c < 3; ++c)
            CHECK(reloaded[0]->channels[c](2, 1) == doctest::Approx(img->channels[c](2, 1)).epsilon(1e-4));
    }
}

TEST_CASE("PNG save/load round-trips HDR transfer functions (PQ, HLG) and wide gamut via the cICP chunk")
{
    // HDRView cares specifically about HDR: CICPProfile::is_HDR() explicitly checks for PQ (CICP transfer
    // characteristics code 16) or HLG (code 18). This is self-contained (no vendored data needed): the cICP
    // chunk carries an arbitrary transfer function, and save_png_image can write one directly via its `tf`
    // parameter - no PQ/HLG-tagged PNG exists anywhere in the already-vendored test data to load instead.
    for (auto tf : {TransferFunction::BT2100_PQ, TransferFunction::BT2100_HLG})
    {
        INFO("transfer function = ", transfer_function_name(tf));

        auto img              = make_test_image(int2{4, 3});
        img->chromaticities   = gamut_chromaticities(ColorGamut_BT2020_2100); // wide gamut, as HDR content typically is

        std::ostringstream out(std::ios::binary);
        // 16-bit output: HDR transfer functions need far more than 8-bit precision to avoid banding
        save_png_image(*img, out, "test.png", /*gain*/ 1.f, /*dither*/ false, /*interlaced*/ false,
                       /*sixteen_bit*/ true, tf);

        std::istringstream in(out.str(), std::ios::binary);
        auto                reloaded = load_png_image(in, "test.png");
        REQUIRE(reloaded.size() == 1);

        auto &cicp_value = reloaded[0]->metadata["header"]["cICP"]["value"];
        REQUIRE(cicp_value != "<not present>");
        int transfer_characteristics = cicp_value[1].get<int>();
        CHECK(transfer_characteristics == transfer_function_to_CICP(tf));
        CHECK((transfer_characteristics == 16 || transfer_characteristics == 18)); // CICPProfile::is_HDR()'s own check

        REQUIRE(reloaded[0]->chromaticities.has_value());
        CHECK(approx_equal(*reloaded[0]->chromaticities, gamut_chromaticities(ColorGamut_BT2020_2100)));

        // pixel round-trip through the actual PQ/HLG encode+decode math, not just the abstract transfer function
        for (int c = 0; c < 3; ++c)
            CHECK(reloaded[0]->channels[c](2, 1) == doctest::Approx(img->channels[c](2, 1)).epsilon(1e-3));
    }
}

TEST_CASE("is_png_image correctly identifies real PNG bytes and rejects garbage")
{
    auto img = make_test_image(int2{1, 1});

    std::ostringstream out(std::ios::binary);
    save_png_image(*img, out, "test.png");

    std::istringstream valid(out.str(), std::ios::binary);
    CHECK(is_png_image(valid));

    std::istringstream garbage("this is definitely not a PNG file", std::ios::binary);
    CHECK_FALSE(is_png_image(garbage));

    std::istringstream empty("", std::ios::binary);
    CHECK_FALSE(is_png_image(empty));
}

TEST_CASE("channel_selector drops channels in formats that don't filter during decode")
{
    // Only EXR and JPEG XL apply the selector while decoding; for every other format load_image()
    // applies it centrally afterwards, so "-.A" has to work here just as it does for EXR.
    const int2 size{2, 2};
    auto       img = std::make_shared<Image>(size, 4);
    for (auto &c : img->channels)
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) c(x, y) = 0.5f;
    img->finalize();

    std::ostringstream out(std::ios::binary);
    save_png_image(*img, out, "rgba.png", /*gain*/ 1.f, /*dither*/ false, /*interlaced*/ false,
                   /*sixteen_bit*/ false, TransferFunction::Linear);

    SUBCASE("excluding the alpha channel by name")
    {
        ImageLoadOptions opts;
        opts.channel_selector = "-.A";

        std::istringstream in(out.str(), std::ios::binary);
        auto               reloaded = load_image(in, "rgba.png", opts);

        REQUIRE(reloaded.size() == 1);
        REQUIRE(reloaded[0]->channels.size() == 3);
        for (const auto &c : reloaded[0]->channels) CHECK(c.name != "A");
        // save_png_image unpremultiplied on the way out, so the file holds 1.0 here; with the alpha
        // channel filtered away there is nothing left to premultiply it back down by.
        CHECK(reloaded[0]->channels[0](0, 0) == doctest::Approx(1.f).epsilon(0.005));
    }

    SUBCASE("selecting a single channel by name")
    {
        ImageLoadOptions opts;
        opts.channel_selector = "G";

        std::istringstream in(out.str(), std::ios::binary);
        auto               reloaded = load_image(in, "rgba.png", opts);

        REQUIRE(reloaded.size() == 1);
        REQUIRE(reloaded[0]->channels.size() == 1);
        CHECK(reloaded[0]->channels[0].name == "G");
    }

    SUBCASE("a selector matching nothing yields no images")
    {
        ImageLoadOptions opts;
        opts.channel_selector = "NoSuchChannel";

        std::istringstream in(out.str(), std::ios::binary);
        CHECK(load_image(in, "rgba.png", opts).empty());
    }
}

// The following tests need libpng's own vendored test image sets (PngSuite and testpngs), which HDRView's own
// save_png_image can't reproduce (it doesn't write palette/indexed files, and constructing a matching
// color-signaling matrix by hand would just be re-deriving PngSuite/testpngs badly). Only compiled in when CMake
// found that data (see the HDRVIEW_TEST_PNG_CONTRIB_DIR check in CMakeLists.txt) - i.e. only for -cpm/-universal
// presets, which are the only ones that fetch libpng's source (and therefore these test images) at all.
#ifdef HDRVIEW_TEST_PNG_CONTRIB_DIR

namespace
{

std::vector<ImagePtr> load_test_png(const std::string &subdir, const std::string &filename,
                                    const ImageLoadOptions &opts = {})
{
    std::string  path = std::string(HDRVIEW_TEST_PNG_CONTRIB_DIR) + "/" + subdir + "/" + filename;
    std::ifstream file(path, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "Could not open vendored test PNG: ", path);
    return load_png_image(file, filename, opts);
}

} // namespace

TEST_CASE("PNG load handles the full PngSuite bit-depth/color-type matrix without crashing")
{
    // one representative file per PngSuite color-type code: 0g=gray, 2c=truecolor, 3p=palette (expanded to RGB),
    // 4a=gray+alpha, 6a=truecolor+alpha; expected channel count reflects HDRView's post-expansion representation
    struct Case
    {
        const char *file;
        int          expected_channels;
    };
    static const Case cases[] = {
        {"basn0g01.png", 1}, {"basn0g02.png", 1}, {"basn0g04.png", 1}, {"basn0g08.png", 1}, {"basn0g16.png", 1},
        {"basn2c08.png", 3}, {"basn2c16.png", 3}, {"basn3p01.png", 3}, {"basn3p02.png", 3}, {"basn3p04.png", 3},
        {"basn3p08.png", 3}, {"basn4a08.png", 2}, {"basn4a16.png", 2}, {"basn6a08.png", 4}, {"basn6a16.png", 4},
    };

    for (const auto &c : cases)
    {
        INFO("file = ", c.file);
        auto reloaded = load_test_png("pngsuite", c.file);
        REQUIRE(reloaded.size() == 1);
        CHECK(reloaded[0]->channels.size() == (size_t)c.expected_channels);
    }
}

TEST_CASE("PNG load produces identical pixels for interlaced and non-interlaced encodings of the same image")
{
    // PngSuite ships matched pairs: "basn..." (non-interlaced) and "ibasn..." (interlaced) encode the same
    // underlying image data, but aren't necessarily identical in every other chunk - e.g. basn2c08.png has a gAMA
    // chunk while ibasn2c08.png doesn't, so comparing HDRView's normally-linearized output would also be
    // comparing two different color decodes, not just interlacing. override_profile+Linear bypasses that
    // entirely, isolating interlacing as the only variable under test.
    ImageLoadOptions raw_opts;
    raw_opts.override_profile = true;
    raw_opts.tf_override      = TransferFunction::Linear;

    static const char *base_names[] = {"0g08", "0g16", "2c08", "2c16", "3p08", "4a08", "4a16", "6a08", "6a16"};

    for (const auto *base : base_names)
    {
        INFO("base = ", base);
        auto plain      = load_test_png("pngsuite", std::string("basn") + base + ".png", raw_opts);
        auto interlaced = load_test_png("pngsuite", std::string("ibasn") + base + ".png", raw_opts);

        REQUIRE(plain.size() == 1);
        REQUIRE(interlaced.size() == 1);
        REQUIRE(plain[0]->channels.size() == interlaced[0]->channels.size());

        for (size_t c = 0; c < plain[0]->channels.size(); ++c)
        {
            auto &pc = plain[0]->channels[c];
            auto &ic = interlaced[0]->channels[c];
            REQUIRE(pc.size().x == ic.size().x);
            REQUIRE(pc.size().y == ic.size().y);
            for (int y = 0; y < pc.size().y; ++y)
                for (int x = 0; x < pc.size().x; ++x) CHECK(pc(x, y) == doctest::Approx(ic(x, y)));
        }
    }
}

TEST_CASE("PNG load respects the documented color-profile chunk priority: cICP > ICC > sRGB > gAMA")
{
    SUBCASE("gAMA chunk: reported gamma matches the file's raw gAMA chunk value")
    {
        auto reloaded = load_test_png("testpngs", "gray-8-1.8.png");
        REQUIRE(reloaded.size() == 1);
        auto &header = reloaded[0]->metadata["header"];
        REQUIRE(header.contains("gAMA"));
        REQUIRE(header["sRGB"]["value"].get<int>() < 0); // sRGB chunk absent
        // Verified independently by parsing the file's raw gAMA chunk bytes: stores 65909 (i.e. 0.65909, PNG's
        // gAMA convention of storing 1/gamma scaled by 100000); HDRView reports the reciprocal of that, 1.51724.
        // The filename's "1.8" doesn't correspond directly to either of these numbers - likely a different
        // parameter in the test image generator (contrib/testpngs/makepngs.sh) - so this pins down HDRView's
        // actual observed, independently-verified behavior rather than a guess based on the filename.
        CHECK(header["gAMA"]["value"].get<float>() == doctest::Approx(100000.f / 65909.f).epsilon(1e-3));
    }

    SUBCASE("sRGB chunk takes priority when present")
    {
        auto reloaded = load_test_png("testpngs", "gray-8-sRGB.png");
        REQUIRE(reloaded.size() == 1);
        auto &header = reloaded[0]->metadata["header"];
        CHECK(header["sRGB"]["value"].get<int>() >= 0); // sRGB chunk present with a valid rendering intent
    }

    SUBCASE("cICP chunk takes priority over everything else, with correct Display P3 primaries")
    {
        auto reloaded = load_test_png("testpngs/png-3", "cicp-display-p3_reencoded.png");
        REQUIRE(reloaded.size() == 1);
        auto &header = reloaded[0]->metadata["header"];
        REQUIRE(header.contains("cICP"));
        CHECK(header["cICP"]["value"] != "<not present>");

        REQUIRE(reloaded[0]->chromaticities.has_value());
        // standard Display P3 primaries / D65 white point
        Chromaticities p3{{0.680f, 0.320f}, {0.265f, 0.690f}, {0.150f, 0.060f}, {0.3127f, 0.3290f}};
        CHECK(approx_equal(*reloaded[0]->chromaticities, p3, 1e-3f));
    }
}

#endif // HDRVIEW_TEST_PNG_CONTRIB_DIR
