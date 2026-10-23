//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"

#include <string>

namespace
{

const ChannelGroup *find_group(const ImagePtr &img, const std::string &name)
{
    for (const auto &g : img->groups)
        if (g.name == name)
            return &g;
    return nullptr;
}

} // namespace

TEST_CASE("finalize() groups alpha by what it means, premultiplies only straight alpha, and raw_pixel() undoes it")
{
    // R, G, B and A all differ, and differ from pixel to pixel, so premultiplying by the wrong channel or by
    // another pixel's alpha shows
    const int2 size{3, 2};
    auto       stored = [](int c, int x, int y)
    { return c == 3 ? 0.3f + 0.1f * float(x + 3 * y) : 0.8f - 0.2f * float(c) + 0.01f * float(x + 3 * y); };

    for (TransparencyType_ t = TransparencyType_None; t < TransparencyType_Count; ++t)
    {
        CAPTURE(transparency_type_name(t));

        // the kind has to be set before finalize(), which is what acts on it
        auto img = std::make_shared<Image>(size, 4);
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[size_t(c)](x, y) = stored(c, x, y);
        img->transparency = t;
        img->finalize();

        // alpha that means coverage joins the color; alpha that does not stands beside it on its own
        const bool coverage = t != TransparencyType_None;
        if (coverage)
        {
            REQUIRE(img->groups.size() == 1);
            const auto *rgba = find_group(img, "R,G,B,A");
            REQUIRE(rgba != nullptr);
            CHECK(rgba->type == ChannelGroup::RGBA_Channels);
            CHECK(rgba->num_channels == 4);
        }
        else
        {
            REQUIRE(img->groups.size() == 2);
            const auto *rgb = find_group(img, "R,G,B");
            const auto *a   = find_group(img, "A");
            REQUIRE(rgb != nullptr);
            REQUIRE(a != nullptr);
            CHECK(rgb->type == ChannelGroup::RGB_Channels);
            CHECK(rgb->num_channels == 3);
            CHECK(a->type == ChannelGroup::Single_Channel);
            CHECK(a->num_channels == 1);
        }

        // the color group, which comes first either way
        img->selected_group = 0;
        REQUIRE(img->groups[0].num_channels >= 3);

        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                const float a = stored(3, x, y);

                // multiplied by its own alpha only when the file held it straight; a premultiplied file is
                // already in that form, whichever space it was multiplied in
                for (int c = 0; c < 3; ++c)
                    CHECK(img->channels[size_t(c)](x, y) ==
                          doctest::Approx(t == TransparencyType_Straight ? a * stored(c, x, y) : stored(c, x, y)));
                CHECK(img->channels[3](x, y) == a); // the alpha channel itself is untouched

                // raw_pixel() reports the file's values: straight alpha divided back out, a premultiplied
                // file's as stored, since its alpha=0 pixels have no straight form
                const float4 p = img->raw_pixel(int2{x, y});
                for (int c = 0; c < 3; ++c) CHECK(p[c] == doctest::Approx(stored(c, x, y)));
                CHECK(p.w == (coverage ? a : 0.f));
            }
    }
}
