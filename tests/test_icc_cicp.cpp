//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "imageio/icc.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

void put_be32(std::vector<uint8_t> &v, size_t offset, uint32_t value)
{
    v[offset + 0] = uint8_t(value >> 24);
    v[offset + 1] = uint8_t(value >> 16);
    v[offset + 2] = uint8_t(value >> 8);
    v[offset + 3] = uint8_t(value);
}

// Builds the smallest byte sequence that carries an ICC `cicp` tag: a 128-byte header, a one-entry tag
// table, and the tag itself. LCMS will refuse to open it as a profile, which is deliberate -- the code
// points have to be readable from the bytes alone, independent of what LCMS makes of the rest.
std::vector<uint8_t> icc_bytes_with_cicp(uint8_t cp, uint8_t tc, uint8_t mc, uint8_t fr)
{
    std::vector<uint8_t> v(128 + 4 + 12 + 12, 0);
    put_be32(v, 0, (uint32_t)v.size()); // profile size
    put_be32(v, 128, 1);                // one tag
    put_be32(v, 132, 0x63696370u);      // 'cicp'
    put_be32(v, 136, 144);              // tag offset
    put_be32(v, 140, 12);               // tag size
    put_be32(v, 144, 0x63696370u);      // tag type signature, repeated
    v[152] = cp;
    v[153] = tc;
    v[154] = mc;
    v[155] = fr;
    return v;
}

} // namespace

// ICC.1:2022 lets a profile declare CICP code points directly. HDRView reads them out of the profile bytes
// rather than through LCMS, which only gained the tag in 2.16 -- so a build linking an older LCMS (libjxl
// vendors 2.10) would otherwise see nothing here.
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
        put_be32(v, 0, (uint32_t)v.size());
        put_be32(v, 128, 0); // no tags
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }
}

// The defect this guards: an HDR image whose transfer function is declared only by the profile's cicp tag
// used to be linearized by transforming through the profile itself, and the ICC PCS is normalized to media
// white -- so everything above diffuse white was clamped away. PQ's 1.0 is 10000 nits, which against a
// 203-nit reference white is a little over 49, and that is what has to survive.
TEST_CASE("An ICC profile whose cicp tag declares PQ keeps its HDR range")
{
    auto       bytes = icc_bytes_with_cicp(9, 16, 0, 1);
    ICCProfile profile(bytes.data(), bytes.size());

    std::vector<float> pixels{1.f, 1.f, 1.f, 0.5f, 0.5f, 0.5f};
    std::string        description;
    Chromaticities     chr;

    REQUIRE(profile.linearize_pixels(pixels.data(), int3{2, 1, 3}, /*keep_primaries*/ true, &description, &chr));

    // Fully-encoded PQ is far above SDR white; the pre-fix path could not return anything above it.
    CHECK(pixels[0] > 40.f);
    CHECK(pixels[0] == doctest::Approx(49.2611f).epsilon(1e-3));
    // Mid-code PQ is a much dimmer absolute level, so the curve is being applied rather than a scale factor.
    CHECK(pixels[3] < 1.f);
    CHECK(pixels[3] > 0.f);
}

// The tag is parsed straight from untrusted bytes, so a truncated or self-inconsistent profile has to fall
// out as "no code points" rather than reading past the buffer.
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
        put_be32(v, 128, 0xFFFFFFFFu);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag offset and size point past the end")
    {
        auto v = good;
        put_be32(v, 136, 0xFFFFFF00u);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());

        v = good;
        put_be32(v, 140, 0xFFFFFFFFu);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag size too small to hold the code points")
    {
        auto v = good;
        put_be32(v, 140, 11);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag type signature does not match the table entry")
    {
        auto v = good;
        put_be32(v, 144, 0x64656164u);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("a null buffer is handled") { CHECK(!ICCProfile(nullptr, 0).cicp().valid()); }
}
