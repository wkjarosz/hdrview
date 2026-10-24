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

#include "test_support.h"

#include <functional>
#include <sstream>
#include <vector>

using namespace hdrview_test;

namespace
{

/// A 1x1 RGBA image: `color` in each of R, G and B, `alpha` in A.
ImagePtr rgba_pixel_image(float color, float alpha)
{
    return test_image(int2{1, 1}, 4, [=](int c, int, int) { return c < 3 ? color : alpha; });
}

/// The sample of the 1x1 image's channel `name`, found by name since OpenEXR stores channels alphabetically.
float channel_value(const ImagePtr &img, const std::string &name)
{
    for (const auto &c : img->channels)
        if (Channel::tail(c.name) == name)
            return c(0, 0);
    FAIL("no channel named ", name);
    return 0.f;
}

} // namespace

TEST_CASE("The alpha pair divides out and restores post-transfer premultiplication, and nothing else")
{
    // A 3x2 buffer whose every sample differs, so a pixel scaled by its neighbor's alpha, or a stride that
    // is off by one, shows. Fully transparent pixels in both rows, whose color is kept rather than divided
    // by zero.
    const int2  size{3, 2};
    const float alphas[] = {0.5f, 0.f, 0.8f, 0.25f, 1.f, 0.f};

    for (TransparencyType_ t = TransparencyType_None; t < TransparencyType_Count; ++t)
        for (int n = 1; n <= 4; ++n)
        {
            CAPTURE(transparency_type_name(t));
            CAPTURE(n);

            // alpha is last, so with one channel there is none, and with any more the last one is it
            const bool divides = t == TransparencyType_PremultipliedNonLinear && n > 1;

            std::vector<float> stored(size_t(size.x * size.y * n));
            for (int i = 0; i < size.x * size.y; ++i)
                for (int c = 0; c < n; ++c)
                    stored[size_t(i * n + c)] = c == n - 1 && n > 1 ? alphas[i] : 0.1f + 0.05f * float(i * 4 + c);

            auto px = stored;
            unpremultiply_before_transfer(px.data(), int3{size, n}, t);

            // dividing by alpha recovers the encoded straight color the inverse transfer expects; the
            // linearization in between is what the pair exists for
            auto linear = px;
            for (int i = 0; i < size.x * size.y; ++i)
                for (int c = 0; c < n - 1; ++c)
                    linear[size_t(i * n + c)] = to_linear(linear[size_t(i * n + c)], TransferFunction::sRGB);
            repremultiply_after_transfer(linear.data(), int3{size, n}, t);

            for (int i = 0; i < size.x * size.y; ++i)
                for (int c = 0; c < n; ++c)
                {
                    CAPTURE(i);
                    CAPTURE(c);
                    const size_t k     = size_t(i * n + c);
                    const bool   color = c < n - 1;
                    const float  a     = alphas[i];

                    // alpha itself is never scaled, nor is anything when there is no division to make
                    if (!divides || !color)
                    {
                        CHECK(px[k] == stored[k]);
                        CHECK(linear[k] == (color ? to_linear(stored[k], TransferFunction::sRGB) : stored[k]));
                        continue;
                    }

                    // a fully transparent pixel carries no color to recover, so it is left as it is
                    const float straight = a == 0.f ? stored[k] : stored[k] / a;
                    CHECK(px[k] == doctest::Approx(straight).epsilon(1e-6));

                    // ending in HDRView's working form, linear color multiplied by alpha, which is zero for a
                    // transparent pixel either way
                    CHECK(linear[k] == doctest::Approx(a * to_linear(straight, TransferFunction::sRGB)).epsilon(1e-6));
                }
        }
}

// The override exists for files that contradict their own format's convention.

TEST_CASE("An associated-alpha PNG can be read as premultiplied, against the PNG spec")
{
    // PNG is unassociated by spec, so HDRView premultiplies on load. The writer divides alpha out before
    // encoding, so landing stored samples of a*OETF(C), half coverage over a fully bright color encoded as
    // 128, needs a linear color of a * EOTF(0.5) going in.
    auto img = rgba_pixel_image(0.5f * to_linear(0.5f, TransferFunction::sRGB), 0.5f);

    std::ostringstream out(std::ios::binary);
    save_png_image(*img, out, "premul.png", /*dither*/ false, /*interlaced*/ false,
                   /*sixteen_bit*/ false, TransferFunction::sRGB);

    // read as the spec says, the samples are taken as straight: EOTF(0.5), multiplied by coverage a second time
    {
        auto image = load_bytes(out.str(), "premul.png");
        REQUIRE(image);
        CHECK(image->transparency == TransparencyType_Straight);
        CHECK(channel_value(image, "R") ==
              doctest::Approx(0.5f * to_linear(0.5f, TransferFunction::sRGB)).epsilon(0.05));
    }

    // read as what it is: divided out across the transfer, then restored, ending where it started
    {
        ImageLoadOptions opts;
        opts.override_transparency = true;
        opts.transparency_override = TransparencyType_PremultipliedNonLinear;

        auto image = load_bytes(out.str(), "premul.png", opts);
        REQUIRE(image);
        CHECK(image->transparency == TransparencyType_PremultipliedNonLinear);
        // the bright color the file encodes, times its coverage
        CHECK(channel_value(image, "R") == doctest::Approx(0.5f).epsilon(0.05));
    }
}

TEST_CASE("A straight-alpha EXR can be read as straight, against the EXR spec")
{
    // EXR is associated by spec, so HDRView leaves its samples alone; a file written straight needs the
    // multiply that assumption skips
    auto img = rgba_pixel_image(1.f, 0.5f); // straight, linear

    std::ostringstream out(std::ios::binary);
    save_exr_image(*img, out, "straight.exr");

    {
        auto image = load_bytes(out.str(), "straight.exr");
        REQUIRE(image);
        CHECK(image->transparency == TransparencyType_PremultipliedLinear);
        CHECK(channel_value(image, "R") == doctest::Approx(1.f).epsilon(0.01));
    }

    {
        ImageLoadOptions opts;
        opts.override_transparency = true;
        opts.transparency_override = TransparencyType_Straight;

        auto image = load_bytes(out.str(), "straight.exr", opts);
        REQUIRE(image);
        CHECK(image->transparency == TransparencyType_Straight);
        // finalize() multiplies by the coverage the file's samples were never scaled by
        CHECK(channel_value(image, "R") == doctest::Approx(0.5f).epsilon(0.01));
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
        TransparencyType_                                  from_file;
    };

    const std::vector<Case> cases = {
        {"a.png", [](const Image &i, std::ostream &o)
         { save_png_image(i, o, "a.png", false, false, false, TransferFunction::sRGB); }, TransparencyType_Straight},
        {"a.exr", [](const Image &i, std::ostream &o) { save_exr_image(i, o, "a.exr"); },
         TransparencyType_PremultipliedLinear},
        {"a.qoi", [](const Image &i, std::ostream &o) { save_qoi_image(i, o, "a.qoi", true, false); },
         TransparencyType_Straight},
        {"a.tga", [](const Image &i, std::ostream &o) { save_stb_tga(i, o, "a.tga", false); },
         TransparencyType_Straight},
#if HDRVIEW_ENABLE_LIBTIFF
        // written with an EXTRASAMPLES tag, so the file itself carries the signal
        {"a.tif", [](const Image &i, std::ostream &o) { save_tiff_image(i, o, "a.tif", TransferFunction::sRGB, 0, 0); },
         TransparencyType_PremultipliedNonLinear},
#endif
    };

    auto img = rgba_pixel_image(0.25f, 0.5f);

    for (const auto &c : cases)
    {
        CAPTURE(c.name);
        std::ostringstream out(std::ios::binary);
        c.save(*img, out);
        REQUIRE(out.str().size() > 8);

        auto image = load_bytes(out.str(), c.name);
        REQUIRE(image);

        CHECK_FALSE(image->transparency_assumed);
        CHECK(image->transparency_from_file == c.from_file);
        // with nothing overridden the effective kind is the file's, and no override is recorded
        CHECK(image->transparency == c.from_file);
        CHECK_FALSE(image->transparency_override.has_value());
    }
}

TEST_CASE("An image assembled in memory reports its alpha as assumed")
{
    // Nothing read it from anywhere: the samples are already in HDRView's working form. The IPC path relies
    // on that for tiles arriving before the alpha channel they would be scaled by.
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    CHECK(img->transparency == TransparencyType_PremultipliedLinear);
    CHECK(img->transparency_assumed);

    // and one with no alpha channel says None, still without claiming anyone stated it
    auto rgb = std::make_shared<Image>(int2{1, 1}, 3);
    CHECK(rgb->transparency == TransparencyType_None);
    CHECK(rgb->transparency_assumed);
}
