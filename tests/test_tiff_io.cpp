//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/tiff.h"

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
    auto read_u32 = [&](size_t o)
    { return uint32_t(read_u16(o)) | (uint32_t(read_u16(o + 2)) << 16); };

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
