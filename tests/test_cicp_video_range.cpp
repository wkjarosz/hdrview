//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/icc.h"
#include "imageio/image_loader.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace
{

void put_be32(std::vector<uint8_t> &v, size_t o, uint32_t x)
{
    v[o]     = uint8_t(x >> 24);
    v[o + 1] = uint8_t(x >> 16);
    v[o + 2] = uint8_t(x >> 8);
    v[o + 3] = uint8_t(x);
}

// An ICC profile carrying nothing but a `cicp` tag (ICC.1:2022); TIFF stores the blob opaquely and never
// parses the rest of it.
std::vector<uint8_t> icc_with_cicp(uint8_t cp, uint8_t tc, uint8_t mc, uint8_t fr)
{
    std::vector<uint8_t> v(128 + 4 + 12 + 12, 0);
    put_be32(v, 0, (uint32_t)v.size());
    put_be32(v, 128, 1);           // one tag
    put_be32(v, 132, 0x63696370u); // 'cicp'
    put_be32(v, 136, 144);         // offset
    put_be32(v, 140, 12);          // size
    put_be32(v, 144, 0x63696370u); // type signature
    v[152] = cp;
    v[153] = tc;
    v[154] = mc;
    v[155] = fr;
    return v;
}

// 16-bit narrow ("video") range puts black at 16 and white at 235, scaled by the sample depth.
constexpr uint16_t k_narrow_black = 16 * 256;  // 4096
constexpr uint16_t k_narrow_white = 235 * 256; // 60160
constexpr uint16_t k_mid          = 128 * 256; // 32768

// A 3x1, 16-bit, single-sample uncompressed little-endian TIFF holding `codes`, with an embedded ICC profile.
std::string make_gray16_tiff(const std::vector<uint16_t> &codes, const std::vector<uint8_t> &icc)
{
    struct Entry
    {
        uint16_t tag, type;
        uint32_t count, value;
    };
    const uint32_t width = (uint32_t)codes.size();

    // Header (8) + entry count (2) + 11 entries (12 each) + next-IFD (4), then the out-of-line ICC blob,
    // then the pixel data.
    const uint32_t ifd_off   = 8;
    const uint32_t n_entries = 11;
    const uint32_t after_ifd = ifd_off + 2 + n_entries * 12 + 4;
    const uint32_t icc_off   = after_ifd;
    const uint32_t data_off  = icc_off + (uint32_t)icc.size();
    const uint32_t data_len  = width * 2;

    const Entry entries[n_entries] = {
        {256, 4, 1, width},                        // ImageWidth
        {257, 4, 1, 1},                            // ImageLength
        {258, 3, 1, 16},                           // BitsPerSample
        {259, 3, 1, 1},                            // Compression = none
        {262, 3, 1, 1},                            // Photometric = min-is-black
        {273, 4, 1, data_off},                     // StripOffsets
        {277, 3, 1, 1},                            // SamplesPerPixel
        {278, 4, 1, 1},                            // RowsPerStrip
        {279, 4, 1, data_len},                     // StripByteCounts
        {339, 3, 1, 1},                            // SampleFormat = unsigned int
        {34675, 7, (uint32_t)icc.size(), icc_off}, // ICCProfile
    };

    std::string out;
    auto        u16 = [&out](uint16_t x)
    {
        out += char(x & 0xFF);
        out += char(x >> 8);
    };
    auto u32 = [&out](uint32_t x)
    {
        out += char(x & 0xFF);
        out += char((x >> 8) & 0xFF);
        out += char((x >> 16) & 0xFF);
        out += char((x >> 24) & 0xFF);
    };

    out += "II";
    u16(42);
    u32(ifd_off);
    u16((uint16_t)n_entries);
    for (const auto &e : entries)
    {
        u16(e.tag);
        u16(e.type);
        u32(e.count);
        // A SHORT that fits in the value field is stored in its low half, left-justified in the four bytes.
        if (e.type == 3 && e.count == 1)
        {
            u16((uint16_t)e.value);
            u16(0);
        }
        else
            u32(e.value);
    }
    u32(0); // no next IFD
    out.append(reinterpret_cast<const char *>(icc.data()), icc.size());
    for (uint16_t c : codes) u16(c);
    return out;
}

ImagePtr load(const std::string &bytes, const char *name)
{
    std::istringstream in(bytes, std::ios::binary);
    auto               loaded = load_image(in, name);
    REQUIRE(loaded.size() == 1);
    return loaded.front();
}

} // namespace

// The range flag lives only in the cicp tag; TIFF has no field of its own for it. Transfer characteristic 8
// is Linear, so these assertions see the dequantization alone.
TEST_CASE("A cicp tag declaring narrow video range is dequantized 16..235")
{
    const std::vector<uint16_t> codes{k_narrow_black, k_narrow_white, k_mid};

    SUBCASE("narrow range")
    {
        auto img = load(make_gray16_tiff(codes, icc_with_cicp(1, 8, 0, /*full_range*/ 0)), "narrow.tif");
        REQUIRE(img->channels.size() >= 1);
        const auto &ch = img->channels[0];
        REQUIRE(ch.size().x == 3);

        CHECK(ch(0, 0) == doctest::Approx(0.f).epsilon(1e-5));
        CHECK(ch(1, 0) == doctest::Approx(1.f).epsilon(1e-5));
        CHECK(ch(2, 0) == doctest::Approx((128.f - 16.f) / 219.f).epsilon(1e-5));
    }

    SUBCASE("full range, same samples")
    {
        auto img = load(make_gray16_tiff(codes, icc_with_cicp(1, 8, 0, /*full_range*/ 1)), "full.tif");
        REQUIRE(img->channels.size() >= 1);
        const auto &ch = img->channels[0];
        REQUIRE(ch.size().x == 3);

        // without the flag, black and white would sit at the ends of the full range
        CHECK(ch(0, 0) == doctest::Approx(k_narrow_black / 65535.f).epsilon(1e-5));
        CHECK(ch(1, 0) == doctest::Approx(k_narrow_white / 65535.f).epsilon(1e-5));
        CHECK(ch(2, 0) == doctest::Approx(k_mid / 65535.f).epsilon(1e-5));
    }

    SUBCASE("narrow range keeps excursions beyond black and white")
    {
        // codes outside 16..235 are legal and must survive as values outside [0,1]; HDR test patterns use
        // that headroom
        auto        img = load(make_gray16_tiff({0, 65535}, icc_with_cicp(1, 8, 0, 0)), "excursions.tif");
        const auto &ch  = img->channels[0];
        CHECK(ch(0, 0) < 0.f);
        CHECK(ch(1, 0) > 1.f);
    }
}

TEST_CASE("icc_cicp_tag reports the video range flag")
{
    auto narrow = icc_with_cicp(9, 18, 0, 0);
    auto full   = icc_with_cicp(9, 18, 0, 1);

    CHECK(icc_cicp_tag(narrow.data(), narrow.size()).fr() == 0);
    CHECK(icc_cicp_tag(full.data(), full.size()).fr() == 1);
    CHECK(icc_cicp_tag(narrow.data(), narrow.size()).valid());
    CHECK(!icc_cicp_tag(nullptr, 0).valid());
}
