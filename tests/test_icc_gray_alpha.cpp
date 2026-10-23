//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "imageio/icc.h"

#if HDRVIEW_ENABLE_LCMS2

#include <fstream>
#include <string>
#include <vector>

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
