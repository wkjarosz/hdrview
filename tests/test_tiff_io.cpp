//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"
#include "image.h"
#include "imageio/tiff.h"

#include <initializer_list>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// A 2x1 8-bit RGBA uncompressed TIFF tagged EXTRASAMPLE_UNASSALPHA (straight alpha).
// Pixel 0 is opaque white; pixel 1 is white at alpha 128/255.
const unsigned char k_rgba_tiff[] = {
    0x49, 0x49, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01,
    0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00, 0x00, 0x03, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x11, 0x01,
    0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x9a, 0x00, 0x00, 0x00, 0x15, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x16, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x17, 0x01,
    0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1c, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x52, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80};

constexpr uint16_t k_extra_samples_tag = 338;
constexpr float    k_half              = 128.f / 255.f;

// Returns k_rgba_tiff with its EXTRASAMPLES tag rewritten, so the fixtures under test differ in
// nothing but how the file describes its fourth sample. Walks the little-endian IFD rather than
// hardcoding a byte offset.
std::string tiff_with_extra_samples(uint16_t value)
{
    std::string bytes(reinterpret_cast<const char *>(k_rgba_tiff), sizeof(k_rgba_tiff));

    auto read_u16 = [&](size_t o) { return uint16_t(uint8_t(bytes[o]) | (uint8_t(bytes[o + 1]) << 8)); };
    auto read_u32 = [&](size_t o) { return uint32_t(read_u16(o)) | (uint32_t(read_u16(o + 2)) << 16); };

    REQUIRE(bytes.compare(0, 2, "II") == 0); // the fixture is little-endian
    const uint32_t ifd     = read_u32(4);
    const uint16_t entries = read_u16(ifd);
    for (uint16_t i = 0; i < entries; ++i)
    {
        const size_t entry = ifd + 2 + size_t(i) * 12;
        if (read_u16(entry) != k_extra_samples_tag)
            continue;
        // SHORT, count 1: the value lives inline in the first half of the entry's value field
        bytes[entry + 8] = char(value & 0xff);
        bytes[entry + 9] = char(value >> 8);
        return bytes;
    }
    FAIL("fixture has no EXTRASAMPLES tag");
    return bytes;
}

ImagePtr load_tiff(const std::string &bytes)
{
    std::istringstream in(bytes, std::ios::binary);
    auto               images = load_tiff_image(in, "rgba.tif");
    REQUIRE(images.size() == 1);
    images[0]->finalize();
    return images[0];
}

} // namespace

TEST_CASE("TIFF channels report the file's sample depth")
{
    auto img = load_tiff(tiff_with_extra_samples(2)); // EXTRASAMPLE_UNASSALPHA
    for (const auto &c : img->channels) CHECK(c.bits_per_sample == 8);
}

TEST_CASE("TIFF with unassociated alpha is premultiplied exactly once")
{
    // EXTRASAMPLE_UNASSALPHA == 2
    auto img = load_tiff(tiff_with_extra_samples(2));

    REQUIRE(img->channels.size() == 4);
    CHECK(img->alpha_type == AlphaType_Straight);
    CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));

    // Premultiplying twice would leave k_half*k_half here instead.
    CHECK(img->channels[0](0, 0) == doctest::Approx(1.f).epsilon(0.001));
    CHECK(img->channels[0](1, 0) == doctest::Approx(k_half).epsilon(0.001));
}

TEST_CASE("TIFF tagged EXTRASAMPLE_UNSPECIFIED keeps its fourth sample as data")
{
    // EXTRASAMPLE_UNSPECIFIED == 0: the file states the extra sample is not alpha, so nothing is
    // premultiplied and the color channels keep their stored values.
    auto img = load_tiff(tiff_with_extra_samples(0));

    REQUIRE(img->channels.size() == 4);
    CHECK(img->alpha_type == AlphaType_None);
    CHECK_FALSE(img->alpha_is_transparency);
    CHECK(img->channels[0](1, 0) == doctest::Approx(1.f).epsilon(0.001));
    CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));

    // The extra sample stands on its own rather than joining an RGBA group that would composite it.
    REQUIRE(img->groups.size() == 2);
    CHECK(img->groups[0].type == ChannelGroup::RGB_Channels);
    CHECK(img->groups[1].type == ChannelGroup::Single_Channel);
}

TEST_CASE("TIFF save/load round-trips alpha without repeated premultiplication")
{
    auto original = load_tiff(tiff_with_extra_samples(2));

    // Saving to a std::ostringstream is the only path HDRView has -- draw_save_as_dialog() renders
    // into one and then writes the buffer out. Float samples keep the comparison about alpha rather
    // than about transfer-function encoding or 8-bit quantization.
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

//! Builds a minimal single-strip grayscale TIFF: one IFD, uncompressed, one sample per pixel.
/*!
    `samples` holds raw sample words in the file's own bit width, written in `endian`'s byte order;
    `bits` must be a whole number of bytes. Generating these rather than embedding byte blobs is what
    makes the byte-order axis reachable at all -- a hand-built fixture is one endianness only, and
    little-endian is the one where a host-order bug hides.
*/
std::string make_gray_tiff(Endian endian, uint16_t bits, uint16_t sample_format, const std::vector<uint64_t> &samples)
{
    REQUIRE(bits % 8 == 0);
    const uint32_t bytes_per_sample = bits / 8u;
    const uint32_t width            = (uint32_t)samples.size();

    std::string out;
    auto        put_u16 = [&](uint16_t v)
    {
        if (endian == Endian::Little)
            out += {(char)(v & 0xff), (char)((v >> 8) & 0xff)};
        else
            out += {(char)((v >> 8) & 0xff), (char)(v & 0xff)};
    };
    auto put_u32 = [&](uint32_t v)
    {
        if (endian == Endian::Little)
            out += {(char)(v & 0xff), (char)((v >> 8) & 0xff), (char)((v >> 16) & 0xff), (char)((v >> 24) & 0xff)};
        else
            out += {(char)((v >> 24) & 0xff), (char)((v >> 16) & 0xff), (char)((v >> 8) & 0xff), (char)(v & 0xff)};
    };

    // 8-byte header + 2-byte entry count + ten 12-byte entries + 4-byte next-IFD pointer.
    constexpr uint32_t k_num_entries  = 10;
    constexpr uint32_t k_strip_offset = 8 + 2 + k_num_entries * 12 + 4;

    // A SHORT value occupies the first two bytes of its four-byte field in both byte orders; a LONG
    // fills it.
    auto short_entry = [&](uint16_t tag, uint16_t value)
    {
        put_u16(tag);
        put_u16(3); // SHORT
        put_u32(1);
        put_u16(value);
        put_u16(0);
    };
    auto long_entry = [&](uint16_t tag, uint32_t value)
    {
        put_u16(tag);
        put_u16(4); // LONG
        put_u32(1);
        put_u32(value);
    };

    out += endian == Endian::Little ? "II" : "MM";
    put_u16(42);
    put_u32(8);

    put_u16(k_num_entries);                       // entries must be ordered by ascending tag
    short_entry(0x0100, (uint16_t)width);         // ImageWidth
    short_entry(0x0101, 1);                       // ImageLength
    short_entry(0x0102, bits);                    // BitsPerSample
    short_entry(0x0103, 1);                       // Compression: none
    short_entry(0x0106, 1);                       // PhotometricInterpretation: BlackIsZero
    long_entry(0x0111, k_strip_offset);           // StripOffsets
    short_entry(0x0115, 1);                       // SamplesPerPixel
    short_entry(0x0116, 1);                       // RowsPerStrip
    long_entry(0x0117, width * bytes_per_sample); // StripByteCounts
    short_entry(0x0153, sample_format);           // SampleFormat
    put_u32(0);                                   // no next IFD

    REQUIRE(out.size() == k_strip_offset);

    for (uint64_t s : samples)
        for (uint32_t b = 0; b < bytes_per_sample; ++b)
        {
            const uint32_t shift = endian == Endian::Little ? b : (bytes_per_sample - 1 - b);
            out += (char)((s >> (8 * shift)) & 0xff);
        }

    return out;
}

std::vector<ImagePtr> load_tiff_bytes(const std::string &bytes, const std::string &name)
{
    std::istringstream is(bytes, std::ios::binary);
    return load_tiff_image(is, name);
}

const char *endian_name(Endian e) { return e == Endian::Little ? "little-endian" : "big-endian"; }

} // namespace

TEST_CASE("TIFF integer samples decode into the unit range at every supported width, sign and byte order")
{
    // One matrix rather than a fixture per case: the sample width, the signedness and the file's byte
    // order are three independent axes through unpack_bits(), and a bug in any one of them (a sign bit
    // read as a magnitude, a partial word assembled in host order) shows up as the same symptom -- a
    // value outside [0,1] or out of order. GDAL's int16/int24/int32 and uint* samples are such files.
    for (Endian endian : {Endian::Little, Endian::Big})
        for (uint16_t bits : {(uint16_t)8, (uint16_t)16, (uint16_t)24, (uint16_t)32})
            for (uint16_t format : {k_sampleformat_uint, k_sampleformat_int})
            {
                const char *sign_name = format == k_sampleformat_int ? "signed" : "unsigned";
                CAPTURE(endian_name(endian));
                CAPTURE(bits);
                CAPTURE(sign_name);

                // Four well-separated samples in increasing true numeric order, so the check is on
                // ordering and endpoints rather than on whatever curve the loader applies between them.
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

                auto images = load_tiff_bytes(make_gray_tiff(endian, bits, format, samples), "matrix.tif");
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
    // The refusal surfaces as no image rather than as an exception: load_image() catches per directory
    // and skips it, so one bad directory of a multi-directory TIFF doesn't discard the rest.
    //
    // unpack_bits accumulates into a uint32_t, so an integer sample wider than that has nowhere to land;
    // convert_to_float reads a float sample as half, float or double and has no meaning to give any other
    // width. GDAL's int64.tif and float24.tif are such files.
    for (Endian endian : {Endian::Little, Endian::Big})
    {
        CAPTURE(endian_name(endian));

        SUBCASE("integers wider than the accumulator")
        {
            for (uint16_t format : {k_sampleformat_uint, k_sampleformat_int})
            {
                CAPTURE(format);
                CHECK(load_tiff_bytes(make_gray_tiff(endian, 64, format, {0, 1, 2, 3}), "int64.tif").empty());
            }
        }

        SUBCASE("floating point at a width with no such format")
        {
            for (uint16_t bits : {(uint16_t)8, (uint16_t)24})
            {
                CAPTURE(bits);
                CHECK(load_tiff_bytes(make_gray_tiff(endian, bits, k_sampleformat_ieeefp, {0, 0, 0, 0}), "floatN.tif")
                          .empty());
            }
        }
    }
}

TEST_CASE("TIFF 32- and 64-bit floating point samples load")
{
    // The widths convert_to_float does have a meaning for, so the refusal above is about the width and
    // not about SAMPLEFORMAT_IEEEFP itself.
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
        auto images = load_tiff_bytes(make_gray_tiff(endian, 32, k_sampleformat_ieeefp, f32), "float32.tif");
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
        auto images64 = load_tiff_bytes(make_gray_tiff(endian, 64, k_sampleformat_ieeefp, f64), "float64.tif");
        REQUIRE(images64.size() == 1);
        auto &ch64 = images64[0]->channels[0];
        REQUIRE(ch64.num_elements() == 4);
        CHECK(ch64(0) < ch64(1));
        CHECK(ch64(1) < ch64(2));
        CHECK(ch64(2) < ch64(3));
    }
}
