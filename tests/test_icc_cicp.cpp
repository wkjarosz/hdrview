//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "imageio/icc.h"
#include "imageio/image_loader.h"

#include "test_support.h"

#include "image.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace hdrview_test;

namespace
{

// The smallest byte sequence carrying an ICC `cicp` tag: a 128-byte header, a one-entry tag table, and the
// tag. LCMS refuses to open it as a profile, so the code points have to be readable from the bytes alone.
std::vector<uint8_t> icc_bytes_with_cicp(uint8_t cp, uint8_t tc, uint8_t mc, uint8_t fr)
{
    std::vector<uint8_t> v(128 + 4 + 12 + 12, 0);
    patch<uint32_t>(v, 0, (uint32_t)v.size(), Endian::Big); // profile size
    patch<uint32_t>(v, 128, 1, Endian::Big);                // one tag
    patch<uint32_t>(v, 132, 0x63696370u, Endian::Big);      // 'cicp'
    patch<uint32_t>(v, 136, 144, Endian::Big);              // tag offset
    patch<uint32_t>(v, 140, 12, Endian::Big);               // tag size
    patch<uint32_t>(v, 144, 0x63696370u, Endian::Big);      // tag type signature, repeated
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

/// A 3x1, 16-bit, single-sample uncompressed TIFF holding `codes`, with `icc` embedded.
std::string gray16_tiff(const std::vector<uint16_t> &codes, const std::vector<uint8_t> &icc)
{
    std::vector<uint8_t> strip;
    for (uint16_t c : codes) put(strip, c);

    return tiff_bytes(Endian::Little,
                      {
                          {256, 4, 1, uint32_t(codes.size()), {}},  // ImageWidth
                          {257, 4, 1, 1, {}},                       // ImageLength
                          {258, 3, 1, 16, {}},                      // BitsPerSample
                          {259, 3, 1, 1, {}},                       // Compression: none
                          {262, 3, 1, 1, {}},                       // Photometric: BlackIsZero
                          {277, 3, 1, 1, {}},                       // SamplesPerPixel
                          {278, 4, 1, 1, {}},                       // RowsPerStrip
                          {339, 3, 1, 1, {}},                       // SampleFormat: unsigned int
                          {34675, 7, uint32_t(icc.size()), 0, icc}, // ICCProfile
                      },
                      strip);
}

} // namespace

// ICC.1:2022 lets a profile declare CICP code points directly. HDRView reads them from the profile bytes;
// LCMS only gained the tag in 2.16 and libjxl vendors 2.10.
TEST_CASE("ICC cicp tag is read from the profile bytes")
{
    SUBCASE("PQ at BT.2020 primaries")
    {
        auto bytes = icc_bytes_with_cicp(9, 16, 0, 1);
        auto codes = ICCProfile(bytes.data(), bytes.size()).cicp();
        CHECK(codes.valid());
        CHECK(codes.cp() == 9);
        CHECK(codes.tc() == 16);
        CHECK(codes.is_HDR());
        CHECK(codes.transfer_function().type == TransferFunction::BT2100_PQ);
    }

    SUBCASE("HLG at BT.2020 primaries")
    {
        auto bytes = icc_bytes_with_cicp(9, 18, 0, 1);
        auto codes = ICCProfile(bytes.data(), bytes.size()).cicp();
        CHECK(codes.valid());
        CHECK(codes.tc() == 18);
        CHECK(codes.is_HDR());
        CHECK(codes.transfer_function().type == TransferFunction::BT2100_HLG);
    }

    SUBCASE("an SDR tag is read but is not HDR")
    {
        auto bytes = icc_bytes_with_cicp(1, 13, 0, 1);
        auto codes = ICCProfile(bytes.data(), bytes.size()).cicp();
        CHECK(codes.valid());
        CHECK(!codes.is_HDR());
    }

    SUBCASE("a profile without the tag reports no code points")
    {
        std::vector<uint8_t> v(132, 0);
        patch<uint32_t>(v, 0, (uint32_t)v.size(), Endian::Big);
        patch<uint32_t>(v, 128, 0, Endian::Big); // no tags
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }
}

// The ICC PCS is normalized to media white, so transforming through the profile would clamp away everything
// above diffuse white.
TEST_CASE("An ICC profile whose cicp tag declares PQ keeps its HDR range")
{
    auto       bytes = icc_bytes_with_cicp(9, 16, 0, 1);
    ICCProfile profile(bytes.data(), bytes.size());

    std::vector<float> pixels{1.f, 1.f, 1.f, 0.5f, 0.5f, 0.5f};
    std::string        description;
    Chromaticities     chr;

    REQUIRE(profile.linearize_pixels(pixels.data(), int3{2, 1, 3}, /*keep_primaries*/ true, &description, &chr));

    // fully-encoded PQ is far above SDR white
    CHECK(pixels[0] > 40.f);
    CHECK(pixels[0] == doctest::Approx(10000.f / 203.f).epsilon(1e-3)); // PQ 1.0 is 10000 nits over a 203-nit white
    // mid-code PQ is a much dimmer absolute level, so this is the curve and not a scale factor
    CHECK(pixels[3] < 1.f);
    CHECK(pixels[3] > 0.f);
}

// The tag is parsed straight from untrusted bytes.
TEST_CASE("ICC cicp parsing rejects malformed profiles")
{
    auto good = icc_bytes_with_cicp(9, 16, 0, 1);

    SUBCASE("truncated before the tag table")
    {
        for (size_t n : {size_t(0), size_t(4), size_t(127), size_t(131)})
        {
            std::vector<uint8_t> v(good.begin(), good.begin() + (ptrdiff_t)n);
            CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
        }
    }

    SUBCASE("tag count larger than the buffer can hold")
    {
        auto v = good;
        patch<uint32_t>(v, 128, 0xFFFFFFFFu, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag offset and size point past the end")
    {
        auto v = good;
        patch<uint32_t>(v, 136, 0xFFFFFF00u, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());

        v = good;
        patch<uint32_t>(v, 140, 0xFFFFFFFFu, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag size too small to hold the code points")
    {
        auto v = good;
        patch<uint32_t>(v, 140, 11, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag type signature does not match the table entry")
    {
        auto v = good;
        patch<uint32_t>(v, 144, 0x64656164u, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("a null buffer is handled") { CHECK(!ICCProfile(nullptr, 0).cicp().valid()); }
}

// The range flag lives only in the cicp tag; TIFF has no field of its own for it. Transfer characteristic 8
// is Linear, so these assertions see the dequantization alone.
TEST_CASE("A cicp tag declaring narrow video range is dequantized 16..235")
{
    const std::vector<uint16_t> codes{k_narrow_black, k_narrow_white, k_mid};

    SUBCASE("narrow range")
    {
        auto img = load_bytes(gray16_tiff(codes, icc_bytes_with_cicp(1, 8, 0, /*full_range*/ 0)), "narrow.tif");
        REQUIRE(img);
        const auto &ch = img->channels[0];
        REQUIRE(ch.size().x == 3);

        CHECK(ch(0, 0) == doctest::Approx(0.f).epsilon(1e-5));
        CHECK(ch(1, 0) == doctest::Approx(1.f).epsilon(1e-5));
        CHECK(ch(2, 0) == doctest::Approx((128.f - 16.f) / 219.f).epsilon(1e-5));
    }

    SUBCASE("full range, same samples")
    {
        auto img = load_bytes(gray16_tiff(codes, icc_bytes_with_cicp(1, 8, 0, /*full_range*/ 1)), "full.tif");
        REQUIRE(img);
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
        auto img = load_bytes(gray16_tiff({0, 65535}, icc_bytes_with_cicp(1, 8, 0, 0)), "excursions.tif");
        REQUIRE(img);
        const auto &ch = img->channels[0];
        CHECK(ch(0, 0) < 0.f);
        CHECK(ch(1, 0) > 1.f);
    }
}

TEST_CASE("icc_cicp_tag reports the video range flag")
{
    auto narrow = icc_bytes_with_cicp(9, 18, 0, 0);
    auto full   = icc_bytes_with_cicp(9, 18, 0, 1);

    CHECK(icc_cicp_tag(narrow.data(), narrow.size()).fr() == 0);
    CHECK(icc_cicp_tag(full.data(), full.size()).fr() == 1);
    CHECK(icc_cicp_tag(narrow.data(), narrow.size()).valid());
    CHECK(!icc_cicp_tag(nullptr, 0).valid());
}

#ifdef HDRVIEW_TEST_LIBJXL_DIR

// The Compact-ICC-Profiles set libjxl vendors writes each color space four ways, over ICC v2 and v4 and in
// the cut-down "micro" and "magic" forms, named <space>-<version>[-form].icc. The names are the oracle:
// whatever primaries one encoding yields, its siblings have to yield the same.
TEST_CASE("Profiles encoding the same color space agree on its primaries")
{
    namespace fs = std::filesystem;

    std::map<std::string, std::vector<std::pair<std::string, Chromaticities>>> by_space;
    int                                                                        read = 0;

    for (const auto &entry :
         fs::directory_iterator(std::string(HDRVIEW_TEST_LIBJXL_DIR) + "/external/Compact-ICC-Profiles/profiles"))
    {
        const auto path = entry.path();
        if (path.extension() != ".icc")
            continue;

        const std::string name = path.filename().string();
        CAPTURE(name);

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        REQUIRE(bytes.size() > 128);
        ++read;

        // reading one must not throw, whatever it holds
        ICCProfile     profile{reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()};
        Chromaticities chr;
        if (!profile.valid() || !profile.extract_chromaticities(&chr))
            continue; // a gray or LUT-shaped profile carries no colorants to read

        by_space[name.substr(0, name.find("-v"))].emplace_back(name, chr);
    }
    CHECK(read >= 40);

    for (const auto &[space, profiles] : by_space)
    {
        CAPTURE(space);
        const auto &[first_name, want] = profiles.front();
        for (const auto &[name, got] : profiles)
        {
            CAPTURE(first_name);
            CAPTURE(name);
            CHECK(got.red.x == doctest::Approx(want.red.x).epsilon(1e-3));
            CHECK(got.green.y == doctest::Approx(want.green.y).epsilon(1e-3));
            CHECK(got.blue.x == doctest::Approx(want.blue.x).epsilon(1e-3));
            CHECK(got.white.x == doctest::Approx(want.white.x).epsilon(1e-3));
        }
    }

    // Agreement alone is self-consistent: read every colorant out of the wrong tag and the siblings still
    // match each other. sRGB's primaries are published, so one group is anchored to them.
    const auto srgb = by_space.find("sRGB");
    REQUIRE(srgb != by_space.end());
    for (const auto &[name, got] : srgb->second)
    {
        CAPTURE(name);
        CHECK(got.red.x == doctest::Approx(0.64f).epsilon(1e-3));
        CHECK(got.red.y == doctest::Approx(0.33f).epsilon(1e-3));
        CHECK(got.green.x == doctest::Approx(0.30f).epsilon(1e-3));
        CHECK(got.green.y == doctest::Approx(0.60f).epsilon(1e-3));
        CHECK(got.blue.x == doctest::Approx(0.15f).epsilon(1e-3));
        CHECK(got.blue.y == doctest::Approx(0.06f).epsilon(1e-3));
        CHECK(got.white.x == doctest::Approx(0.3127f).epsilon(1e-3));
        CHECK(got.white.y == doctest::Approx(0.3290f).epsilon(1e-3));
    }
}

#endif // HDRVIEW_TEST_LIBJXL_DIR
