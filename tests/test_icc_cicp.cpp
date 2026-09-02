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

// The smallest byte sequence carrying an ICC `cicp` tag: a 128-byte header, a one-entry tag table, and the
// tag. LCMS refuses to open it as a profile, so the code points have to be readable from the bytes alone.
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
        put_be32(v, 0, (uint32_t)v.size());
        put_be32(v, 128, 0); // no tags
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }
}

// The ICC PCS is normalized to media white, so transforming through the profile would clamp away everything
// above diffuse white. PQ's 1.0 is 10000 nits, a little over 49 against a 203-nit reference white.
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
    CHECK(pixels[0] == doctest::Approx(49.2611f).epsilon(1e-3));
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
