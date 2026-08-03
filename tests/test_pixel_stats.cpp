//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "image.h"

namespace
{

// Fills a WxH channel with a value that uniquely identifies each pixel, so any mixup between which region of the
// channel was actually sampled is immediately visible in the resulting statistics.
Channel make_identifiable_channel(int w, int h)
{
    Channel c("test", int2{w, h});
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) c(x, y) = float(x + 10 * y);
    return c;
}

} // namespace

TEST_CASE("PixelStats::calculate uses the selected sub-region, not the image origin")
{
    // 10x10 channel; the selection below covers pixels (3,3),(4,3),(3,4),(4,4) -> values 33,34,43,44
    auto img = make_identifiable_channel(10, 10);

    PixelStats::Settings settings;
    settings.roi = Box2i{int2{3, 3}, int2{5, 5}};

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, int2{0, 0}, nullptr, int2{0, 0}, settings, canceled);

    CHECK(stats.summary.valid_pixels == 4);
    CHECK(stats.summary.minimum == doctest::Approx(33.f));
    CHECK(stats.summary.maximum == doctest::Approx(44.f));
    CHECK(stats.summary.average == doctest::Approx(38.5));

    // Before the fix, this would incorrectly report the image's own top-left corner (values 0,1,10,11:
    // min=0, max=11, avg=5.5) regardless of where the selection actually was.
    CHECK(stats.summary.minimum != doctest::Approx(0.f));
    CHECK(stats.summary.maximum != doctest::Approx(11.f));
}

TEST_CASE("PixelStats::calculate covers the whole image when no selection is active")
{
    auto img = make_identifiable_channel(4, 4);

    PixelStats::Settings settings; // default-constructed roi has no volume -> whole image

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, int2{0, 0}, nullptr, int2{0, 0}, settings, canceled);

    // values range from 0 (0,0) to 33 (3,3) across all 16 pixels
    CHECK(stats.summary.valid_pixels == 16);
    CHECK(stats.summary.minimum == doctest::Approx(0.f));
    CHECK(stats.summary.maximum == doctest::Approx(33.f));
}

TEST_CASE("PixelStats::calculate offsets a non-zero image data origin correctly")
{
    // Simulates an image (e.g. an EXR) whose data window doesn't start at (0,0): the channel's own local array is
    // still 0-indexed, but img_data_origin records where that local origin sits in the shared/global coordinate
    // space that the selection ROI is expressed in.
    auto img = make_identifiable_channel(10, 10);

    PixelStats::Settings settings;
    // In global coordinates, with img_data_origin={5,5}, this selects local pixels (3,3)-(4,4) again: 33,34,43,44
    settings.roi = Box2i{int2{8, 8}, int2{10, 10}};

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, int2{5, 5}, nullptr, int2{0, 0}, settings, canceled);

    CHECK(stats.summary.valid_pixels == 4);
    CHECK(stats.summary.minimum == doctest::Approx(33.f));
    CHECK(stats.summary.maximum == doctest::Approx(44.f));
}
