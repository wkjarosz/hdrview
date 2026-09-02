//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"

#include <string>

namespace
{

// A 1x1 RGBA image whose color channels are 1 and whose alpha is 0.5, so a premultiply is visible.
ImagePtr make_rgba_image(AlphaType_ alpha_type, bool alpha_is_transparency = true)
{
    auto img = std::make_shared<Image>(int2{1, 1}, 4);
    for (int c = 0; c < 3; ++c) img->channels[c](0, 0) = 1.f;
    img->channels[3](0, 0)      = 0.5f;
    img->alpha_type             = alpha_type;
    img->alpha_is_transparency  = alpha_is_transparency;
    img->finalize();
    return img;
}

const ChannelGroup *find_group(const ImagePtr &img, const std::string &name)
{
    for (const auto &g : img->groups)
        if (g.name == name)
            return &g;
    return nullptr;
}

} // namespace

TEST_CASE("straight alpha is premultiplied into one RGBA group by default")
{
    auto img = make_rgba_image(AlphaType_Straight);

    REQUIRE(img->groups.size() == 1);
    auto *rgba = find_group(img, "R,G,B,A");
    REQUIRE(rgba != nullptr);
    CHECK(rgba->type == ChannelGroup::RGBA_Channels);
    CHECK(rgba->num_channels == 4);

    CHECK(img->channels[0](0, 0) == doctest::Approx(0.5f));
}

TEST_CASE("alpha_is_transparency=false splits alpha off and skips the premultiply")
{
    auto img = make_rgba_image(AlphaType_Straight, /*alpha_is_transparency*/ false);

    // R,G,B group next in the table, with A left over as its own single-channel group.
    REQUIRE(img->groups.size() == 2);
    auto *rgb = find_group(img, "R,G,B");
    REQUIRE(rgb != nullptr);
    CHECK(rgb->type == ChannelGroup::RGB_Channels);
    CHECK(rgb->num_channels == 3);

    auto *a = find_group(img, "A");
    REQUIRE(a != nullptr);
    CHECK(a->type == ChannelGroup::Single_Channel);
    CHECK(a->num_channels == 1);

    // No alpha-bearing group means finalize() has nothing to premultiply.
    CHECK(img->channels[0](0, 0) == doctest::Approx(1.f));
    CHECK(img->channels[3](0, 0) == doctest::Approx(0.5f));
}

TEST_CASE("raw_pixel reports the file's values for a straight-alpha image")
{
    SUBCASE("straight alpha is divided back out")
    {
        auto img = make_rgba_image(AlphaType_Straight);

        // Stored premultiplied as 0.5; the file held 1.0.
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.5f));
        auto p = img->raw_pixel(int2{0, 0});
        CHECK(p.x == doctest::Approx(1.f));
        CHECK(p.w == doctest::Approx(0.5f)); // the alpha channel itself is untouched
    }

    SUBCASE("a premultiplied file is reported as stored")
    {
        // Its alpha=0 pixels have no straight form, so its values are what the author intended.
        auto img = make_rgba_image(AlphaType_PremultipliedLinear);

        auto p = img->raw_pixel(int2{0, 0});
        CHECK(p.x == doctest::Approx(1.f));
        CHECK(p.w == doctest::Approx(0.5f));
    }
}

TEST_CASE("alpha_is_transparency=false leaves premultiplied files untouched too")
{
    auto img = make_rgba_image(AlphaType_PremultipliedLinear, /*alpha_is_transparency*/ false);

    REQUIRE(img->groups.size() == 2);
    CHECK(find_group(img, "R,G,B") != nullptr);
    CHECK(find_group(img, "A") != nullptr);
    CHECK(img->channels[0](0, 0) == doctest::Approx(1.f));
}
