//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/icc.h"
#include "imageio/image_loader.h"
#include "imageio/png.h"

#include "test_support.h"

using namespace hdrview_test;

// For PNG_cICP_SUPPORTED. libpng gained the cICP chunk in 1.6.45 and Ubuntu 24.04 still ships 1.6.43, so a
// -local build can link a libpng without it; src/imageio/png.cpp guards png_set_cICP() on the same macro,
// and without the chunk a saved PNG records no transfer function at all.
#include <png.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace
{

// An RGB image with a value per pixel/channel that uniquely identifies it.
ImagePtr make_test_image(int2 size)
{
    return test_image(size, 3, [](int c, int x, int y) { return (float(x + 100 * y) + float(c)) / 1000.f; });
}

} // namespace

TEST_CASE("PNG save records the transfer function in gAMA, sRGB or an ICC profile, not only in cICP")
{
    // cICP is recent enough that most readers ignore it, so every transfer function gAMA or the sRGB chunk can
    // express is written through those too, and every other SDR one in an ICC profile
    auto img = make_test_image(int2{4, 3});

    SUBCASE("a transfer function gAMA and the sRGB chunk cannot state exactly gets an ICC profile, and no other does")
    {
        for (int i = TransferFunction::Linear; i < TransferFunction::Count; ++i)
        {
            const TransferFunction tf{(TransferFunction::Type_)i, 2.4f};
            if (tf.type == TransferFunction::BT2100_PQ || tf.type == TransferFunction::BT2100_HLG)
                continue;
            CAPTURE(transfer_function_name(tf));
            std::ostringstream out(std::ios::binary);
            save_png_image(*img, out, "test.png", /*dither*/ false, /*interlaced*/ false, /*sixteen_bit*/ true, tf);
            auto reloaded = load_bytes(load_png_image, out.str(), "test.png");
            REQUIRE(reloaded);

            const bool stated = tf.type == TransferFunction::Linear || tf.type == TransferFunction::Gamma ||
                                tf.type == TransferFunction::sRGB || tf.type == TransferFunction::ITU;
            if (stated || !ICCProfile::can_describe(tf))
            {
                CHECK(reloaded->icc_data.empty());
                continue;
            }
            REQUIRE_FALSE(reloaded->icc_data.empty());

            // the profile decodes code values as the transfer function does, up to the white an ICC curve ends at
            ICCProfile         profile{reloaded->icc_data};
            constexpr int      num = 32;
            std::vector<float> ramp(num * 3);
            for (int j = 0; j < num * 3; ++j) ramp[j] = float(j / 3) / (num - 1);
            REQUIRE(profile.linearize_pixels(ramp.data(), int3{num, 1, 3}, true));
            float worst = 0.f;
            for (int j = 0; j < num * 3; ++j)
                worst =
                    std::max(worst, std::abs(ramp[j] - std::clamp(to_linear(float(j / 3) / (num - 1), tf), 0.f, 1.f)));
            CHECK(worst <= 2e-3f);
        }
    }

    SUBCASE("gAMA and the sRGB chunk state what they can, and claim nothing they cannot")
    {
        // The loader reports gAMA as the decoding exponent, 1e5 over the stored value, and an absent one as NaN.
        // The sRGB chunk asserts BT.709's primaries as well as the transfer function, so a wide gamut takes gAMA
        // and cHRM instead.
        [[maybe_unused]] constexpr float none = std::numeric_limits<float>::quiet_NaN(); // only cICP's cases
        struct Case
        {
            TransferFunction tf;
            bool             wide;
            float            gamma; ///< what gAMA must say, or none
            bool             srgb;  ///< whether the sRGB chunk must be present
        };
        const Case cases[] = {
            {TransferFunction::Linear, false, 1.f, false},
            {{TransferFunction::Gamma, 2.4f}, false, 2.4f, false},
            {TransferFunction::sRGB, false, 2.2f, true}, // gAMA too, for readers that honor neither cICP nor sRGB
            {TransferFunction::sRGB, true, 2.2f, false},
            {TransferFunction::ITU, false, 2.4f, false}, // BT.1886's gamma 2.4, which is not sRGB
#ifdef PNG_cICP_SUPPORTED
            // with no power-curve equivalent, nothing lets a gAMA-only reader believe it knows the transfer function
            {TransferFunction::BT2100_PQ, false, none, false},
            {TransferFunction::BT2100_HLG, false, none, false},
#endif
        };
        for (const auto &c : cases)
        {
            CAPTURE(transfer_function_name(c.tf));
            CAPTURE(c.wide);
            auto image = make_test_image(int2{4, 3});
            if (c.wide)
                image->chromaticities = gamut_chromaticities(ColorGamut_BT2020_2100);

            std::ostringstream out(std::ios::binary);
            save_png_image(*image, out, "test.png", /*dither*/ false, /*interlaced*/ false, /*sixteen_bit*/ true, c.tf);
            auto reloaded = load_bytes(load_png_image, out.str(), "test.png");
            REQUIRE(reloaded);
            auto &header = reloaded->metadata["header"];

            REQUIRE(header.contains("gAMA"));
            const float gamma = header["gAMA"]["value"].get<float>();
            if (std::isnan(c.gamma))
                CHECK(std::isnan(gamma));
            else
                CHECK(gamma == doctest::Approx(c.gamma).epsilon(1e-3));
            REQUIRE(header.contains("sRGB"));
            CHECK((header["sRGB"]["value"].get<int>() >= 0) == c.srgb); // a rendering intent, or -1 for none
        }
    }
}

#ifndef PNG_cICP_SUPPORTED
TEST_CASE("saving a transfer function this libpng cannot record fails instead of mislabeling the file")
{
    // without cICP there is nowhere to put PQ; only reachable on a libpng older than 1.6.45
    auto img = make_test_image(int2{4, 3});

    std::ostringstream out(std::ios::binary);
    CHECK_THROWS_AS(save_png_image(*img, out, "test.png", /*dither*/ false, /*interlaced*/ false,
                                   /*sixteen_bit*/ true, TransferFunction::BT2100_PQ),
                    std::runtime_error);
}
#endif // PNG_cICP_SUPPORTED

namespace
{

std::string zlib_compressed(const std::string &bytes)
{
    uLongf      size = compressBound((uLong)bytes.size());
    std::string out(size, '\0');
    REQUIRE(compress((Bytef *)&out[0], &size, (const Bytef *)bytes.data(), (uLong)bytes.size()) == Z_OK);
    out.resize(size);
    return out;
}

/// A 256x1 8-bit gray PNG holding every code value in order, with \p color_chunks ahead of its pixels.
std::string gray_ramp_png(const std::string &color_chunks)
{
    std::string ihdr;
    put(ihdr, (uint32_t)256, Endian::Big);
    put(ihdr, (uint32_t)1, Endian::Big);
    ihdr += std::string("\x08\x00\x00\x00\x00", 5); // 8-bit gray, deflate, adaptive filters, not interlaced

    std::string scanline(1, '\0'); // filter type None
    for (int i = 0; i < 256; ++i) scanline += char(i);

    return std::string("\x89PNG\r\n\x1a\n", 8) + png_chunk("IHDR", ihdr) + color_chunks +
           png_chunk("IDAT", zlib_compressed(scanline)) + png_chunk("IEND", "");
}

std::string gama_chunk(uint32_t times_1e5)
{
    std::string data;
    put(data, times_1e5, Endian::Big);
    return png_chunk("gAMA", data);
}

std::string srgb_chunk() { return png_chunk("sRGB", std::string(1, '\0')); }

std::string iccp_chunk(const std::vector<uint8_t> &profile)
{ return png_chunk("iCCP", std::string("icc\0\0", 5) + zlib_compressed(std::string(profile.begin(), profile.end()))); }

#ifdef PNG_cICP_SUPPORTED
/// A cICP chunk naming BT.709's primaries, full-range RGB, and transfer characteristics \p tc.
std::string cicp_chunk(uint8_t tc) { return png_chunk("cICP", std::string{'\x01', char(tc), '\0', '\x01'}); }
#endif

} // namespace

TEST_CASE("PNG load follows the color-profile chunk priority: cICP, then ICC, then sRGB, then gAMA")
{
    // each file carries chunks that disagree, so only the one with priority decodes the ramp as it says
    const std::string linear_icc = iccp_chunk(ICCProfile::linear_Gray().bytes());
    struct Case
    {
        const char      *label;
        std::string      chunks;
        TransferFunction tf;
    };
    const Case cases[] = {
        {"none, which is read as sRGB", "", TransferFunction::sRGB},
        {"gAMA alone", gama_chunk(38462), {TransferFunction::Gamma, 2.6f}},
        {"sRGB over a gAMA saying linear", srgb_chunk() + gama_chunk(100000), TransferFunction::sRGB},
        {"an ICC profile over a gAMA saying 2.2", linear_icc + gama_chunk(45455), TransferFunction::Linear},
#ifdef PNG_cICP_SUPPORTED
        {"cICP over sRGB and gAMA", cicp_chunk(8) + srgb_chunk() + gama_chunk(45455), TransferFunction::Linear},
        {"cICP over an ICC profile", cicp_chunk(13) + linear_icc, TransferFunction::sRGB},
#endif
    };

    for (const auto &c : cases)
    {
        CAPTURE(std::string{c.label});
        auto img = load_bytes(load_png_image, gray_ramp_png(c.chunks), "ramp.png");
        REQUIRE(img);
        REQUIRE(img->channels.size() == 1);
        REQUIRE(img->size() == int2{256, 1});

        float worst = 0.f;
        for (int x = 0; x < 256; ++x)
            worst = std::max(worst, std::abs(img->channels[0](x, 0) - to_linear(x / 255.f, c.tf)));
        CHECK(worst <= 2e-3f);
    }
}

// The tests below need libpng's own PngSuite and testpngs image sets, which save_png_image cannot reproduce
// (it writes no palette/indexed files). Only compiled in when CMake found that data, i.e. for the -cpm and
// -universal presets that fetch libpng from source.
#ifdef HDRVIEW_TEST_PNG_CONTRIB_DIR

namespace
{

/// The vendored test PNG at `subdir/filename`, which has to decode to exactly one image.
ImagePtr load_test_png(const std::string &subdir, const std::string &filename, const ImageLoadOptions &opts = {})
{
    std::string   path = std::string(HDRVIEW_TEST_PNG_CONTRIB_DIR) + "/" + subdir + "/" + filename;
    std::ifstream file(path, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "Could not open vendored test PNG: ", path);
    auto images = load_png_image(file, filename, opts);
    REQUIRE(images.size() == 1);
    return images.front();
}

} // namespace

TEST_CASE("PNG load handles the full PngSuite bit-depth/color-type matrix without crashing")
{
    // one file per PngSuite color-type code: 0g=gray, 2c=truecolor, 3p=palette (expanded to RGB), 4a=gray+alpha,
    // 6a=truecolor+alpha. expected_bits is the file's own sample depth, which sets the histogram's bin count;
    // palette entries are 8-bit RGB however few bits the indices took.
    struct Case
    {
        const char *file;
        int         expected_channels;
        int         expected_bits;
    };
    static const Case cases[] = {
        {"basn0g01.png", 1, 1},  {"basn0g02.png", 1, 2}, {"basn0g04.png", 1, 4},  {"basn0g08.png", 1, 8},
        {"basn0g16.png", 1, 16}, {"basn2c08.png", 3, 8}, {"basn2c16.png", 3, 16}, {"basn3p01.png", 3, 8},
        {"basn3p02.png", 3, 8},  {"basn3p04.png", 3, 8}, {"basn3p08.png", 3, 8},  {"basn4a08.png", 2, 8},
        {"basn4a16.png", 2, 16}, {"basn6a08.png", 4, 8}, {"basn6a16.png", 4, 16},
    };

    for (const auto &c : cases)
    {
        INFO("file = ", c.file);
        auto reloaded = load_test_png("pngsuite", c.file);
        CHECK(reloaded->channels.size() == (size_t)c.expected_channels);
        for (const auto &ch : reloaded->channels) CHECK(ch.bits_per_sample == c.expected_bits);
    }
}

TEST_CASE("PNG load produces identical pixels for interlaced and non-interlaced encodings of the same image")
{
    // PngSuite's "basn..."/"ibasn..." pairs hold the same pixels but not the same chunks: basn2c08.png has a
    // gAMA chunk and ibasn2c08.png does not. override_profile+Linear leaves interlacing the only variable.
    ImageLoadOptions raw_opts;
    raw_opts.override_profile = true;
    raw_opts.tf_override      = TransferFunction::Linear;

    static const char *base_names[] = {"0g08", "0g16", "2c08", "2c16", "3p08", "4a08", "4a16", "6a08", "6a16"};

    for (const auto *base : base_names)
    {
        INFO("base = ", base);
        auto plain      = load_test_png("pngsuite", std::string("basn") + base + ".png", raw_opts);
        auto interlaced = load_test_png("pngsuite", std::string("ibasn") + base + ".png", raw_opts);

        REQUIRE(plain->channels.size() == interlaced->channels.size());

        for (size_t c = 0; c < plain->channels.size(); ++c)
        {
            auto &pc = plain->channels[c];
            auto &ic = interlaced->channels[c];
            REQUIRE(pc.size().x == ic.size().x);
            REQUIRE(pc.size().y == ic.size().y);
            for (int y = 0; y < pc.size().y; ++y)
                for (int x = 0; x < pc.size().x; ++x) CHECK(pc(x, y) == doctest::Approx(ic(x, y)));
        }
    }
}

#endif // HDRVIEW_TEST_PNG_CONTRIB_DIR
