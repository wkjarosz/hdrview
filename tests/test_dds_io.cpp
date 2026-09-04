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

#include "test_support.h"

using namespace hdrview_test;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

// The alpha nibble every texel of the synthetic block carries, and the value it decodes to.
constexpr uint8_t k_alpha_nibble = 8;
constexpr float   k_alpha        = float(k_alpha_nibble) / 15.f;

// A 4x4 single-block DX9 DDS under the given FourCC. DXT2 and DXT3 (like DXT4 and DXT5) are the same bits in
// the same layout; the FourCC alone says whether the colors are already multiplied by alpha.
std::string make_one_block_dds(const char *fourcc)
{
    std::vector<uint8_t> v{'D', 'D', 'S', ' '};

    put<uint32_t>(v, 124);                                // dwSize
    put<uint32_t>(v, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000); // caps|height|width|pixelformat|linearsize
    put<uint32_t>(v, 4);                                  // height
    put<uint32_t>(v, 4);                                  // width
    put<uint32_t>(v, 16);                                 // linear size: one 16-byte block
    put<uint32_t>(v, 0);                                  // depth
    put<uint32_t>(v, 0);                                  // mipmap count
    for (int i = 0; i < 11; ++i) put<uint32_t>(v, 0);     // reserved

    // DDS_PIXELFORMAT: FourCC only, no DDPF_ALPHAPREMULT, so the format tag has to carry it on its own
    put<uint32_t>(v, 32);  // dwSize
    put<uint32_t>(v, 0x4); // DDPF_FOURCC
    v.insert(v.end(), {uint8_t(fourcc[0]), uint8_t(fourcc[1]), uint8_t(fourcc[2]), uint8_t(fourcc[3])});
    for (int i = 0; i < 5; ++i) put<uint32_t>(v, 0); // bit count and masks

    put<uint32_t>(v, 0x1000); // dwCaps = TEXTURE
    for (int i = 0; i < 4; ++i) put<uint32_t>(v, 0);

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
    auto img = load_bytes(bytes, name);
    REQUIRE(img);
    return img;
}

} // namespace

// DXT2 and DXT4 store colors already multiplied by alpha; DXT3 and DXT5 store them straight. Nothing but the
// FourCC records that in a DX9 file.
TEST_CASE("DDS premultiplied-alpha formats are not premultiplied a second time")
{
    // the alpha has to be well away from 0 and 1 for the comparison below to mean anything
    REQUIRE(k_alpha > 0.1f);
    REQUIRE(k_alpha < 0.9f);

    // BC2's pair and BC3's, each the same bits under two FourCCs
    for (auto fourccs : {std::pair{"DXT2", "DXT3"}, std::pair{"DXT4", "DXT5"}})
    {
        // Named apart: C++17 cannot capture a structured binding in a lambda.
        const char *premultiplied_cc = fourccs.first;
        const char *straight_cc      = fourccs.second;
        CAPTURE(premultiplied_cc);
        auto straight_bytes      = make_one_block_dds(straight_cc);
        auto premultiplied_bytes = make_one_block_dds(premultiplied_cc);

        // the same payload under both tags, so any difference is the alpha handling alone
        REQUIRE(straight_bytes.size() == premultiplied_bytes.size());
        REQUIRE(straight_bytes.substr(128) == premultiplied_bytes.substr(128));

        auto straight      = load_dds(straight_bytes, "straight.dds");
        auto premultiplied = load_dds(premultiplied_bytes, "premultiplied.dds");

        REQUIRE(straight->channels.size() == 4);
        REQUIRE(premultiplied->channels.size() == 4);

        CHECK(straight->transparency == TransparencyType_Straight);
        CHECK(premultiplied->transparency == TransparencyType_PremultipliedLinear);

        // both decode to the same alpha
        for (auto *img : {straight.get(), premultiplied.get()})
            CHECK(img->channels[3](0, 0) == doctest::Approx(k_alpha).epsilon(1e-4));

        for (int c = 0; c < 3; ++c)
        {
            const float straight_value      = straight->channels[c](0, 0);
            const float premultiplied_value = premultiplied->channels[c](0, 0);

            REQUIRE(premultiplied_value > 0.f);

            // HDRView stores premultiplied values, so finalize() scales the straight file by alpha and
            // leaves the other one alone; the two differ by alpha
            CHECK(straight_value == doctest::Approx(premultiplied_value * k_alpha).epsilon(1e-4));
            CHECK(premultiplied_value != doctest::Approx(straight_value).epsilon(1e-4));
        }
    }
}

#ifdef HDRVIEW_TEST_BCDEC_DIR

// bcdec's own test images, one per block format, decoded by the library HDRView links for exactly this.
// The BC7 one reaches a left shift of an uninitialized endpoint inside bcdec, suppressed for UBSan in
// .github/ubsan-suppressions.txt.
TEST_CASE("Every block format bcdec ships decodes to the size its header declares")
{
    namespace fs = std::filesystem;

    int files = 0;
    for (const auto &entry : fs::directory_iterator(HDRVIEW_TEST_BCDEC_DIR))
    {
        const auto path = entry.path();
        if (path.extension() != ".dds")
            continue;

        const std::string name = path.filename().string();
        CAPTURE(name);

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

        // DDS_HEADER follows the four-byte magic: dwSize, dwFlags, then height and width
        REQUIRE(bytes.size() > 20);
        REQUIRE(bytes.compare(0, 4, "DDS ") == 0);
        auto u32 = [&bytes](size_t at)
        {
            return (uint32_t)(uint8_t)bytes[at] | ((uint32_t)(uint8_t)bytes[at + 1] << 8) |
                   ((uint32_t)(uint8_t)bytes[at + 2] << 16) | ((uint32_t)(uint8_t)bytes[at + 3] << 24);
        };

        auto img = load_bytes(load_dds_image, bytes, name.c_str());
        REQUIRE(img);
        img->finalize();

        CHECK(img->size().y == (int)u32(12));
        CHECK(img->size().x == (int)u32(16));
        REQUIRE_FALSE(img->channels.empty());
        ++files;
    }
    CHECK(files > 0);
}

#endif // HDRVIEW_TEST_BCDEC_DIR
