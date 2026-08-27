//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "imageio/icc.h"

#if HDRVIEW_ENABLE_LCMS2

#include <lcms2.h>

#include <cmath>
#include <vector>

namespace
{

// A minimal grayscale ICC profile with a pure power transfer curve, serialized the way a file would
// carry it. cmsCreateGrayProfile takes ownership of neither argument, so the tone curve is freed here.
std::vector<uint8_t> make_gray_profile(double gamma)
{
    cmsToneCurve *curve = cmsBuildGamma(nullptr, gamma);
    REQUIRE(curve != nullptr);
    cmsHPROFILE profile = cmsCreateGrayProfile(cmsD50_xyY(), curve);
    cmsFreeToneCurve(curve);
    REQUIRE(profile != nullptr);

    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    std::vector<uint8_t> data(size);
    bool                 ok = cmsSaveProfileToMem(profile, data.data(), &size) != 0;
    cmsCloseProfile(profile);
    REQUIRE(ok);
    return data;
}

} // namespace

TEST_CASE("a gray ICC profile linearizes every pixel of a gray+alpha buffer")
{
    // Two floats per pixel, so the transform has to advance two floats per pixel as well. Read as
    // TYPE_GRAY_FLT it advances one, covering only the first half of the buffer and treating alternating
    // luminance and alpha values as consecutive gray pixels.
    constexpr int num_pixels = 64;

    auto      profile_data = make_gray_profile(2.2);
    ICCProfile profile{profile_data};
    REQUIRE(profile.valid());

    // A constant, distinctly non-linear luminance, and an alpha ramp that must survive untouched.
    constexpr float encoded_y = 0.5f;
    std::vector<float> pixels(num_pixels * 2);
    std::vector<float> alphas(num_pixels);
    for (int i = 0; i < num_pixels; ++i)
    {
        alphas[i]          = float(i) / float(num_pixels - 1);
        pixels[i * 2 + 0]  = encoded_y;
        pixels[i * 2 + 1]  = alphas[i];
    }

    // keep_primaries=false takes the linear_Gray() output profile directly. The keep_primaries=true path
    // first asks for the profile's chromaticities, which a gray profile need not carry.
    REQUIRE(profile.linearize_pixels(pixels.data(), int3{num_pixels, 1, 2}, /*keep_primaries*/ false));

    // Every luminance sample took the same input, so every one has to come out the same -- and different
    // from what went in, since gamma 2.2 is not the identity at 0.5.
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
    // The one-channel case shares the code path and must keep working.
    constexpr int num_pixels = 16;

    auto      profile_data = make_gray_profile(2.2);
    ICCProfile profile{profile_data};
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

#endif // HDRVIEW_ENABLE_LCMS2
