//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "image.h"

// PixelStats::calculate() (exercised by the tests below) lazily spins up stp::ThreadPool::singleton()'s
// worker threads. If we let them be torn down via the pool's own static destructor, their exit-time
// ordering relative to other statics (e.g. spdlog's logger registry, which the workers touch on startup)
// is unspecified, and a still-shutting-down worker can end up racing the destruction of globals it
// depends on. Stopping the pool explicitly here, before any static destructors run, sidesteps that
// entirely.
int main(int argc, char **argv)
{
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    int res = ctx.run();

    // try_singleton() (unlike singleton()) never creates the pool as a side effect, so tests that never
    // touched PixelStats::calculate() (and thus never spun up any worker threads) don't pay for spinning
    // up a pool here just to immediately tear it down.
    if (auto *pool = stp::ThreadPool::try_singleton())
        pool->stop();

    return res;
}

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

TEST_CASE("PixelStats::calculate counts NaN/Inf pixels separately and excludes them from min/max/average")
{
    // 4x4 channel, values 0..15 (x + 4*y), except three pixels replaced with NaN/+Inf/-Inf. Pixels (0,0)=0 and
    // (3,3)=15 are left untouched, so min/max staying exactly 0/15 confirms NaN/Inf didn't corrupt them.
    Channel img("test", int2{4, 4});
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) img(x, y) = float(x + 4 * y);
    img(1, 1) = std::numeric_limits<float>::quiet_NaN();     // value 5
    img(2, 2) = std::numeric_limits<float>::infinity();      // value 10
    img(3, 0) = -std::numeric_limits<float>::infinity();     // value 3

    PixelStats::Settings settings; // whole image

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, int2{0, 0}, nullptr, int2{0, 0}, settings, canceled);

    CHECK(stats.summary.nan_pixels == 1);
    CHECK(stats.summary.inf_pixels == 2);
    CHECK(stats.summary.valid_pixels == 13); // 16 - 1 NaN - 2 Inf
    CHECK(stats.summary.minimum == doctest::Approx(0.f));
    CHECK(stats.summary.maximum == doctest::Approx(15.f));
}

TEST_CASE("PixelStats::calculate applies each blend mode against a reference image")
{
    // Both channels are constant-valued, so every pixel's blended result is identical and known exactly.
    Channel img("img", int2{2, 2});
    Channel ref("ref", int2{2, 2});
    img.apply([](float, int, int) { return 10.f; });
    ref.apply([](float, int, int) { return 4.f; });

    auto blended_value = [&](BlendMode_ mode)
    {
        PixelStats::Settings settings;
        settings.blend_mode = mode;

        PixelStats        stats;
        std::atomic<bool> canceled{false};
        stats.calculate(img, int2{0, 0}, &ref, int2{0, 0}, settings, canceled);

        CHECK(stats.summary.valid_pixels == 4);
        // constant inputs -> every blended pixel is identical -> zero spread
        CHECK(stats.summary.minimum == doctest::Approx(stats.summary.maximum));
        CHECK(stats.summary.stddev == doctest::Approx(0.0));
        return stats.summary.average;
    };

    CHECK(blended_value(BlendMode_Add) == doctest::Approx(14.0));
    CHECK(blended_value(BlendMode_Multiply) == doctest::Approx(40.0));
    CHECK(blended_value(BlendMode_Subtract) == doctest::Approx(6.0));
    CHECK(blended_value(BlendMode_Divide) == doctest::Approx(2.5));
    CHECK(blended_value(BlendMode_Average) == doctest::Approx(7.0));
    CHECK(blended_value(BlendMode_Difference) == doctest::Approx(6.0));
}

TEST_CASE("PixelStats::calculate tolerates a reference channel smaller than the image under BlendMode_Normal")
{
    // BlendMode_Normal ignores the reference sample entirely (blend() just returns the image's own value), but
    // a reference image/channel of a different size than the current one is a completely ordinary thing to
    // select (e.g. comparing two unrelated photos) -- it must not crash just because Normal is the active mode.
    auto img = make_identifiable_channel(10, 10);
    auto ref = make_identifiable_channel(2, 2);

    PixelStats::Settings settings; // BlendMode_Normal by default, whole image

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, int2{0, 0}, &ref, int2{0, 0}, settings, canceled);

    CHECK(stats.summary.valid_pixels == 100);
    CHECK(stats.summary.minimum == doctest::Approx(0.f));
    CHECK(stats.summary.maximum == doctest::Approx(99.f));
}

TEST_CASE("PixelStats::calculate resets to the default, uncomputed state when canceled")
{
    auto img = make_identifiable_channel(4, 4);

    PixelStats::Settings settings;

    PixelStats        stats;
    std::atomic<bool> canceled{true}; // already canceled before calculate() even starts
    stats.calculate(img, int2{0, 0}, nullptr, int2{0, 0}, settings, canceled);

    CHECK_FALSE(stats.computed);
    CHECK(stats.summary.valid_pixels == 0);
    CHECK(stats.summary.minimum == std::numeric_limits<float>::infinity());
    CHECK(stats.summary.maximum == -std::numeric_limits<float>::infinity());
}
