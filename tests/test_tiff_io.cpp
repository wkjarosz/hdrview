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

#include <cstring>
#include <initializer_list>
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

// k_rgba_tiff with its EXTRASAMPLES tag rewritten, so the fixtures differ in nothing but how the file
// describes its fourth sample. Walks the little-endian IFD instead of hardcoding a byte offset.
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

// The same fixture with pixel 1's color samples replaced. Associated alpha stores a*OETF(C), which can never
// exceed alpha, so the unmodified fixture's opaque white under half coverage is straight-alpha data.
std::string tiff_with_second_pixel_color(uint16_t extra_samples, unsigned char value)
{
    std::string bytes = tiff_with_extra_samples(extra_samples);
    // pixel 1's RGB, immediately before its alpha at the end of the single strip
    bytes[bytes.size() - 4] = char(value);
    bytes[bytes.size() - 3] = char(value);
    bytes[bytes.size() - 2] = char(value);
    return bytes;
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

// The fixture with nothing left saying what its fourth sample is, as most RGBA TIFFs in the wild look. The
// entry is renumbered to a private tag, not erased: removing it would shift every out-of-line value without
// shifting the offsets pointing at them. EXTRASAMPLES is the highest tag here, so the IFD stays sorted.
std::string tiff_without_extra_samples()
{
    std::string bytes = tiff_with_extra_samples(2);

    auto read_u16 = [&](size_t o) { return uint16_t(uint8_t(bytes[o]) | (uint8_t(bytes[o + 1]) << 8)); };
    auto read_u32 = [&](size_t o) { return uint32_t(read_u16(o)) | (uint32_t(read_u16(o + 2)) << 16); };

    const uint32_t ifd     = read_u32(4);
    const uint16_t entries = read_u16(ifd);
    for (uint16_t i = 0; i < entries; ++i)
    {
        const size_t entry = ifd + 2 + size_t(i) * 12;
        if (read_u16(entry) != k_extra_samples_tag)
            continue;

        constexpr uint16_t k_private_tag = 65000;
        bytes[entry]                     = char(k_private_tag & 0xff);
        bytes[entry + 1]                 = char(k_private_tag >> 8);
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

    // premultiplying twice would leave k_half*k_half here
    CHECK(img->channels[0](0, 0) == doctest::Approx(1.f).epsilon(0.001));
    CHECK(img->channels[0](1, 0) == doctest::Approx(k_half).epsilon(0.001));
}

TEST_CASE("TIFF tagged EXTRASAMPLE_UNSPECIFIED keeps its fourth sample as data")
{
    // EXTRASAMPLE_UNSPECIFIED == 0: the file states the extra sample is not alpha
    auto img = load_tiff(tiff_with_extra_samples(0));

    REQUIRE(img->channels.size() == 4);
    CHECK(img->alpha_type == AlphaType_None);
    CHECK_FALSE(img->alpha_is_transparency());
    CHECK(img->channels[0](1, 0) == doctest::Approx(1.f).epsilon(0.001));
    CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));

    // the extra sample stands on its own, outside any RGBA group that would composite it
    REQUIRE(img->groups.size() == 2);
    CHECK(img->groups[0].type == ChannelGroup::RGB_Channels);
    CHECK(img->groups[1].type == ChannelGroup::Single_Channel);
}

TEST_CASE("TIFF associated alpha is read as multiplied into the encoded samples")
{
    // EXTRASAMPLE_ASSOCALPHA == 1, with pixel 1 holding 128/255 under alpha 128/255: a fully bright color at
    // half coverage, as Photoshop, OpenImageIO, ImageMagick and vips all store it (a*OETF(C))
    auto img = load_tiff(tiff_with_second_pixel_color(1, 0x80));

    REQUIRE(img->channels.size() == 4);
    CHECK(img->alpha_type == AlphaType_PremultipliedNonLinear);
    CHECK(img->channels[3](1, 0) == doctest::Approx(k_half).epsilon(0.001));

    // dividing alpha out before the inverse transfer recovers straight 1.0, and multiplying back gives
    // linear 1.0 times alpha; the transfer applied to the stored value would give EOTF(0.502) = 0.214
    CHECK(img->channels[0](1, 0) == doctest::Approx(k_half).epsilon(0.005));
    CHECK(img->channels[0](1, 0) != doctest::Approx(0.214f).epsilon(0.01));

    // the opaque pixel is the control, unaffected by any of this
    CHECK(img->channels[0](0, 0) == doctest::Approx(1.f).epsilon(0.001));
}

TEST_CASE("An alpha override replaces what the TIFF declared")
{
    // Read a file the tag says is associated as straight, so finalize() multiplies instead of the loader
    // dividing. ImageMagick and vips both write premultiplied samples tagged unassociated.
    ImageLoadOptions opts;
    opts.override_alpha = true;
    opts.alpha_override = AlphaType_Straight;

    std::istringstream in(tiff_with_second_pixel_color(1, 0x80), std::ios::binary);
    auto               images = load_image(in, "rgba.tif", opts);
    REQUIRE(images.size() == 1);

    CHECK(images[0]->alpha_type == AlphaType_Straight);
    // straight 128/255 linearizes to 0.216, which finalize() then multiplies by alpha
    CHECK(images[0]->channels[0](1, 0) == doctest::Approx(0.216f * k_half).epsilon(0.02));
}

TEST_CASE("Overriding to AlphaType_None loads a declared alpha channel as data")
{
    // the file declares real alpha and the user says it is a mask, so nothing is multiplied by it
    ImageLoadOptions opts;
    opts.override_alpha = true;
    opts.alpha_override = AlphaType_None;

    std::istringstream in(tiff_with_extra_samples(2), std::ios::binary);
    auto               images = load_image(in, "rgba.tif", opts);
    REQUIRE(images.size() == 1);

    CHECK(images[0]->alpha_type == AlphaType_None);
    CHECK_FALSE(images[0]->alpha_is_transparency());
    CHECK(images[0]->channels[0](1, 0) == doctest::Approx(1.f).epsilon(0.001));
    REQUIRE(images[0]->groups.size() == 2);
    CHECK(images[0]->groups[1].type == ChannelGroup::Single_Channel);
}

TEST_CASE("A TIFF says where its alpha kind came from")
{
    // EXTRASAMPLES settles it, whichever value it carries, including the one saying the sample is data
    for (uint16_t tag : {uint16_t(0), uint16_t(1), uint16_t(2)})
    {
        CAPTURE(tag);
        auto img = load_tiff(tiff_with_extra_samples(tag));
        CHECK(img->alpha_source == AlphaSource_File);
    }

    // with the tag gone, nothing in the file states a kind and straight is the loader's guess
    auto guessed = load_tiff(tiff_without_extra_samples());
    CHECK(guessed->alpha_source == AlphaSource_Assumed);
    CHECK(guessed->alpha_type == AlphaType_Straight);
}

TEST_CASE("An override leaves what the file said intact")
{
    ImageLoadOptions opts;
    opts.override_alpha = true;
    opts.alpha_override = AlphaType_PremultipliedNonLinear;

    std::istringstream in(tiff_with_extra_samples(2), std::ios::binary); // unassociated
    auto               images = load_image(in, "rgba.tif", opts);
    REQUIRE(images.size() == 1);

    // what is used, what the file declared, and how it declared it all stay answerable, so the Info panel
    // can say the override contradicts the file rather than fills a gap
    CHECK(images[0]->alpha_type == AlphaType_PremultipliedNonLinear);
    CHECK(images[0]->alpha_type_from_file == AlphaType_Straight);
    CHECK(images[0]->alpha_source == AlphaSource_File);
}

TEST_CASE("A saved TIFF multiplies alpha into its encoded samples")
{
    // HDRView holds premultiplied linear color, so a straight 1.0 under half coverage is linear 0.5 and the
    // file has to hold alpha * OETF(1.0) = alpha, not OETF(0.5) = 0.735
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 0.5f; // premultiplied linear
    img->channels[3](0, 0) = 0.5f;
    img->finalize();

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
    CHECK(reloaded->alpha_type == AlphaType_PremultipliedNonLinear);
    CHECK(reloaded->channels[0](0, 0) == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("TIFF save/load round-trips alpha without repeated premultiplication")
{
    auto original = load_tiff(tiff_with_extra_samples(2));

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

//! Builds a minimal single-strip grayscale TIFF: one IFD, uncompressed, one sample per pixel.
/*!
    `samples` holds raw sample words in the file's own bit width, written in `endian`'s byte order; `bits`
    must be a whole number of bytes. Generated so the byte-order axis is reachable: a byte blob is one
    endianness only, and little-endian is the one where a host-order bug hides.
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

    // 8-byte header + 2-byte entry count + ten 12-byte entries + 4-byte next-IFD pointer
    constexpr uint32_t k_num_entries  = 10;
    constexpr uint32_t k_strip_offset = 8 + 2 + k_num_entries * 12 + 4;

    // a SHORT value occupies the first two bytes of its four-byte field in both byte orders; a LONG fills it
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

#if HDRVIEW_ENABLE_LIBTIFF

namespace
{

// A 4x4 8-bit RGB baseline TIFF carrying a TransferFunction tag (301). TIFF sizes that curve at
// 2^BitsPerSample entries, 256 here, so a reader indexing it over the full 16-bit range runs off the end.
// The curve written is the identity, so a correct decode recovers the stored samples.
std::string tiff_with_transfer_function(std::array<uint8_t, 3> pixel_1_0)
{
    std::string out;
    auto        u16 = [&out](uint16_t v)
    {
        out.push_back(char(v & 0xff));
        out.push_back(char(v >> 8));
    };
    auto u32 = [&out](uint32_t v)
    {
        for (int i = 0; i < 4; ++i) out.push_back(char((v >> (8 * i)) & 0xff));
    };

    out += "II"; // little-endian
    u16(42);     // magic
    const size_t ifd_offset_pos = out.size();
    u32(0); // patched below

    const uint32_t pixels_off = (uint32_t)out.size();
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 3; ++c) out.push_back(char((x == 1 && y == 0) ? pixel_1_0[c] : uint8_t(0)));

    const uint32_t bps_off = (uint32_t)out.size();
    for (int i = 0; i < 3; ++i) u16(8);

    const uint32_t lut_off = (uint32_t)out.size();
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) u16(uint16_t(i * 257)); // identity, 8-bit index to 16-bit value

    const uint32_t ifd_off = (uint32_t)out.size();
    struct Entry
    {
        uint16_t tag, type;
        uint32_t count, value;
    };
    const Entry entries[] = {
        {256, 3, 1, 4},          // ImageWidth
        {257, 3, 1, 4},          // ImageLength
        {258, 3, 3, bps_off},    // BitsPerSample
        {259, 3, 1, 1},          // Compression: none
        {262, 3, 1, 2},          // Photometric: RGB
        {273, 4, 1, pixels_off}, // StripOffsets
        {277, 3, 1, 3},          // SamplesPerPixel
        {278, 3, 1, 4},          // RowsPerStrip
        {279, 4, 1, 48},         // StripByteCounts
        {301, 3, 768, lut_off},  // TransferFunction
    };
    u16((uint16_t)std::size(entries));
    for (auto &e : entries)
    {
        u16(e.tag);
        u16(e.type);
        u32(e.count);
        // a SHORT value with count 1 sits in the low half of the value field
        if (e.type == 3 && e.count == 1)
        {
            u16((uint16_t)e.value);
            u16(0);
        }
        else
            u32(e.value);
    }
    u32(0); // no next IFD

    for (int i = 0; i < 4; ++i) out[ifd_offset_pos + i] = char((ifd_off >> (8 * i)) & 0xff);
    return out;
}

} // namespace

TEST_CASE("An 8-bit TIFF's TransferFunction curve is indexed over its own length, not the 16-bit range")
{
    const std::array<uint8_t, 3> stored{24, 72, 136};
    auto                         bytes = tiff_with_transfer_function(stored);

    std::istringstream in(bytes, std::ios::binary);
    auto               imgs = load_tiff_image(in, "tf.tif");
    REQUIRE(imgs.size() == 1);
    REQUIRE(imgs[0]->channels.size() == 3);

    // the identity curve has to give the stored samples back
    for (int c = 0; c < 3; ++c) CHECK(imgs[0]->channels[c](1, 0) == doctest::Approx(stored[c] / 255.f).epsilon(1e-4));
}
TEST_CASE("A wide-gamut image saved as TIFF records the primaries its samples are in")
{
    // save_tiff_image() asks as_interleaved() not to convert to sRGB, so the samples it writes are in the
    // image's own primaries and the file has to record them in WhitePoint and PrimaryChromaticities.
    const Chromaticities bt2020{{0.708f, 0.292f}, {0.170f, 0.797f}, {0.131f, 0.046f}, {0.3127f, 0.3290f}};

    auto original            = load_tiff(tiff_with_extra_samples(2));
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
#endif
