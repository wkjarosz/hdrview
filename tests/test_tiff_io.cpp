//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"
#include "image.h"
#include "imageio/image_loader.h"
#include "imageio/tiff.h"

#include "test_support.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace hdrview_test;

namespace
{

constexpr uint16_t k_extra_samples_tag = 338;
constexpr float    k_half              = 128.f / 255.f;

/// A 2x1 8-bit RGBA uncompressed TIFF; pixel 0 is opaque white, pixel 1 is `color` at alpha 128/255.
/**
    `extra_samples` is EXTRASAMPLES' value, or nothing at all, as most RGBA TIFFs in the wild look: then
    the file says nothing about what its fourth sample means.
*/
std::string rgba_tiff(std::optional<uint16_t> extra_samples, uint8_t color = 0xff)
{
    std::vector<TiffEntry> entries{
        {256, 4, 1, 2, {}},                       // ImageWidth
        {257, 4, 1, 1, {}},                       // ImageLength
        {258, 3, 4, 0, {8, 0, 8, 0, 8, 0, 8, 0}}, // BitsPerSample
        {259, 3, 1, 1, {}},                       // Compression: none
        {262, 3, 1, 2, {}},                       // Photometric: RGB
        {277, 3, 1, 4, {}},                       // SamplesPerPixel
        {278, 4, 1, 1, {}},                       // RowsPerStrip
        {284, 3, 1, 1, {}},                       // PlanarConfiguration: chunky
    };
    if (extra_samples)
        entries.push_back({k_extra_samples_tag, 3, 1, *extra_samples, {}});

    return tiff_bytes(Endian::Little, entries, {0xff, 0xff, 0xff, 0xff, color, color, color, 0x80});
}

// Reads the samples of an uncompressed, single-strip TIFF out of its strip, so a written file can be checked
// against what other applications produce. A save/load round trip cannot: writer and reader share a convention
// and agree either way.
std::vector<unsigned char> tiff_raw_samples(const std::string &bytes)
{
    auto read_u16 = [&](size_t o) { return uint16_t(uint8_t(bytes[o]) | (uint8_t(bytes[o + 1]) << 8)); };
    auto read_u32 = [&](size_t o) { return uint32_t(read_u16(o)) | (uint32_t(read_u16(o + 2)) << 16); };

    REQUIRE(bytes.compare(0, 2, "II") == 0);
    const uint32_t ifd     = read_u32(4);
    const uint16_t entries = read_u16(ifd);

    uint32_t offset = 0, count = 0, compression = 1;
    for (uint16_t i = 0; i < entries; ++i)
    {
        const size_t   entry = ifd + 2 + size_t(i) * 12;
        const uint16_t tag   = read_u16(entry);
        const uint32_t value = read_u16(entry + 8); // every tag read here is a single SHORT or LONG
        if (tag == 273)
            offset = read_u32(entry + 8);
        else if (tag == 279)
            count = read_u32(entry + 8);
        else if (tag == 259)
            compression = value;
    }

    REQUIRE(compression == 1); // the fixtures below ask for none
    REQUIRE(offset != 0);
    REQUIRE(count != 0);
    return {bytes.begin() + offset, bytes.begin() + offset + count};
}

#ifdef HDRVIEW_TEST_LCMS_TESTBED_DIR
/// A 2x1 8-bit CMYK uncompressed TIFF carrying `icc`; pixel 0 is bare paper, pixel 1 a solid hit of black.
std::string cmyk_tiff(const std::string &icc)
{
    std::vector<TiffEntry> entries{
        {256, 4, 1, 2, {}},                       // ImageWidth
        {257, 4, 1, 1, {}},                       // ImageLength
        {258, 3, 4, 0, {8, 0, 8, 0, 8, 0, 8, 0}}, // BitsPerSample
        {259, 3, 1, 1, {}},                       // Compression: none
        {262, 3, 1, 5, {}},                       // Photometric: SEPARATED, which is to say ink
        {277, 3, 1, 4, {}},                       // SamplesPerPixel
        {278, 4, 1, 1, {}},                       // RowsPerStrip
        {284, 3, 1, 1, {}},                       // PlanarConfiguration: chunky
        {332, 3, 1, 1, {}},                       // InkSet: CMYK
        {34675, 7, (uint32_t)icc.size(), 0, std::vector<uint8_t>(icc.begin(), icc.end())}, // ICCProfile
    };

    return tiff_bytes(Endian::Little, entries, {0, 0, 0, 0, 0, 0, 0, 0xff});
}

/// lcms's own CMYK printer profile, which libjxl carries in-tree.
std::string cmyk_profile()
{
    std::ifstream in(std::string(HDRVIEW_TEST_LCMS_TESTBED_DIR) + "/test1.icc", std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
#endif

ImagePtr load_tiff(const std::string &bytes)
{
    auto img = load_bytes(load_tiff_image, bytes, "rgba.tif");
    REQUIRE(img);
    img->finalize();
    return img;
}

} // namespace

TEST_CASE("TIFF channels report the file's sample depth")
{
    auto img = load_tiff(rgba_tiff(2)); // EXTRASAMPLE_UNASSALPHA
    for (const auto &c : img->channels) CHECK(c.bits_per_sample == 8);
}

TEST_CASE("What a TIFF declares about its fourth sample, and what an override does to it")
{
    // EXTRASAMPLES settles the kind whichever value it carries, including the one saying the sample is data;
    // with the tag gone nothing states a kind and straight is the loader's guess. An override replaces what
    // is used without erasing what the file said, so the Info panel can say the two disagree.
    struct Case
    {
        std::optional<uint16_t>          declared; // EXTRASAMPLES, or nothing
        std::optional<TransparencyType_> override_with;
        TransparencyType_                from_file;
        bool                             assumed;
    };

    const std::vector<Case> cases = {
        {0, {}, TransparencyType_None, false},                   // EXTRASAMPLE_UNSPECIFIED: the sample is not alpha
        {1, {}, TransparencyType_PremultipliedNonLinear, false}, // EXTRASAMPLE_ASSOCALPHA
        {2, {}, TransparencyType_Straight, false},               // EXTRASAMPLE_UNASSALPHA
        {{}, {}, TransparencyType_Straight, true},               // nothing said, so the loader guesses
        {2, TransparencyType_Straight, TransparencyType_Straight, false},
        {2, TransparencyType_None, TransparencyType_Straight, false},
        {2, TransparencyType_PremultipliedNonLinear, TransparencyType_Straight, false},
        {1, TransparencyType_Straight, TransparencyType_PremultipliedNonLinear, false},
        {{}, TransparencyType_PremultipliedLinear, TransparencyType_Straight, true},
    };

    for (const auto &c : cases)
    {
        CAPTURE(c.declared.value_or(0xffff));
        CAPTURE(transparency_type_name(c.override_with.value_or(c.from_file)));

        ImageLoadOptions opts;
        opts.override_transparency = c.override_with.has_value();
        opts.transparency_override = c.override_with.value_or(TransparencyType_None);

        auto img = load_bytes(rgba_tiff(c.declared), "rgba.tif", opts);
        REQUIRE(img);

        const auto used = c.override_with.value_or(c.from_file);
        CHECK(img->transparency == used);
        CHECK(img->transparency_from_file == c.from_file);
        CHECK(img->transparency_assumed == c.assumed);
        CHECK(img->alpha_is_transparency() == (used != TransparencyType_None));

        // a fourth sample that is not transparency stands on its own instead of joining an RGBA group
        REQUIRE(img->groups.size() == (used == TransparencyType_None ? 2u : 1u));
        if (used == TransparencyType_None)
        {
            CHECK(img->groups[0].type == ChannelGroup::RGB_Channels);
            CHECK(img->groups[1].type == ChannelGroup::Single_Channel);
        }
    }

#ifdef HDRVIEW_TEST_LCMS_TESTBED_DIR
    // A separated image's fourth sample is black ink and never coverage, whatever EXTRASAMPLES is absent.
    // Reading it as alpha would make the solid black pixel transparent instead of dark.
    {
        auto img = load_bytes(cmyk_tiff(cmyk_profile()), "cmyk.tif");
        REQUIRE(img);
        CHECK(img->transparency == TransparencyType_None);
        CHECK_FALSE(img->alpha_is_transparency());

        // the profile is what converts the ink, and applying it means the loader read the four samples
        // itself rather than taking libtiff's RGBA interface, which converts them by a formula of its own
        CHECK(img->metadata.value("color profile", std::string{}).find("Test profile") != std::string::npos);
        REQUIRE(img->channels.size() >= 3);
        for (int c = 0; c < 3; ++c)
        {
            CAPTURE(c);
            CHECK(img->channels[c](0, 0) > 0.5f); // bare paper
            CHECK(img->channels[c](1, 0) < 0.2f); // solid black
        }
    }
#endif
}

TEST_CASE("A TIFF's alpha kind decides how many times its samples are multiplied by coverage")
{
    SUBCASE("unassociated alpha is premultiplied exactly once")
    {
        auto img = load_tiff(rgba_tiff(2));

        REQUIRE(img->channels.size() == 4);
        CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));

        // premultiplying twice would leave k_half*k_half here
        CHECK(img->channels[0](0, 0) == doctest::Approx(1.f).epsilon(0.001));
        CHECK(img->channels[0](1, 0) == doctest::Approx(k_half).epsilon(0.001));
    }

    SUBCASE("associated alpha is read as already multiplied into the encoded samples")
    {
        // pixel 1 holds 128/255 under alpha 128/255: a fully bright color at half coverage, as Photoshop,
        // OpenImageIO, ImageMagick and vips all store it (a*OETF(C))
        auto img = load_tiff(rgba_tiff(1, 0x80));

        REQUIRE(img->channels.size() == 4);
        CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));

        // dividing alpha out before the inverse transfer recovers straight 1.0, and multiplying back gives
        // linear 1.0 times alpha; the transfer applied to the stored value would give EOTF(0.502) = 0.214
        CHECK(img->channels[0](1, 0) == doctest::Approx(k_half).epsilon(0.005));
        CHECK(img->channels[0](1, 0) != doctest::Approx(0.214f).epsilon(0.01));

        // the opaque pixel is the control, unaffected by any of this
        CHECK(img->channels[0](0, 0) == doctest::Approx(1.f).epsilon(0.001));
    }

    SUBCASE("an override changes which of the two the samples go through")
    {
        // Read a file the tag says is associated as straight, so finalize() multiplies instead of the loader
        // dividing. ImageMagick and vips both write premultiplied samples tagged unassociated.
        ImageLoadOptions opts;
        opts.override_transparency = true;
        opts.transparency_override = TransparencyType_Straight;

        auto img = load_bytes(rgba_tiff(1, 0x80), "rgba.tif", opts);
        REQUIRE(img);
        // straight 128/255 linearizes to 0.216, which finalize() then multiplies by alpha
        CHECK(img->channels[0](1, 0) == doctest::Approx(0.216f * k_half).epsilon(0.02));
    }

    SUBCASE("a fourth sample that is not transparency is left as it was stored")
    {
        ImageLoadOptions opts;
        opts.override_transparency = true;
        opts.transparency_override = TransparencyType_None;

        auto img = load_bytes(rgba_tiff(2), "rgba.tif", opts);
        REQUIRE(img);
        CHECK(img->channels[0](1, 0) == doctest::Approx(1.f).epsilon(0.001));
        CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));
    }
}

TEST_CASE("A saved TIFF multiplies alpha into its encoded samples")
{
    // HDRView holds premultiplied linear color, so a straight 1.0 under half coverage is linear 0.5 and the
    // file has to hold alpha * OETF(1.0) = alpha, not OETF(0.5) = 0.735
    auto img = test_image(int2{1, 1}, 4, [](int c, int, int) { return 0.5f; }); // premultiplied linear

    std::ostringstream out(std::ios::binary);
    save_tiff_image(*img, out, "assoc.tif", /*gain*/ 1.f, TransferFunction::sRGB, /*compression*/ 0,
                    /*data_type*/ 0);

    auto samples = tiff_raw_samples(out.str());
    REQUIRE(samples.size() >= 4);

    const unsigned char alpha = samples[3];
    CHECK(int(alpha) == doctest::Approx(127).epsilon(0.02)); // 0.5 quantized over 8 bits
    // a*OETF(C) == 0.5 * 1.0
    CHECK(int(samples[0]) == doctest::Approx(127).epsilon(0.02));
    // not OETF(a*C) == 0.735, which leaving the premultiply in place across the transfer would give
    CHECK(int(samples[0]) != doctest::Approx(188).epsilon(0.02));

    // a stored color can never exceed its alpha, being that alpha times an encoded value of at most one
    for (int c = 0; c < 3; ++c) CHECK(samples[c] <= alpha);

    // and it reads back as what it started as
    auto reloaded = load_tiff(out.str());
    CHECK(reloaded->transparency == TransparencyType_PremultipliedNonLinear);
    CHECK(reloaded->channels[0](0, 0) == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("TIFF save/load round-trips alpha without repeated premultiplication")
{
    auto original = load_tiff(rgba_tiff(2));

    // draw_save_as_dialog() renders into an ostringstream and writes the buffer out, so that is the only
    // save path there is. Float samples keep the comparison about alpha, not quantization.
    std::ostringstream out(std::ios::binary);
    save_tiff_image(*original, out, "roundtrip.tif", /*gain*/ 1.f, TransferFunction::Linear,
                    /*compression*/ 0, /*data_type*/ 2);
    REQUIRE(out.str().size() > 64);

    auto reloaded = load_tiff(out.str());

    REQUIRE(reloaded->channels.size() == 4);
    for (int c = 0; c < 4; ++c)
        for (int x = 0; x < 2; ++x)
            CHECK(reloaded->channels[c](x, 0) == doctest::Approx(original->channels[c](x, 0)).epsilon(0.001));
}

namespace
{

constexpr uint16_t k_sampleformat_uint   = 1;
constexpr uint16_t k_sampleformat_int    = 2;
constexpr uint16_t k_sampleformat_ieeefp = 3;

/// A minimal single-strip grayscale TIFF: one IFD, uncompressed, one sample per pixel.
/**
    `samples` holds raw sample words in the file's own bit width, written in `endian`'s byte order; `bits`
    must be a whole number of bytes. Generated so the byte-order axis is reachable: a byte blob is one
    endianness only, and little-endian is the one where a host-order bug hides.
*/
std::string gray_tiff(Endian endian, uint16_t bits, uint16_t sample_format, const std::vector<uint64_t> &words)
{
    REQUIRE(bits % 8 == 0);
    const uint32_t bytes_per_word = bits / 8u;

    std::vector<uint8_t> strip;
    for (uint64_t w : words)
        for (uint32_t b = 0; b < bytes_per_word; ++b)
        {
            const uint32_t shift = endian == Endian::Little ? b : (bytes_per_word - 1 - b);
            strip.push_back(uint8_t((w >> (8 * shift)) & 0xff));
        }

    return tiff_bytes(endian,
                      {
                          {256, 3, 1, uint32_t(words.size()), {}}, // ImageWidth
                          {257, 3, 1, 1, {}},                      // ImageLength
                          {258, 3, 1, bits, {}},                   // BitsPerSample
                          {259, 3, 1, 1, {}},                      // Compression: none
                          {262, 3, 1, 1, {}},                      // Photometric: BlackIsZero
                          {277, 3, 1, 1, {}},                      // SamplesPerPixel
                          {278, 3, 1, 1, {}},                      // RowsPerStrip
                          {339, 3, 1, sample_format, {}},          // SampleFormat
                      },
                      strip);
}

/// Every image a TIFF's directories yield, so a refusal shows as an empty result.
std::vector<ImagePtr> load_tiff_bytes(const std::string &bytes, const std::string &name)
{
    std::istringstream is(bytes, std::ios::binary);
    return load_tiff_image(is, name);
}

const char *endian_name(Endian e) { return e == Endian::Little ? "little-endian" : "big-endian"; }

} // namespace

TEST_CASE("TIFF integer samples decode into the unit range at every supported width, sign and byte order")
{
    // Sample width, signedness and byte order are three independent axes through unpack_bits(), and a bug in
    // any of them (a sign bit read as a magnitude, a partial word assembled in host order) shows up the same
    // way: a value outside [0,1] or out of order. GDAL's int16/int24/int32 and uint* samples are such files.
    for (Endian endian : {Endian::Little, Endian::Big})
        for (uint16_t bits : {(uint16_t)8, (uint16_t)16, (uint16_t)24, (uint16_t)32})
            for (uint16_t format : {k_sampleformat_uint, k_sampleformat_int})
            {
                const char *sign_name = format == k_sampleformat_int ? "signed" : "unsigned";
                CAPTURE(endian_name(endian));
                CAPTURE(bits);
                CAPTURE(sign_name);

                // four well-separated samples in increasing numeric order, so the check is on ordering and
                // endpoints and not on whatever curve the loader applies between them
                const uint64_t        one   = 1ull;
                const uint64_t        range = bits >= 64 ? ~0ull : ((one << bits) - 1);
                std::vector<uint64_t> samples;
                if (format == k_sampleformat_uint)
                    samples = {0, range / 4, range / 2, range};
                else
                {
                    const uint64_t min = one << (bits - 1); // two's complement minimum
                    samples            = {min, min + min / 2, 0, (one << (bits - 1)) - 1};
                }

                auto images = load_tiff_bytes(gray_tiff(endian, bits, format, samples), "matrix.tif");
                REQUIRE(images.size() == 1);
                auto &ch = images[0]->channels[0];
                REQUIRE(ch.num_elements() == 4);

                for (int i = 0; i < 4; ++i)
                {
                    CAPTURE(i);
                    CHECK(ch(i) >= 0.f);
                    CHECK(ch(i) <= 1.f);
                }

                CHECK(ch(0) == doctest::Approx(0.f).epsilon(1e-5)); // the minimum
                CHECK(ch(3) == doctest::Approx(1.f).epsilon(1e-5)); // the maximum
                CHECK(ch(0) < ch(1));
                CHECK(ch(1) < ch(2));
                CHECK(ch(2) < ch(3));
            }
}

TEST_CASE("TIFF samples of a width the loader cannot represent are refused")
{
    // The refusal surfaces as no image, not an exception: load_image() catches per directory and skips it,
    // so one bad directory of a multi-directory TIFF doesn't discard the rest. unpack_bits accumulates into a
    // uint32_t and convert_to_float reads half, float or double, so wider samples have nowhere to land.
    // GDAL's int64.tif and float24.tif are such files.
    for (Endian endian : {Endian::Little, Endian::Big})
    {
        CAPTURE(endian_name(endian));

        SUBCASE("integers wider than the accumulator")
        {
            for (uint16_t format : {k_sampleformat_uint, k_sampleformat_int})
            {
                CAPTURE(format);
                CHECK(load_tiff_bytes(gray_tiff(endian, 64, format, {0, 1, 2, 3}), "int64.tif").empty());
            }
        }

        SUBCASE("floating point at a width with no such format")
        {
            for (uint16_t bits : {(uint16_t)8, (uint16_t)24})
            {
                CAPTURE(bits);
                CHECK(load_tiff_bytes(gray_tiff(endian, bits, k_sampleformat_ieeefp, {0, 0, 0, 0}), "floatN.tif")
                          .empty());
            }
        }
    }
}

TEST_CASE("TIFF 32- and 64-bit floating point samples load")
{
    // the widths convert_to_float does handle, so the refusal above is about the width and not about
    // SAMPLEFORMAT_IEEEFP itself
    for (Endian endian : {Endian::Little, Endian::Big})
    {
        CAPTURE(endian_name(endian));

        std::vector<uint64_t> f32;
        for (float v : {0.f, 0.25f, 0.5f, 1.f})
        {
            uint32_t bits32;
            std::memcpy(&bits32, &v, sizeof(bits32));
            f32.push_back(bits32);
        }
        auto images = load_tiff_bytes(gray_tiff(endian, 32, k_sampleformat_ieeefp, f32), "float32.tif");
        REQUIRE(images.size() == 1);
        auto &ch = images[0]->channels[0];
        REQUIRE(ch.num_elements() == 4);
        CHECK(ch(0) < ch(1));
        CHECK(ch(1) < ch(2));
        CHECK(ch(2) < ch(3));

        std::vector<uint64_t> f64;
        for (double v : {0.0, 0.25, 0.5, 1.0})
        {
            uint64_t bits64;
            std::memcpy(&bits64, &v, sizeof(bits64));
            f64.push_back(bits64);
        }
        auto images64 = load_tiff_bytes(gray_tiff(endian, 64, k_sampleformat_ieeefp, f64), "float64.tif");
        REQUIRE(images64.size() == 1);
        auto &ch64 = images64[0]->channels[0];
        REQUIRE(ch64.num_elements() == 4);
        CHECK(ch64(0) < ch64(1));
        CHECK(ch64(1) < ch64(2));
        CHECK(ch64(2) < ch64(3));
    }
}

#if HDRVIEW_ENABLE_LIBTIFF

namespace
{

// A 4x4 8-bit RGB baseline TIFF carrying a TransferFunction tag (301). TIFF sizes that curve at
// 2^BitsPerSample entries, 256 here, so a reader indexing it over the full 16-bit range runs off the end.
// The curve written is the identity, so a correct decode recovers the stored samples.
std::string tiff_with_transfer_function(std::array<uint8_t, 3> pixel_1_0)
{
    std::vector<uint8_t> strip;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 3; ++c) strip.push_back((x == 1 && y == 0) ? pixel_1_0[c] : uint8_t(0));

    std::vector<uint8_t> bits_per_sample;
    for (int i = 0; i < 3; ++i) put<uint16_t>(bits_per_sample, 8);

    std::vector<uint8_t> curve;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) put<uint16_t>(curve, uint16_t(i * 257)); // 8-bit index to 16-bit value

    return tiff_bytes(Endian::Little,
                      {
                          {256, 3, 1, 4, {}},              // ImageWidth
                          {257, 3, 1, 4, {}},              // ImageLength
                          {258, 3, 3, 0, bits_per_sample}, // BitsPerSample
                          {259, 3, 1, 1, {}},              // Compression: none
                          {262, 3, 1, 2, {}},              // Photometric: RGB
                          {277, 3, 1, 3, {}},              // SamplesPerPixel
                          {278, 3, 1, 4, {}},              // RowsPerStrip
                          {301, 3, 768, 0, curve},         // TransferFunction
                      },
                      strip);
}

} // namespace

TEST_CASE("An 8-bit TIFF's TransferFunction curve is indexed over its own length, not the 16-bit range")
{
    const std::array<uint8_t, 3> stored{24, 72, 136};

    auto img = load_bytes(load_tiff_image, tiff_with_transfer_function(stored), "tf.tif");
    REQUIRE(img);
    REQUIRE(img->channels.size() == 3);

    // the identity curve has to give the stored samples back
    for (int c = 0; c < 3; ++c) CHECK(img->channels[c](1, 0) == doctest::Approx(stored[c] / 255.f).epsilon(1e-4));
}
TEST_CASE("A wide-gamut image saved as TIFF records the primaries its samples are in")
{
    // save_tiff_image() asks as_interleaved() not to convert to sRGB, so the samples it writes are in the
    // image's own primaries and the file has to record them in WhitePoint and PrimaryChromaticities.
    const Chromaticities bt2020{{0.708f, 0.292f}, {0.170f, 0.797f}, {0.131f, 0.046f}, {0.3127f, 0.3290f}};

    auto original            = load_tiff(rgba_tiff(2));
    original->chromaticities = bt2020;
    color_conversion_matrix(original->M_to_sRGB, bt2020, gamut_chromaticities(ColorGamut_sRGB_BT709));
    const float4 want = original->rgba_pixel(int2{0, 0}, Target_Primary);

    // 32-bit float, so the transfer function plays no part in what comes back
    std::ostringstream out(std::ios::binary);
    REQUIRE_NOTHROW(save_tiff_image(*original, out, "wide.tif", /*gain*/ 1.f, TransferFunction::Linear,
                                    /*compression*/ 0, /*data_type*/ 2));

    auto reloaded = load_tiff(out.str());
    REQUIRE(reloaded->chromaticities.has_value());
    CHECK(reloaded->chromaticities->red.x == doctest::Approx(bt2020.red.x).epsilon(1e-4));
    CHECK(reloaded->chromaticities->green.y == doctest::Approx(bt2020.green.y).epsilon(1e-4));
    CHECK(reloaded->chromaticities->white.x == doctest::Approx(bt2020.white.x).epsilon(1e-4));

    const float4 got = reloaded->rgba_pixel(int2{0, 0}, Target_Primary);
    for (int c = 0; c < 3; ++c) CHECK(got[c] == doctest::Approx(want[c]).epsilon(1e-3));
}

#ifdef HDRVIEW_TEST_LIBTIFF_DIR

// libtiff's own test images: OJPEG, fax, palettes at three depths, LogLuv, float64 behind a predictor, and
// four files with several directories, two of which loop. A name of the form photometric-channels-bits is
// the only oracle they carry; the README's "mostly 157x151" is not one, since two of them are neither.
TEST_CASE("libtiff's test images decode as their names say, or are refused")
{
    namespace fs = std::filesystem;

    int named = 0, read = 0, refused = 0;
    for (const auto &entry : fs::directory_iterator(HDRVIEW_TEST_LIBTIFF_DIR))
    {
        const auto path = entry.path();
        if (path.extension() != ".tif" && path.extension() != ".tiff")
            continue;

        const std::string name = path.filename().string();
        CAPTURE(name);

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

        // the loader is called directly rather than through load_bytes(), which keeps only a lone image: the
        // files with several directories are the ones worth reaching here, and they decode to one apiece
        std::istringstream    in_stream(bytes, std::ios::binary);
        std::vector<ImagePtr> images;
        try
        {
            images = load_tiff_image(in_stream, name);
        }
        catch (const std::exception &)
        {
            // refusing a file libtiff keeps because it is malformed is an answer rather than a failure; the
            // count below is what says every file was reached
            ++refused;
            continue;
        }
        REQUIRE_FALSE(images.empty());

        ++read;
        for (const auto &part : images)
        {
            part->finalize();
            CHECK(part->size().x > 0);
            CHECK(part->size().y > 0);
        }
        const auto &img = images.front();

        // photometric-channels-bits, e.g. rgb-3c-16b.tiff. A palette is one channel of indices in the file
        // and three of color once expanded, so the count is what the pixels are, not what the file stores.
        int channels = 0, bits = 0;
        if (sscanf(name.c_str(), "%*[a-z]-%dc-%db", &channels, &bits) == 2)
        {
            ++named;
            CHECK((int)img->channels.size() == (name.rfind("palette", 0) == 0 ? 3 : channels));
            // LogLuv decodes to float, and a float channel reports a depth of zero rather than the file's
            if (name.rfind("logluv", 0) != 0)
                CHECK(img->channels[0].bits_per_sample == bits);
        }
    }

    CAPTURE(read);
    CAPTURE(refused);
    CHECK(named >= 8); // the README lists eight of them
    CHECK(read >= 31); // every .tif and .tiff in the directory, the multi-directory ones included
    CHECK(read > named);
}

#endif // HDRVIEW_TEST_LIBTIFF_DIR
#endif
