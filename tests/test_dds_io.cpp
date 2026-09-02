//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/dds.h"
#include "imageio/image_loader.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace
{

void put_le32(std::vector<uint8_t> &v, uint32_t value)
{
    v.insert(v.end(), {uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)});
}

// The alpha nibble every texel of the synthetic block carries, and the value it decodes to.
constexpr uint8_t k_alpha_nibble = 8;
constexpr float   k_alpha        = float(k_alpha_nibble) / 15.f;

// A 4x4 single-block DX9 DDS under the given FourCC. DXT2 and DXT3 (like DXT4 and DXT5) are the same bits in
// the same layout; the FourCC alone says whether the colors are already multiplied by alpha.
std::string make_one_block_dds(const char fourcc[4])
{
    std::vector<uint8_t> v{'D', 'D', 'S', ' '};

    put_le32(v, 124);                                // dwSize
    put_le32(v, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000); // caps|height|width|pixelformat|linearsize
    put_le32(v, 4);                                  // height
    put_le32(v, 4);                                  // width
    put_le32(v, 16);                                 // linear size: one 16-byte block
    put_le32(v, 0);                                  // depth
    put_le32(v, 0);                                  // mipmap count
    for (int i = 0; i < 11; ++i) put_le32(v, 0);     // reserved

    // DDS_PIXELFORMAT: FourCC only, no DDPF_ALPHAPREMULT, so the format tag has to carry it on its own
    put_le32(v, 32);  // dwSize
    put_le32(v, 0x4); // DDPF_FOURCC
    v.insert(v.end(), {uint8_t(fourcc[0]), uint8_t(fourcc[1]), uint8_t(fourcc[2]), uint8_t(fourcc[3])});
    for (int i = 0; i < 5; ++i) put_le32(v, 0); // bit count and masks

    put_le32(v, 0x1000); // dwCaps = TEXTURE
    for (int i = 0; i < 4; ++i) put_le32(v, 0);

    // BC2 block: eight bytes of 4-bit alpha, then a DXT1-style color block
    const uint8_t packed_alpha = uint8_t(k_alpha_nibble | (k_alpha_nibble << 4));
    for (int i = 0; i < 8; ++i) v.push_back(packed_alpha);

    // both endpoints the same mid-gray, so all sixteen texels decode to one color whatever the
    // interpolation does
    const uint16_t rgb565 = uint16_t((16u << 11) | (32u << 5) | 16u);
    v.insert(v.end(), {uint8_t(rgb565 & 0xFF), uint8_t(rgb565 >> 8), uint8_t(rgb565 & 0xFF), uint8_t(rgb565 >> 8)});
    for (int i = 0; i < 4; ++i) v.push_back(0); // all indices 0

    return std::string(v.begin(), v.end());
}

ImagePtr load_dds(const std::string &bytes, const char *name)
{
    // load_image(), not load_dds_image(): finalize() premultiplies straight alpha, and it runs here
    std::istringstream in(bytes, std::ios::binary);
    auto               loaded = load_image(in, name);
    REQUIRE(loaded.size() == 1);
    return loaded.front();
}

} // namespace

// DXT2 and DXT4 store colors already multiplied by alpha; DXT3 and DXT5 store them straight. Nothing but the
// FourCC records that in a DX9 file.
TEST_CASE("DDS premultiplied-alpha formats are not premultiplied a second time")
{
    auto straight_bytes     = make_one_block_dds("DXT3");
    auto premultiplied_byte = make_one_block_dds("DXT2");

    // the same payload under both tags, so any difference is the alpha handling alone
    REQUIRE(straight_bytes.size() == premultiplied_byte.size());
    REQUIRE(straight_bytes.substr(128) == premultiplied_byte.substr(128));

    auto straight      = load_dds(straight_bytes, "straight.dds");
    auto premultiplied = load_dds(premultiplied_byte, "premultiplied.dds");

    REQUIRE(straight->channels.size() == 4);
    REQUIRE(premultiplied->channels.size() == 4);

    CHECK(straight->alpha_type == AlphaType_Straight);
    CHECK(premultiplied->alpha_type == AlphaType_PremultipliedLinear);

    // both decode to the same alpha, which has to be well away from 0 and 1 for the rest to mean anything
    for (auto *img : {straight.get(), premultiplied.get()})
        CHECK(img->channels[3](0, 0) == doctest::Approx(k_alpha).epsilon(1e-4));
    REQUIRE(k_alpha > 0.1f);
    REQUIRE(k_alpha < 0.9f);

    for (int c = 0; c < 3; ++c)
    {
        const float straight_value      = straight->channels[c](0, 0);
        const float premultiplied_value = premultiplied->channels[c](0, 0);

        REQUIRE(premultiplied_value > 0.f);

        // HDRView stores premultiplied values, so finalize() scales the straight file by alpha and leaves
        // the other one alone; the two differ by alpha
        CHECK(straight_value == doctest::Approx(premultiplied_value * k_alpha).epsilon(1e-4));
        CHECK(premultiplied_value != doctest::Approx(straight_value).epsilon(1e-4));
    }
}

TEST_CASE("DDS DXT4 is premultiplied and DXT5 is straight")
{
    auto dxt5 = load_dds(make_one_block_dds("DXT5"), "dxt5.dds");
    auto dxt4 = load_dds(make_one_block_dds("DXT4"), "dxt4.dds");

    CHECK(dxt5->alpha_type == AlphaType_Straight);
    CHECK(dxt4->alpha_type == AlphaType_PremultipliedLinear);
}
