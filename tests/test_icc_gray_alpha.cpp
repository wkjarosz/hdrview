//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "imageio/icc.h"

#if HDRVIEW_ENABLE_LCMS2

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace
{

// A minimal grayscale ICC profile with a gamma 2.2 tone curve, as lcms serializes one
// (cmsCreateGrayProfile with cmsD50_xyY, saved with cmsSaveProfileToMem). Embedded because lcms2's headers
// are not on this target's include path; HDRView reaches them through libjxl's.
const uint8_t k_gray_gamma22_icc[] = {
    0x00, 0x00, 0x01, 0x5c, 0x6c, 0x63, 0x6d, 0x73, 0x04, 0x40, 0x00, 0x00, 0x6d, 0x6e, 0x74, 0x72, 0x47, 0x52, 0x41,
    0x59, 0x58, 0x59, 0x5a, 0x20, 0x07, 0xea, 0x00, 0x08, 0x00, 0x1b, 0x00, 0x05, 0x00, 0x22, 0x00, 0x22, 0x61, 0x63,
    0x73, 0x70, 0x41, 0x50, 0x50, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6, 0xd6, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0xd3, 0x2d, 0x6c, 0x63, 0x6d, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x64,
    0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x36, 0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x00, 0xec,
    0x00, 0x00, 0x00, 0x4c, 0x77, 0x74, 0x70, 0x74, 0x00, 0x00, 0x01, 0x38, 0x00, 0x00, 0x00, 0x14, 0x6b, 0x54, 0x52,
    0x43, 0x00, 0x00, 0x01, 0x4c, 0x00, 0x00, 0x00, 0x10, 0x6d, 0x6c, 0x75, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x0c, 0x65, 0x6e, 0x55, 0x53, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x1c, 0x00,
    0x67, 0x00, 0x72, 0x00, 0x61, 0x00, 0x79, 0x00, 0x20, 0x00, 0x62, 0x00, 0x75, 0x00, 0x69, 0x00, 0x6c, 0x00, 0x74,
    0x00, 0x2d, 0x00, 0x69, 0x00, 0x6e, 0x00, 0x00, 0x6d, 0x6c, 0x75, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0c, 0x65, 0x6e, 0x55, 0x53, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x4e,
    0x00, 0x6f, 0x00, 0x20, 0x00, 0x63, 0x00, 0x6f, 0x00, 0x70, 0x00, 0x79, 0x00, 0x72, 0x00, 0x69, 0x00, 0x67, 0x00,
    0x68, 0x00, 0x74, 0x00, 0x2c, 0x00, 0x20, 0x00, 0x75, 0x00, 0x73, 0x00, 0x65, 0x00, 0x20, 0x00, 0x66, 0x00, 0x72,
    0x00, 0x65, 0x00, 0x65, 0x00, 0x6c, 0x00, 0x79, 0x58, 0x59, 0x5a, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6,
    0xd6, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xd3, 0x2d, 0x70, 0x61, 0x72, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x33, 0x33};

std::vector<uint8_t> gray_profile()
{
    return std::vector<uint8_t>(std::begin(k_gray_gamma22_icc), std::end(k_gray_gamma22_icc));
}

} // namespace

TEST_CASE("a gray ICC profile linearizes every pixel of a gray+alpha buffer")
{
    // a transform advancing one float per pixel would cover only the first half of the buffer, reading
    // alternating luminance and alpha values as consecutive gray pixels
    constexpr int num_pixels = 64;

    ICCProfile profile{gray_profile()};
    REQUIRE(profile.valid());

    // a constant non-linear luminance, and an alpha ramp that must survive untouched
    constexpr float    encoded_y = 0.5f;
    std::vector<float> pixels(num_pixels * 2);
    std::vector<float> alphas(num_pixels);
    for (int i = 0; i < num_pixels; ++i)
    {
        alphas[i]         = float(i) / float(num_pixels - 1);
        pixels[i * 2 + 0] = encoded_y;
        pixels[i * 2 + 1] = alphas[i];
    }

    // keep_primaries=true would ask for chromaticities, which a gray profile need not carry
    REQUIRE(profile.linearize_pixels(pixels.data(), int3{num_pixels, 1, 2}, /*keep_primaries*/ false));

    // every luminance sample took the same input, and gamma 2.2 is not the identity at 0.5
    const float linearized = pixels[0];
    CHECK(linearized != doctest::Approx(encoded_y).epsilon(1e-4));
    for (int i = 0; i < num_pixels; ++i)
    {
        CAPTURE(i);
        CHECK(pixels[i * 2 + 0] == doctest::Approx(linearized).epsilon(1e-4));
        CHECK(pixels[i * 2 + 1] == doctest::Approx(alphas[i]).epsilon(1e-4));
    }
}

TEST_CASE("a gray ICC profile still linearizes a single-channel buffer")
{
    constexpr int num_pixels = 16;

    ICCProfile profile{gray_profile()};
    REQUIRE(profile.valid());

    std::vector<float> pixels(num_pixels, 0.5f);
    REQUIRE(profile.linearize_pixels(pixels.data(), int3{num_pixels, 1, 1}, /*keep_primaries*/ false));

    const float linearized = pixels[0];
    CHECK(linearized != doctest::Approx(0.5f).epsilon(1e-4));
    for (int i = 0; i < num_pixels; ++i)
    {
        CAPTURE(i);
        CHECK(pixels[i] == doctest::Approx(linearized).epsilon(1e-4));
    }
}

#ifdef HDRVIEW_TEST_LCMS_TESTBED_DIR

TEST_CASE("a CMYK ICC profile converts ink to color, and only inverts it when told to")
{
    // lcms's own CMYK printer profile. It says of itself that it is not suitable for real use, which is
    // beside the point: its color space is CMYK, and that is the property the conversion turns on.
    std::ifstream in(std::string(HDRVIEW_TEST_LCMS_TESTBED_DIR) + "/test1.icc", std::ios::binary);
    REQUIRE(in.good());
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    ICCProfile        profile{reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()};
    REQUIRE(profile.valid());
    REQUIRE(profile.is_CMYK());

    // bare paper and a solid hit of black, as a file storing ink directly writes them
    const auto ink = [] { return std::vector<float>{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f}; };

    // A CMYK profile has no primaries of its own to keep, so the default has to convert anyway rather than
    // asking for colorants that are not there and giving up.
    for (bool keep_primaries : {true, false})
    {
        CAPTURE(keep_primaries);
        auto pixels = ink();
        REQUIRE(profile.linearize_pixels(pixels.data(), int3{2, 1, 4}, keep_primaries, nullptr, nullptr,
                                         /*cmyk_is_inverted*/ false));

        for (int c = 0; c < 3; ++c)
        {
            CAPTURE(c);
            CHECK(pixels[c] > 0.5f);     // paper
            CHECK(pixels[4 + c] < 0.2f); // solid black
        }
        CHECK(pixels[3] == doctest::Approx(1.f)); // the conversion writes an opaque alpha
    }

    // Adobe's JPEGs store the same ink inverted, and nothing in the profile says which convention a file
    // used, so the caller states it. Bare paper is what separates the two: read the wrong way up it is not
    // an absence of ink but a full hit of every one.
    auto inverted = ink();
    REQUIRE(profile.linearize_pixels(inverted.data(), int3{2, 1, 4}, /*keep_primaries*/ false, nullptr, nullptr,
                                     /*cmyk_is_inverted*/ true));
    for (int c = 0; c < 3; ++c)
    {
        CAPTURE(c);
        CHECK(inverted[c] < 0.2f);
    }
}

#endif // HDRVIEW_TEST_LCMS_TESTBED_DIR

#endif // HDRVIEW_ENABLE_LCMS2
