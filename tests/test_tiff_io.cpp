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
#include <half.h>
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
    // A separated image's fourth sample is black ink and never coverage, with no EXTRASAMPLES needed to say so.
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
        // pixel 1 holds 128/255 under alpha 128/255: a fully bright color at half coverage, multiplied into the
        // encoded samples (a*OETF(C)), as Photoshop stores it, OpenImageIO 3.2 writes by default and vips 8.18 asked
        // to premultiply; ImageMagick 7.1.2's associated alpha multiplies the color the same way, but its alpha too
        auto img = load_tiff(rgba_tiff(1, 0x80));

        REQUIRE(img->channels.size() == 4);
        CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));

        // dividing alpha out before the inverse transfer recovers straight 1.0, and multiplying back gives
        // linear 1.0 times alpha; the transfer applied to the stored value would give EOTF(0.502) = 0.216
        CHECK(img->channels[0](1, 0) == doctest::Approx(k_half).epsilon(0.005));
        CHECK(img->channels[0](1, 0) != doctest::Approx(0.216f).epsilon(0.01));

        // the opaque pixel is the control, unaffected by any of this
        CHECK(img->channels[0](0, 0) == doctest::Approx(1.f).epsilon(0.001));
    }

    SUBCASE("an override changes which of the two the samples go through")
    {
        // Read a file the tag says is associated as straight, so finalize() multiplies instead of the loader
        // dividing, for a file whose writer tagged it wrongly.
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
    save_tiff_image(*img, out, "assoc.tif", TransferFunction::sRGB, /*compression*/ 0,
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

/// Every image a TIFF's directories yield, its samples left as stored, so a refusal shows as an empty result.
std::vector<ImagePtr> load_tiff_bytes(const std::string &bytes, const std::string &name)
{
    ImageLoadOptions opts;
    opts.override_profile = true;
    opts.tf_override      = TransferFunction::Linear;
    std::istringstream is(bytes, std::ios::binary);
    return load_tiff_image(is, name, opts);
}

const char *endian_name(Endian e) { return e == Endian::Little ? "little-endian" : "big-endian"; }

} // namespace

TEST_CASE("TIFF samples decode to their values at every width, sign and byte order, or are refused")
{
    // Width, sign and byte order are independent axes through unpack_bits() and convert_to_float(), and a bug in
    // any of them (a sign bit read as magnitude, a partial word assembled in host order) moves a value. Integers
    // dequantize to [0,1], signed ones offset by half the range; floats arrive as stored, sign and range intact.
    // unpack_bits() accumulates into a uint32_t and convert_to_float() reads half, float or double, so samples of
    // other widths have nowhere to land and are refused: as no image, not an exception, since the loader skips a
    // bad directory and keeps the rest. GDAL's int16/int24/int32, int64.tif and float24.tif are such files.
    const float floats[] = {-0.5f, 0.25f, 2.f, 1000.f}; // exact at half precision too
    for (Endian endian : {Endian::Little, Endian::Big})
        for (uint16_t format : {k_sampleformat_uint, k_sampleformat_int, k_sampleformat_ieeefp})
            for (uint16_t bits : {(uint16_t)8, (uint16_t)16, (uint16_t)24, (uint16_t)32, (uint16_t)64})
            {
                CAPTURE(endian_name(endian));
                CAPTURE(format);
                CAPTURE(bits);

                std::vector<uint64_t> words;
                std::vector<double>   want;
                const uint64_t        one = 1;
                if (format == k_sampleformat_ieeefp)
                    for (float v : floats)
                    {
                        if (bits == 16)
                            words.push_back(half(v).bits());
                        else if (bits == 64)
                        {
                            const double d = v;
                            uint64_t     w;
                            std::memcpy(&w, &d, sizeof(w));
                            words.push_back(w);
                        }
                        else
                        {
                            uint32_t w;
                            std::memcpy(&w, &v, sizeof(w));
                            words.push_back(w);
                        }
                        want.push_back(v);
                    }
                else
                {
                    // the extremes and two values between, as words of the file's width
                    const uint64_t range = bits >= 64 ? ~0ull : (one << bits) - 1;
                    const double   denom = double(range);
                    if (format == k_sampleformat_uint)
                    {
                        words = {0, range / 4, range / 2, range};
                        for (uint64_t w : words) want.push_back(double(w) / denom);
                    }
                    else
                    {
                        // two's complement: the minimum, half of it, zero and the maximum
                        const uint64_t min = one << (bits - 1);
                        words              = {min, min + min / 2, 0, min - 1};
                        const double bias  = double(min);
                        for (double v : {-bias, -bias / 2, 0., bias - 1}) want.push_back((v + bias) / denom);
                    }
                }

                const bool supported =
                    format == k_sampleformat_ieeefp ? (bits == 16 || bits == 32 || bits == 64) : bits <= 32;
                auto images = load_tiff_bytes(gray_tiff(endian, bits, format, words), "matrix.tif");
                if (!supported)
                {
                    CHECK(images.empty());
                    continue;
                }
                REQUIRE(images.size() == 1);
                auto &ch = images[0]->channels[0];
                REQUIRE(ch.num_elements() == 4);
                // the histogram's bin count comes from this: the file's depth, or none for floating point
                CHECK(ch.bits_per_sample == (format == k_sampleformat_ieeefp ? 0 : bits));
                for (int i = 0; i < 4; ++i)
                {
                    CAPTURE(i);
                    CHECK(ch(i) == doctest::Approx(want[i]).epsilon(1e-6));
                }
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
            ++refused;
            continue;
        }

        // refusing a file is an answer rather than a failure, whether it comes as a throw or as the empty
        // vector load_image() documents: libtiff is not built with every codec everywhere, and the WebP one
        // is unreadable wherever it lacks that support. The counts below are what say every file was reached.
        if (images.empty())
        {
            ++refused;
            continue;
        }

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
    CHECK(named >= 8);           // the README lists eight of them
    CHECK(read + refused >= 31); // every .tif and .tiff in the directory, the multi-directory ones included
    CHECK(refused <= 2);         // and a sweep must not refuse its way to a pass
    CHECK(read > named);
}

#endif // HDRVIEW_TEST_LIBTIFF_DIR
#endif
