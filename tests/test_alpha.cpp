//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/alpha.h"
#include "imageio/exr.h"
#include "imageio/image_loader.h"
#include "imageio/png.h"

#include <sstream>

#include <vector>

namespace
{

// One interleaved RGBA pixel, so a scale by alpha is visible in a single value.
std::vector<float> pixel(float r, float g, float b, float a) { return {r, g, b, a}; }

// Channels by name: OpenEXR stores them alphabetically, so position says nothing about which is which.
float channel_value(const ImagePtr &img, const std::string &name)
{
    for (const auto &c : img->channels)
        if (Channel::tail(c.name) == name)
            return c(0, 0);
    FAIL("no channel named ", name);
    return 0.f;
}

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

TEST_CASE("set_alpha records what the file said, and what an override replaced it with")
{
    auto img = std::make_shared<Image>(int2{1, 1}, 4);

    // No override: the effective kind is the file's, and the file's is still there to compare against.
    for (AlphaType_ at :
         {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
    {
        img->set_alpha(at, AlphaSource_File, std::nullopt);
        CHECK(img->alpha_type == at);
        CHECK(img->alpha_type_from_file == at);
        CHECK(img->alpha_source == AlphaSource_File);
    }

    // Overridden: the effective kind changes, what the file said does not, and neither does how it said it.
    for (AlphaType_ forced :
         {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
        for (AlphaType_ from_file :
             {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
        {
            img->set_alpha(from_file, AlphaSource_Assumed, forced);
            CHECK(img->alpha_type == forced);
            CHECK(img->alpha_type_from_file == from_file);
            CHECK(img->alpha_source == AlphaSource_Assumed);
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

// The override exists for files that contradict their own format's convention, which is the case no
// per-format tagging can catch. These two are the ones that motivated it.

TEST_CASE("An associated-alpha PNG can be read as premultiplied, against the PNG spec")
{
    // PNG is unassociated by spec, so HDRView premultiplies on load -- doing that to a file someone
    // wrote premultiplied would multiply twice and darken every semi-transparent pixel.
    // The PNG writer divides alpha out before encoding, so to land a file whose stored samples are
    // a*OETF(C) -- half coverage over a fully bright color, encoded as 128 -- the linear color going in
    // has to be a * EOTF(0.5).
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 0.5f * to_linear(0.5f, TransferFunction::sRGB);
    img->channels[3](0, 0) = 0.5f;
    img->finalize();

    std::ostringstream out(std::ios::binary);
    save_png_image(*img, out, "premul.png", /*gain*/ 1.f, /*dither*/ false, /*interlaced*/ false,
                   /*sixteen_bit*/ false, TransferFunction::sRGB);

    // Read as the spec says: the samples are taken as straight and multiplied by alpha again.
    {
        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "premul.png", ImageLoadOptions{});
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_type == AlphaType_Straight);
        // Taken as straight: EOTF(0.5) linearized, then multiplied by coverage a second time.
        CHECK(channel_value(images[0], "R") ==
              doctest::Approx(0.5f * to_linear(0.5f, TransferFunction::sRGB)).epsilon(0.05));
    }

    // Read as what it is: divided out across the transfer, then restored, ending where it started.
    {
        ImageLoadOptions opts;
        opts.override_alpha = true;
        opts.alpha_override = AlphaType_PremultipliedNonLinear;

        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "premul.png", opts);
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_type == AlphaType_PremultipliedNonLinear);
        // Divided out across the transfer: the bright color the file really encodes, times its coverage.
        CHECK(channel_value(images[0], "R") == doctest::Approx(0.5f).epsilon(0.05));
    }
}

TEST_CASE("A straight-alpha EXR can be read as straight, against the EXR spec")
{
    // EXR is associated by spec, so HDRView leaves its samples alone. A file written straight needs
    // the multiply that assumption skips.
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 1.f; // straight, linear
    img->channels[3](0, 0) = 0.5f;
    img->finalize();

    std::ostringstream out(std::ios::binary);
    save_exr_image(*img, out, "straight.exr");

    {
        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "straight.exr", ImageLoadOptions{});
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_type == AlphaType_PremultipliedLinear);
        CHECK(channel_value(images[0], "R") == doctest::Approx(1.f).epsilon(0.01));
    }

    {
        ImageLoadOptions opts;
        opts.override_alpha = true;
        opts.alpha_override = AlphaType_Straight;

        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "straight.exr", opts);
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_type == AlphaType_Straight);
        // finalize() now multiplies by the coverage the file's samples were never scaled by.
        CHECK(channel_value(images[0], "R") == doctest::Approx(0.5f).epsilon(0.01));
    }
}

TEST_CASE("A format that settles alpha for every conforming file says so")
{
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 0.5f;
    img->channels[3](0, 0) = 0.5f;
    img->finalize();

    SUBCASE("PNG is unassociated by spec, with nothing per-file to read")
    {
        std::ostringstream out(std::ios::binary);
        save_png_image(*img, out, "a.png", 1.f, false, false, false, TransferFunction::sRGB);
        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "a.png", ImageLoadOptions{});
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_source == AlphaSource_Format);
        CHECK(images[0]->alpha_type_from_file == AlphaType_Straight);
    }

    SUBCASE("EXR is associated by spec")
    {
        std::ostringstream out(std::ios::binary);
        save_exr_image(*img, out, "a.exr");
        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "a.exr", ImageLoadOptions{});
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_source == AlphaSource_Format);
        CHECK(images[0]->alpha_type_from_file == AlphaType_PremultipliedLinear);
    }
}

TEST_CASE("An image assembled in memory reports its alpha as assumed")
{
    // Nothing read it from anywhere -- the samples are simply already in HDRView's working form, which is
    // what the IPC path relies on for tiles that arrive before the alpha channel they would be scaled by.
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    CHECK(img->alpha_type == AlphaType_PremultipliedLinear);
    CHECK(img->alpha_source == AlphaSource_Assumed);

    // And one with no alpha channel says None, still without claiming anyone stated it.
    auto rgb = std::make_shared<Image>(int2{1, 1}, 3);
    CHECK(rgb->alpha_type == AlphaType_None);
    CHECK(rgb->alpha_source == AlphaSource_Assumed);
}
