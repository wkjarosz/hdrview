//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/alpha.h"
#include "imageio/image_loader.h"

#include <vector>

namespace
{

// One interleaved RGBA pixel, so a scale by alpha is visible in a single value.
std::vector<float> pixel(float r, float g, float b, float a) { return {r, g, b, a}; }

// The two conventions a premultiplied file can hold, for straight color `c` under coverage `a`.
float post_transfer(float c, float a) { return a * from_linear(c, TransferFunction::sRGB); }
float linear_premult(float c, float a) { return from_linear(a * c, TransferFunction::sRGB); }

} // namespace

TEST_CASE("The alpha pair is a no-op for every kind but post-transfer premultiplication")
{
    for (AlphaType_ at : {AlphaType_None, AlphaType_PremultipliedLinear, AlphaType_Straight})
    {
        CAPTURE(alpha_type_name(at));
        auto px = pixel(0.25f, 0.5f, 0.75f, 0.5f);

        unpremultiply_before_transfer(px.data(), int3{1, 1, 4}, at);
        CHECK(px[0] == doctest::Approx(0.25f));
        CHECK(px[3] == doctest::Approx(0.5f));

        repremultiply_after_transfer(px.data(), int3{1, 1, 4}, at);
        CHECK(px[0] == doctest::Approx(0.25f));
    }
}

TEST_CASE("Post-transfer premultiplication is divided out and restored around a transfer")
{
    const float a = 0.5f, straight = 0.8f;

    // What such a file holds: the encoded color, multiplied by alpha.
    auto px = pixel(post_transfer(straight, a), post_transfer(straight, a), post_transfer(straight, a), a);

    unpremultiply_before_transfer(px.data(), int3{1, 1, 4}, AlphaType_PremultipliedNonLinear);
    // Dividing by alpha recovers the encoded straight color, which is what the inverse transfer expects.
    CHECK(px[0] == doctest::Approx(from_linear(straight, TransferFunction::sRGB)));

    for (int c = 0; c < 3; ++c) px[c] = to_linear(px[c], TransferFunction::sRGB);

    repremultiply_after_transfer(px.data(), int3{1, 1, 4}, AlphaType_PremultipliedNonLinear);
    // Ending in HDRView's working form: linear color, multiplied by alpha.
    CHECK(px[0] == doctest::Approx(a * straight));
    CHECK(px[3] == doctest::Approx(a)); // alpha itself is never scaled
}

TEST_CASE("The two premultiplied conventions disagree, and reading one as the other is visibly wrong")
{
    const float a = 0.5f, straight = 1.f;

    // The discriminating case from real files: full-intensity color under half coverage.
    CHECK(post_transfer(straight, a) == doctest::Approx(0.5f));
    CHECK(linear_premult(straight, a) == doctest::Approx(0.7354f).epsilon(0.001));

    // Reading a post-transfer file as though it were linear-premultiplied applies the inverse transfer
    // to the multiplied value, which is the error this whole distinction exists to prevent.
    const float misread = to_linear(post_transfer(straight, a), TransferFunction::sRGB);
    CHECK(misread == doctest::Approx(0.2140f).epsilon(0.001));
    CHECK(misread != doctest::Approx(a * straight)); // the correct answer is 0.5
}

TEST_CASE("A transfer function that is the identity makes the two premultiplied kinds coincide")
{
    // Formats storing linear samples need no special case: the pair cancels arithmetically.
    const float a = 0.5f, straight = 0.8f;
    CHECK(from_linear(a * straight, TransferFunction::Linear) ==
          doctest::Approx(a * from_linear(straight, TransferFunction::Linear)));
}

TEST_CASE("A fully transparent pixel keeps its color rather than dividing by zero")
{
    auto px = pixel(0.25f, 0.5f, 0.75f, 0.f);

    unpremultiply_before_transfer(px.data(), int3{1, 1, 4}, AlphaType_PremultipliedNonLinear);
    CHECK(px[0] == doctest::Approx(0.25f));
    CHECK(std::isfinite(px[0]));

    repremultiply_after_transfer(px.data(), int3{1, 1, 4}, AlphaType_PremultipliedNonLinear);
    // Restoring multiplies by the alpha that is there, so a transparent pixel ends at zero either way.
    CHECK(px[0] == doctest::Approx(0.f));
}

TEST_CASE("A two-channel gray+alpha buffer is handled like RGBA")
{
    const float        a = 0.5f, straight = 0.8f;
    std::vector<float> px{post_transfer(straight, a), a};

    unpremultiply_before_transfer(px.data(), int3{1, 1, 2}, AlphaType_PremultipliedNonLinear);
    CHECK(px[0] == doctest::Approx(from_linear(straight, TransferFunction::sRGB)));
    CHECK(px[1] == doctest::Approx(a));
}

TEST_CASE("A single-channel buffer has no alpha to divide by and is left alone")
{
    std::vector<float> px{0.25f};
    unpremultiply_before_transfer(px.data(), int3{1, 1, 1}, AlphaType_PremultipliedNonLinear);
    CHECK(px[0] == doctest::Approx(0.25f));
}

TEST_CASE("effective_alpha_type reports the file's kind until the options override it")
{
    ImageLoadOptions opts;
    REQUIRE_FALSE(opts.override_alpha);

    // Off: whatever the loader concluded stands, for every kind.
    for (AlphaType_ at :
         {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
        CHECK(effective_alpha_type(opts, at) == at);

    // On: the option replaces it, including asserting alpha over a file that declared none and
    // declaring data over a file that claimed transparency.
    opts.override_alpha = true;
    for (AlphaType_ forced :
         {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
    {
        opts.alpha_override = forced;
        for (AlphaType_ from_file :
             {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
            CHECK(effective_alpha_type(opts, from_file) == forced);
    }
}

TEST_CASE("finalize() premultiplies straight alpha and leaves the other kinds alone")
{
    auto make = [](AlphaType_ at)
    {
        auto img = std::make_shared<Image>(int2{1, 1}, 4);
        for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 1.f;
        img->channels[3](0, 0) = 0.5f;
        img->alpha_type        = at;
        img->finalize();
        return img;
    };

    CHECK(make(AlphaType_Straight)->channels[0](0, 0) == doctest::Approx(0.5f));
    // Already multiplied, whichever space it happened in: finalize() would be doing it a second time.
    CHECK(make(AlphaType_PremultipliedLinear)->channels[0](0, 0) == doctest::Approx(1.f));
    CHECK(make(AlphaType_PremultipliedNonLinear)->channels[0](0, 0) == doctest::Approx(1.f));
}
