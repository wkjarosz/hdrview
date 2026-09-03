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
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/tiff.h"

#include <functional>
#include <sstream>
#include <vector>

#include <vector>

namespace
{

// one interleaved RGBA pixel
std::vector<float> pixel(float r, float g, float b, float a) { return {r, g, b, a}; }

// by name, since OpenEXR stores channels alphabetically
float channel_value(const ImagePtr &img, const std::string &name)
{
    for (const auto &c : img->channels)
        if (Channel::tail(c.name) == name)
            return c(0, 0);
    FAIL("no channel named ", name);
    return 0.f;
}

// the two conventions a premultiplied file can hold, for straight color `c` under coverage `a`
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

    // what such a file holds: the encoded color, multiplied by alpha
    auto px = pixel(post_transfer(straight, a), post_transfer(straight, a), post_transfer(straight, a), a);

    unpremultiply_before_transfer(px.data(), int3{1, 1, 4}, AlphaType_PremultipliedNonLinear);
    // dividing by alpha recovers the encoded straight color the inverse transfer expects
    CHECK(px[0] == doctest::Approx(from_linear(straight, TransferFunction::sRGB)));

    for (int c = 0; c < 3; ++c) px[c] = to_linear(px[c], TransferFunction::sRGB);

    repremultiply_after_transfer(px.data(), int3{1, 1, 4}, AlphaType_PremultipliedNonLinear);
    // ending in HDRView's working form: linear color, multiplied by alpha
    CHECK(px[0] == doctest::Approx(a * straight));
    CHECK(px[3] == doctest::Approx(a)); // alpha itself is never scaled
}

TEST_CASE("The two premultiplied conventions disagree, and reading one as the other is visibly wrong")
{
    const float a = 0.5f, straight = 1.f;

    // the discriminating case: full-intensity color under half coverage
    CHECK(post_transfer(straight, a) == doctest::Approx(0.5f));
    CHECK(linear_premult(straight, a) == doctest::Approx(0.7354f).epsilon(0.001));

    // reading a post-transfer file as linear-premultiplied applies the inverse transfer to the multiplied value
    const float misread = to_linear(post_transfer(straight, a), TransferFunction::sRGB);
    CHECK(misread == doctest::Approx(0.2140f).epsilon(0.001));
    CHECK(misread != doctest::Approx(a * straight)); // the correct answer is 0.5
}

TEST_CASE("A transfer function that is the identity makes the two premultiplied kinds coincide")
{
    // formats storing linear samples need no special case: the pair cancels arithmetically
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
    // restoring multiplies by the alpha that is there, so a transparent pixel ends at zero either way
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

    // no override: the effective kind is the file's
    for (AlphaType_ at :
         {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
    {
        img->set_alpha(at, std::nullopt);
        CHECK(img->alpha_type == at);
        CHECK(img->alpha_type_from_file == at);
        CHECK_FALSE(img->alpha_assumed);
    }

    // overridden: the effective kind changes, what the file said does not
    for (AlphaType_ forced :
         {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
        for (AlphaType_ from_file :
             {AlphaType_None, AlphaType_Straight, AlphaType_PremultipliedLinear, AlphaType_PremultipliedNonLinear})
        {
            img->set_alpha(from_file, forced, true);
            CHECK(img->alpha_type == forced);
            CHECK(img->alpha_type_from_file == from_file);
            CHECK(img->alpha_assumed);
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
    // already multiplied, whichever space it happened in
    CHECK(make(AlphaType_PremultipliedLinear)->channels[0](0, 0) == doctest::Approx(1.f));
    CHECK(make(AlphaType_PremultipliedNonLinear)->channels[0](0, 0) == doctest::Approx(1.f));
}

// The override exists for files that contradict their own format's convention.

TEST_CASE("An associated-alpha PNG can be read as premultiplied, against the PNG spec")
{
    // PNG is unassociated by spec, so HDRView premultiplies on load. The writer divides alpha out before
    // encoding, so landing stored samples of a*OETF(C), half coverage over a fully bright color encoded as
    // 128, needs a linear color of a * EOTF(0.5) going in.
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 0.5f * to_linear(0.5f, TransferFunction::sRGB);
    img->channels[3](0, 0) = 0.5f;
    img->finalize();

    std::ostringstream out(std::ios::binary);
    save_png_image(*img, out, "premul.png", /*gain*/ 1.f, /*dither*/ false, /*interlaced*/ false,
                   /*sixteen_bit*/ false, TransferFunction::sRGB);

    // read as the spec says: the samples are taken as straight and multiplied by alpha again
    {
        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "premul.png", ImageLoadOptions{});
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_type == AlphaType_Straight);
        // taken as straight: EOTF(0.5) linearized, then multiplied by coverage a second time
        CHECK(channel_value(images[0], "R") ==
              doctest::Approx(0.5f * to_linear(0.5f, TransferFunction::sRGB)).epsilon(0.05));
    }

    // read as what it is: divided out across the transfer, then restored, ending where it started
    {
        ImageLoadOptions opts;
        opts.override_alpha = true;
        opts.alpha_override = AlphaType_PremultipliedNonLinear;

        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, "premul.png", opts);
        REQUIRE(images.size() == 1);
        CHECK(images[0]->alpha_type == AlphaType_PremultipliedNonLinear);
        // the bright color the file encodes, times its coverage
        CHECK(channel_value(images[0], "R") == doctest::Approx(0.5f).epsilon(0.05));
    }
}

TEST_CASE("A straight-alpha EXR can be read as straight, against the EXR spec")
{
    // EXR is associated by spec, so HDRView leaves its samples alone; a file written straight needs the
    // multiply that assumption skips
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
        // finalize() now multiplies by the coverage the file's samples were never scaled by
        CHECK(channel_value(images[0], "R") == doctest::Approx(0.5f).epsilon(0.01));
    }
}

TEST_CASE("Every loader reports the alpha kind its format settles")
{
    // Each of these formats states the kind for every conforming file, so no loader here may mark it
    // assumed. A loader claiming the wrong kind is invisible in the pixels.
    struct Case
    {
        const char                                        *name;
        std::function<void(const Image &, std::ostream &)> save;
        AlphaType_                                         from_file;
    };

    const std::vector<Case> cases = {
        {"a.png", [](const Image &i, std::ostream &o)
         { save_png_image(i, o, "a.png", 1.f, false, false, false, TransferFunction::sRGB); }, AlphaType_Straight},
        {"a.exr", [](const Image &i, std::ostream &o) { save_exr_image(i, o, "a.exr"); },
         AlphaType_PremultipliedLinear},
        {"a.qoi", [](const Image &i, std::ostream &o) { save_qoi_image(i, o, "a.qoi", 1.f, true, false); },
         AlphaType_Straight},
        {"a.tga", [](const Image &i, std::ostream &o)
         { save_stb_tga(i, o, "a.tga", 1.f, TransferFunction::sRGB, false); }, AlphaType_Straight},
#if HDRVIEW_ENABLE_LIBTIFF
        // written with an EXTRASAMPLES tag, so the file itself carries the signal
        {"a.tif", [](const Image &i, std::ostream &o)
         { save_tiff_image(i, o, "a.tif", 1.f, TransferFunction::sRGB, 0, 0); }, AlphaType_PremultipliedNonLinear},
#endif
    };

    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 0.25f;
    img->channels[3](0, 0) = 0.5f;
    img->finalize();

    for (const auto &c : cases)
    {
        CAPTURE(c.name);
        std::ostringstream out(std::ios::binary);
        c.save(*img, out);
        REQUIRE(out.str().size() > 8);

        std::istringstream in(out.str(), std::ios::binary);
        auto               images = load_image(in, c.name, ImageLoadOptions{});
        REQUIRE(images.size() == 1);

        CHECK_FALSE(images[0]->alpha_assumed);
        CHECK(images[0]->alpha_type_from_file == c.from_file);
        // with nothing overridden the effective kind is the file's, and no override is recorded
        CHECK(images[0]->alpha_type == c.from_file);
        CHECK_FALSE(images[0]->alpha_override.has_value());
    }
}

TEST_CASE("An image assembled in memory reports its alpha as assumed")
{
    // Nothing read it from anywhere: the samples are already in HDRView's working form. The IPC path relies
    // on that for tiles arriving before the alpha channel they would be scaled by.
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    CHECK(img->alpha_type == AlphaType_PremultipliedLinear);
    CHECK(img->alpha_assumed);

    // and one with no alpha channel says None, still without claiming anyone stated it
    auto rgb = std::make_shared<Image>(int2{1, 1}, 3);
    CHECK(rgb->alpha_type == AlphaType_None);
    CHECK(rgb->alpha_assumed);
}
