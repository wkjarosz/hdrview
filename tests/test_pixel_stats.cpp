//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "image.h"

#include <algorithm>

// PixelStats::calculate() lazily spins up stp::ThreadPool::singleton()'s workers. Their static destructor's
// order against other statics the workers touch (spdlog's logger registry) is unspecified, so the pool is
// stopped here, before any static destructor runs.
int main(int argc, char **argv)
{
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    int res = ctx.run();

    // try_singleton(), unlike singleton(), never creates the pool as a side effect
    if (auto *pool = stp::ThreadPool::try_singleton())
        pool->stop();

    return res;
}

namespace
{

// A WxH channel whose value uniquely identifies each pixel, so which region was sampled is visible in the
// statistics.
Channel make_identifiable_channel(int w, int h)
{
    Channel c("test", int2{w, h});
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) c(x, y) = float(x + 10 * y);
    return c;
}

// A 256x1 channel holding every 8-bit sRGB code, decoded to linear the way the PNG and JPEG loaders do.
Channel make_8bit_sRGB_ramp()
{
    Channel c("ramp", int2{256, 1});
    c.bits_per_sample = 8;
    for (int k = 0; k < 256; ++k) c(k, 0) = sRGB_to_linear(k / 255.f);
    return c;
}

// Number of bins holding no samples at all, which is the "comb" artifact in the data.
int count_empty_bins(const PixelStats &stats, AxisScale x_scale)
{
    int empty = 0;
    for (int i = 0; i < stats.num_bins; ++i)
        if (stats.hist_ys[x_scale][i] == 0.f)
            ++empty;
    return empty;
}

double total_bin_count(const PixelStats &stats, AxisScale x_scale)
{
    double sum = 0.0;
    for (int i = 0; i < stats.num_bins; ++i) sum += stats.hist_ys[x_scale][i];
    return sum;
}

PixelStats compute(const Channel &c, PixelStats::Settings settings = {})
{
    std::atomic<bool> canceled{false};
    PixelStats        stats;
    stats.calculate(c, nullptr, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);
    return stats;
}

} // namespace

TEST_CASE("PixelStats::calculate bins every axis scale in a single pass")
{
    // binning follows the axis the histogram is drawn in, and all of them are computed up front so
    // switching the x-axis combo never rescans the image
    auto stats = compute(make_8bit_sRGB_ramp());

    REQUIRE(stats.computed);
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(stats.summary.valid_pixels));
        CHECK(stats.hist_normalization[s][1] > 0.f);
    }

    // the scales agree at the ends of the range and place the bins in between differently
    CHECK(stats.hist_xs[AxisScale_SRGB][128] != stats.hist_xs[AxisScale_Linear][128]);
    CHECK(stats.hist_xs[AxisScale_Asinh][128] != stats.hist_xs[AxisScale_Linear][128]);
}

TEST_CASE("PixelStats::Settings::match ignores settings that only affect drawing")
{
    PixelStats::Settings a;
    PixelStats::Settings b;
    REQUIRE(a.match(b));

    SUBCASE("exposure never reaches the computation")
    {
        b.exposure = 3.f;
        CHECK(a.match(b));
    }
    SUBCASE("every x scale is binned, so switching between them needs no recompute")
    {
        b.x_scale = AxisScale_SRGB;
        CHECK(a.match(b));
    }
    SUBCASE("the y scale only picks how the stored histogram is drawn")
    {
        b.y_scale = AxisScale_SymLog;
        CHECK(a.match(b));
    }
    SUBCASE("but the region of interest does")
    {
        b.roi = Box2i{int2{0}, int2{4}};
        CHECK_FALSE(a.match(b));
    }
    SUBCASE("and so does the measured image's pixels having changed")
    {
        b.content_version = 1;
        CHECK_FALSE(a.match(b));
    }
    SUBCASE("and the reference image's pixels having changed")
    {
        b.ref_content_version = 1;
        CHECK_FALSE(a.match(b));
    }
}

TEST_CASE("Channel::upload_tile writes the requested rectangle and nothing else")
{
    // a tile carries only its own rows, and everything outside it has to survive untouched
    const int w = 8, h = 6;

    SUBCASE("tightly packed rows")
    {
        Channel c        = make_identifiable_channel(w, h);
        Channel expected = make_identifiable_channel(w, h);

        const Box2i        tile{int2{2, 1}, int2{6, 4}};
        std::vector<float> data(size_t(tile.size().x) * tile.size().y);
        for (size_t i = 0; i < data.size(); ++i) data[i] = -1.f - float(i);

        c.upload_tile(tile, data.data());

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                // half-open, as upload_tile() treats it; Box::contains() would include the max edge
                const bool inside = x >= tile.min.x && x < tile.max.x && y >= tile.min.y && y < tile.max.y;
                if (inside)
                    CHECK(c(x, y) == data[size_t(y - tile.min.y) * tile.size().x + (x - tile.min.x)]);
                else
                    CHECK(c(x, y) == expected(x, y));
            }
    }

    SUBCASE("strided source reads one channel out of an interleaved payload")
    {
        Channel c = make_identifiable_channel(w, h);

        // three channels interleaved, as a renderer hands over an RGB tile; the middle one is wanted here
        const Box2i        tile{int2{0, 0}, int2{4, 3}};
        const int          n = 3, want = 1;
        std::vector<float> interleaved(size_t(tile.size().x) * tile.size().y * n);
        for (size_t i = 0; i < interleaved.size(); ++i) interleaved[i] = float(i);

        c.upload_tile(tile, interleaved.data() + want, n);

        for (int y = 0; y < tile.size().y; ++y)
            for (int x = 0; x < tile.size().x; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(c(x, y) == interleaved[(size_t(y) * tile.size().x + x) * n + want]);
            }
    }

    SUBCASE("a tile hanging off the edge writes only the part that lands")
    {
        Channel c        = make_identifiable_channel(w, h);
        Channel expected = make_identifiable_channel(w, h);

        // straddles the right and bottom edges; rows are as wide as the requested tile, not the clipped one
        const Box2i        tile{int2{w - 2, h - 2}, int2{w + 2, h + 2}};
        std::vector<float> data(size_t(tile.size().x) * tile.size().y);
        for (size_t i = 0; i < data.size(); ++i) data[i] = -1.f - float(i);

        c.upload_tile(tile, data.data());

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                if (x >= w - 2 && y >= h - 2)
                    CHECK(c(x, y) == data[size_t(y - tile.min.y) * tile.size().x + (x - tile.min.x)]);
                else
                    CHECK(c(x, y) == expected(x, y));
            }
    }

    SUBCASE("a tile entirely outside the channel is a no-op")
    {
        Channel c        = make_identifiable_channel(w, h);
        Channel expected = make_identifiable_channel(w, h);

        std::vector<float> data(16, -1.f);
        c.upload_tile(Box2i{int2{w, h}, int2{w + 4, h + 4}}, data.data());

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) CHECK(c(x, y) == expected(x, y));
    }
}

TEST_CASE("statistics computed after a tile lands reflect the new pixels")
{
    // measured off the channel directly, so this covers the pixels moving and not the cache invalidation
    // Settings::match() above is responsible for
    const int w = 16, h = 16;
    Channel   c("streamed", int2{w, h});
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) c(x, y) = 0.f;

    auto before = compute(c);
    REQUIRE(before.computed);
    CHECK(before.summary.maximum == 0.f);

    // one bucket finishes, well above everything around it
    const Box2i        tile{int2{4, 4}, int2{8, 8}};
    std::vector<float> data(size_t(tile.size().x) * tile.size().y, 5.f);
    c.upload_tile(tile, data.data());

    auto after = compute(c);
    REQUIRE(after.computed);
    CHECK(after.summary.maximum == 5.f);
    CHECK(after.summary.average == doctest::Approx(5.f * tile.size().x * tile.size().y / float(w * h)));
}

TEST_CASE("PixelStats::calculate takes its bin count from the channel's bit depth")
{
    Channel c("test", int2{16, 16});
    c.apply([](float, int x, int y) { return (x + y) / 30.f; });

    auto bins_for = [&c](int bits)
    {
        c.bits_per_sample = bits;
        return compute(c).num_bins;
    };

    CHECK(bins_for(0) == PixelStats::MAX_BINS); //< floating point or unknown
    CHECK(bins_for(32) == PixelStats::MAX_BINS);
    CHECK(bins_for(16) == PixelStats::MAX_BINS);
    CHECK(bins_for(8) == 256);
    CHECK(bins_for(4) == 16);
    CHECK(bins_for(1) == 2);
}

TEST_CASE("Channels in one image can carry different bit depths")
{
    // EXR records a pixel type per channel, so the bin count is a per-channel property
    Channel eight("eight", int2{8, 8});
    Channel floating("floating", int2{8, 8});
    eight.bits_per_sample    = 8;
    floating.bits_per_sample = 0;
    eight.apply([](float, int x, int) { return x / 8.f; });
    floating.apply([](float, int x, int) { return x / 8.f; });

    CHECK(compute(eight).num_bins == 256);
    CHECK(compute(floating).num_bins == PixelStats::MAX_BINS);
}

TEST_CASE("Each x scale's bins are uniform in that scale's own space")
{
    auto stats = compute(make_8bit_sRGB_ramp());
    REQUIRE(stats.num_bins == 256);

    auto edges_uniform_in = [&stats](AxisScale x_scale)
    {
        double first =
            axis_scale_fwd(stats.hist_xs[x_scale][1], x_scale) - axis_scale_fwd(stats.hist_xs[x_scale][0], x_scale);
        for (int i = 1; i < stats.num_bins; ++i)
        {
            double w = axis_scale_fwd(stats.hist_xs[x_scale][i + 1], x_scale) -
                       axis_scale_fwd(stats.hist_xs[x_scale][i], x_scale);
            if (std::abs(w - first) > 1e-4 * std::abs(first))
                return false;
        }
        return true;
    };

    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(edges_uniform_in((AxisScale)s));
        // the bins span the channel's own range, so the outer edges land on its extremes
        CHECK(stats.hist_xs[s][0] == doctest::Approx(stats.summary.minimum));
        CHECK(stats.hist_xs[s][stats.num_bins] == doctest::Approx(stats.summary.maximum));
    }
}

TEST_CASE("Every 8-bit level gets its own bin on the sRGB axis")
{
    // 8-bit content has at most 256 distinct levels, so binning it finer leaves gaps that read as a row of
    // detached spikes
    auto ramp = make_8bit_sRGB_ramp();

    SUBCASE("at a bin count matched to the source's depth")
    {
        auto stats = compute(ramp);
        REQUIRE(stats.num_bins == 256);
        // the 1/255 lattice and the 1/256 bin grid drift by up to a bin, leaving at most one gap
        CHECK(count_empty_bins(stats, AxisScale_SRGB) <= 1);
    }

    SUBCASE("binning finer than the source leaves most bins empty")
    {
        ramp.bits_per_sample = 0;
        auto stats           = compute(ramp);
        REQUIRE(stats.num_bins == PixelStats::MAX_BINS);
        CHECK(count_empty_bins(stats, AxisScale_SRGB) >= 250);
    }
}

TEST_CASE("The asinh axis keeps 8-bit levels within a bin or two of each other")
{
    // asinh's knee has to sit well above the darkest 8-bit level, or the curve is logarithmic across the
    // whole of [0,1] and flings consecutive dark levels tens of bins apart
    auto stats = compute(make_8bit_sRGB_ramp());
    REQUIRE(stats.num_bins == 256);

    int longest = 0, run = 0;
    for (int i = 0; i < stats.num_bins; ++i)
    {
        run     = stats.hist_ys[AxisScale_Asinh][i] == 0.f ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    CHECK(longest <= 4); //< a run this short is under a pixel wide once the fill reduces bins to columns
}

TEST_CASE("Histogram bins hold plain counts of the valid pixels")
{
    // bins are equally wide on screen, so their heights are counts and not densities
    Channel c("test", int2{20, 20});
    c.apply([](float, int x, int y) { return (x * 20 + y) / 400.f; });

    auto stats = compute(c);
    CHECK(stats.summary.valid_pixels == 400);
    CHECK(total_bin_count(stats, AxisScale_Linear) == doctest::Approx(400.0));
}

TEST_CASE("NaN and Inf pixels are left out of the histogram")
{
    Channel c("test", int2{4, 4});
    c.apply([](float, int, int) { return 0.5f; });
    c(0, 0) = std::numeric_limits<float>::quiet_NaN();
    c(1, 0) = std::numeric_limits<float>::infinity();
    c(2, 0) = -std::numeric_limits<float>::infinity();

    auto stats = compute(c);
    CHECK(stats.summary.nan_pixels == 1);
    CHECK(stats.summary.inf_pixels == 2);
    CHECK(stats.summary.valid_pixels == 13);
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(13.0));
    }
}

TEST_CASE("A constant-valued channel produces a finite histogram")
{
    // min == max transforms to a zero-width range, which would divide by zero when indexing bins
    Channel c("test", int2{4, 4});
    c.apply([](float, int, int) { return 0.5f; });

    auto stats = compute(c);
    REQUIRE(stats.computed);
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(16.0));
        CHECK(stats.hist_ys[s][0] == doctest::Approx(16.f)); //< all of them in the first bin
        CHECK(std::isfinite(stats.hist_y_limits[s][1]));
        for (int i = 0; i <= stats.num_bins; ++i) CHECK(std::isfinite(stats.hist_xs[s][i]));
    }
}

TEST_CASE("An all-NaN channel produces an empty but valid histogram")
{
    Channel c("test", int2{4, 4});
    c.apply([](float, int, int) { return std::numeric_limits<float>::quiet_NaN(); });

    auto stats = compute(c);
    CHECK(stats.computed);
    CHECK(stats.summary.valid_pixels == 0);
    // NaN compares false against either clip bound, so it leaves the clip extremes unset as well
    CHECK(!std::isfinite(stats.summary.extreme_minimum));
    CHECK(!std::isfinite(stats.summary.extreme_maximum));
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(0.0));
        CHECK(stats.hist_y_limits[s][0] == 0.f);
        CHECK(stats.hist_y_limits[s][1] == 1.f);
    }
}

TEST_CASE("The upper y limit ignores the tallest few bins")
{
    // one flat region (a black background, a blown highlight) would otherwise squash the rest flat
    Channel c("test", int2{100, 10});
    c.apply([](float, int x, int y) { return x < 90 ? 0.f : (x - 90 + y * 10) / 100.f; });

    auto stats = compute(c);
    REQUIRE(stats.summary.valid_pixels == 1000);
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(stats.hist_y_limits[s][0] == 0.f); //< ImPlot's SymLog is defined at zero
        CHECK(stats.hist_y_limits[s][1] >= 1.f);
        CHECK(stats.hist_y_limits[s][1] < 900.f); //< the spike itself runs off the top
    }
}

TEST_CASE("value_to_bin and bin_to_value round-trip")
{
    auto stats = compute(make_8bit_sRGB_ramp());
    REQUIRE(stats.num_bins == 256);

    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        auto x_scale = (AxisScale)s;
        CAPTURE(s);

        for (int i = 0; i < stats.num_bins; ++i)
            CHECK(stats.value_to_bin(stats.bin_to_value(i + 0.5, x_scale), x_scale) == i);

        CHECK(stats.value_to_bin(stats.summary.minimum, x_scale) == 0);
        CHECK(stats.clamp_idx(stats.value_to_bin(stats.summary.maximum, x_scale)) == stats.num_bins - 1);

        // non-finite input has no bin, and must not be cast to int unclamped
        CHECK(stats.value_to_bin(std::numeric_limits<double>::quiet_NaN(), x_scale) < 0);
        CHECK(stats.value_to_bin(-std::numeric_limits<double>::infinity(), x_scale) < 0);
        CHECK(stats.value_to_bin(std::numeric_limits<double>::infinity(), x_scale) >= stats.num_bins);
    }
}

TEST_CASE("PixelStats::calculate uses the selected sub-region, not the image origin")
{
    // 10x10 channel; the selection below covers pixels (3,3),(4,3),(3,4),(4,4) -> values 33,34,43,44
    auto img = make_identifiable_channel(10, 10);

    PixelStats::Settings settings;
    settings.roi = Box2i{int2{3, 3}, int2{5, 5}};

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, nullptr, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

    CHECK(stats.summary.valid_pixels == 4);
    CHECK(stats.summary.minimum == doctest::Approx(33.f));
    CHECK(stats.summary.maximum == doctest::Approx(44.f));
    CHECK(stats.summary.average == doctest::Approx(38.5));

    // the image's own top-left corner would be values 0,1,10,11
    CHECK(stats.summary.minimum != doctest::Approx(0.f));
    CHECK(stats.summary.maximum != doctest::Approx(11.f));
}

TEST_CASE("PixelStats::calculate covers the whole image when no selection is active")
{
    auto img = make_identifiable_channel(4, 4);

    PixelStats::Settings settings; // default-constructed roi has no volume -> whole image

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, nullptr, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

    // values range from 0 (0,0) to 33 (3,3) across all 16 pixels
    CHECK(stats.summary.valid_pixels == 16);
    CHECK(stats.summary.minimum == doctest::Approx(0.f));
    CHECK(stats.summary.maximum == doctest::Approx(33.f));
}

TEST_CASE("PixelStats::calculate offsets a non-zero image data origin correctly")
{
    // An image whose data window doesn't start at (0,0), as an EXR's may not: the channel's own array is still
    // 0-indexed, and img_data_origin says where that origin sits in the global space the ROI is in.
    auto img = make_identifiable_channel(10, 10);

    PixelStats::Settings settings;
    // In global coordinates, with img_data_origin={5,5}, this selects local pixels (3,3)-(4,4) again: 33,34,43,44
    settings.roi = Box2i{int2{8, 8}, int2{10, 10}};

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, nullptr, int2{5, 5}, nullptr, nullptr, int2{0, 0}, settings, canceled);

    CHECK(stats.summary.valid_pixels == 4);
    CHECK(stats.summary.minimum == doctest::Approx(33.f));
    CHECK(stats.summary.maximum == doctest::Approx(44.f));
}

TEST_CASE("PixelStats::calculate counts NaN/Inf pixels separately and excludes them from min/max/average")
{
    // 4x4, values 0..15 (x + 4*y), with three pixels replaced by NaN/+Inf/-Inf; (0,0)=0 and (3,3)=15 are left
    // alone, so min/max staying at 0/15 says the non-finite ones did not reach them
    Channel img("test", int2{4, 4});
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) img(x, y) = float(x + 4 * y);
    img(1, 1) = std::numeric_limits<float>::quiet_NaN(); // value 5
    img(2, 2) = std::numeric_limits<float>::infinity();  // value 10
    img(3, 0) = -std::numeric_limits<float>::infinity(); // value 3

    PixelStats::Settings settings; // whole image

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, nullptr, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

    CHECK(stats.summary.nan_pixels == 1);
    CHECK(stats.summary.inf_pixels == 2);
    CHECK(stats.summary.valid_pixels == 13); // 16 - 1 NaN - 2 Inf
    CHECK(stats.summary.minimum == doctest::Approx(0.f));
    CHECK(stats.summary.maximum == doctest::Approx(15.f));
}

TEST_CASE("PixelStats::calculate counts FLT_MAX markers apart from the measurements")
{
    // The shape of a renderer's depth channel: real depths over most of the image and FLT_MAX wherever the
    // ray hit nothing (OpenEXR's Blobbies.exr is a third such pixels). One of those in the range fixes the
    // maximum at 3.4e38, which no exposure can fit.
    Channel c("Z", int2{4, 4});
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) c(x, y) = 7.f + float(x + 4 * y);
    c(0, 0) = std::numeric_limits<float>::max();
    c(3, 3) = std::numeric_limits<float>::max();
    c(1, 1) = std::numeric_limits<float>::lowest(); //< the same marker at the other end of the range

    auto stats = compute(c);

    CHECK(stats.summary.huge_pixels == 3);
    CHECK(stats.summary.nan_pixels == 0);
    CHECK(stats.summary.inf_pixels == 0);
    CHECK(stats.summary.valid_pixels == 13);
    // the surviving measurements are 7+1 .. 7+14, skipping the three replaced pixels
    CHECK(stats.summary.minimum == doctest::Approx(8.f));
    CHECK(stats.summary.maximum == doctest::Approx(21.f));
    CHECK(stats.summary.average == doctest::Approx(14.6923f).epsilon(1e-4));

    // binned like the NaNs and infinities: left out entirely, not piled into the end bin the narrowed range
    // now stops at
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(13.0));
    }
}

TEST_CASE("The clip warnings see the samples the measurement range leaves out")
{
    // The extremes the histogram's warning triangles test against take in everything the shader stripes, so a
    // channel whose only content past a bound is an infinity or a marker still reports crossing it.
    Channel c("test", int2{4, 4});
    c.apply([](float, int, int) { return 0.5f; });
    c(0, 0) = std::numeric_limits<float>::max();
    c(1, 0) = -std::numeric_limits<float>::infinity();
    c(2, 0) = std::numeric_limits<float>::quiet_NaN();

    auto stats = compute(c);

    // the measurement range is the flat 0.5 that is left
    CHECK(stats.summary.minimum == doctest::Approx(0.5f));
    CHECK(stats.summary.maximum == doctest::Approx(0.5f));
    // the clip range reaches past both bounds, and NaN moves neither end
    CHECK(stats.summary.extreme_maximum == std::numeric_limits<float>::max());
    CHECK(stats.summary.extreme_minimum == -std::numeric_limits<float>::infinity());
}

TEST_CASE("A channel holding only markers produces an empty but valid histogram")
{
    // same as an all-NaN channel: no range to bin over
    Channel c("Z", int2{4, 4});
    c.apply([](float, int, int) { return std::numeric_limits<float>::max(); });

    auto stats = compute(c);
    CHECK(stats.computed);
    CHECK(stats.summary.huge_pixels == 16);
    CHECK(stats.summary.valid_pixels == 0);
    CHECK(!std::isfinite(stats.summary.minimum));
    CHECK(!std::isfinite(stats.summary.maximum));
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(0.0));
    }
}

TEST_CASE("Values just short of FLT_MAX are measurements, not markers")
{
    // the line is at the top of the float range itself; an enormous sample is still a measurement
    Channel c("test", int2{2, 2});
    c.apply([](float, int, int) { return 1.f; });
    c(0, 0) = std::nextafterf(std::numeric_limits<float>::max(), 0.f);

    auto stats = compute(c);
    CHECK(stats.summary.huge_pixels == 0);
    CHECK(stats.summary.valid_pixels == 4);
    CHECK(stats.summary.maximum == c(0, 0));
}

TEST_CASE("PixelStats::calculate applies each blend mode against a reference image")
{
    // both channels are constant-valued, so every pixel's blended result is identical and known
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
        stats.calculate(img, nullptr, int2{0, 0}, &ref, nullptr, int2{0, 0}, settings, canceled);

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
    // BlendMode_Normal ignores the reference sample (blend() returns the image's own value), but a reference
    // channel of a different size is an ordinary thing to have selected.
    auto img = make_identifiable_channel(10, 10);
    auto ref = make_identifiable_channel(2, 2);

    PixelStats::Settings settings; // BlendMode_Normal by default, whole image

    PixelStats        stats;
    std::atomic<bool> canceled{false};
    stats.calculate(img, nullptr, int2{0, 0}, &ref, nullptr, int2{0, 0}, settings, canceled);

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
    stats.calculate(img, nullptr, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

    CHECK_FALSE(stats.computed);
    CHECK(stats.summary.valid_pixels == 0);
    CHECK(stats.summary.minimum == std::numeric_limits<float>::infinity());
    CHECK(stats.summary.maximum == -std::numeric_limits<float>::infinity());
}

TEST_CASE("PixelStats::calculate reports straight values when given an alpha channel")
{
    // a channel premultiplied on load (Image::finalize()) is divided back out, so the histogram and summary
    // describe what the file holds
    Channel img("img", int2{2, 2});
    Channel alpha("alpha", int2{2, 2});
    alpha.apply([](float, int, int) { return 0.5f; });
    img.apply([](float, int, int) { return 0.5f; }); // 1.0 straight, premultiplied by an alpha of 0.5

    PixelStats::Settings settings;
    std::atomic<bool>    canceled{false};

    SUBCASE("with the alpha channel")
    {
        PixelStats stats;
        stats.calculate(img, &alpha, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

        CHECK(stats.summary.valid_pixels == 4);
        CHECK(stats.summary.average == doctest::Approx(1.0));
        CHECK(stats.summary.maximum == doctest::Approx(1.f));
    }

    SUBCASE("without it, the stored value is reported as-is")
    {
        PixelStats stats;
        stats.calculate(img, nullptr, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

        CHECK(stats.summary.average == doctest::Approx(0.5));
    }
}

TEST_CASE("Unpremultiplying puts 8-bit samples back on the source's lattice")
{
    // finalize() multiplies by max(k_small_alpha, alpha) and calculate() divides by the same clamped
    // denominator, so the file's levels survive and still bin one-to-one
    auto    ramp = make_8bit_sRGB_ramp();
    Channel alpha("alpha", int2{256, 1});
    for (int k = 0; k < 256; ++k) alpha(k, 0) = std::max(k_small_alpha, (k % 255 + 1) / 255.f);

    Channel premultiplied("premultiplied", int2{256, 1});
    premultiplied.bits_per_sample = 8;
    for (int k = 0; k < 256; ++k) premultiplied(k, 0) = alpha(k, 0) * ramp(k, 0);

    std::atomic<bool>    canceled{false};
    PixelStats::Settings settings;
    PixelStats           stats;
    stats.calculate(premultiplied, &alpha, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

    REQUIRE(stats.num_bins == 256);
    CHECK(count_empty_bins(stats, AxisScale_SRGB) <= 1);
}

TEST_CASE("Bin count never exceeds the histogram's storage")
{
    // hist_xs/hist_ys are sized MAX_BINS. Depths of 9..15 bits are ordinary: 10- and 12-bit HEIF/AVIF, 12- and
    // 14-bit camera raw, 12-bit TIFF.
    for (int bits = -1; bits <= 64; ++bits)
    {
        CAPTURE(bits);
        CHECK(PixelStats::bins_for_bit_depth(bits) <= PixelStats::MAX_BINS);
        CHECK(PixelStats::bins_for_bit_depth(bits) >= 1);
    }
}

TEST_CASE("A 10-bit channel bins without running off the end of its storage")
{
    // calculate() fills hist_xs[0..num_bins] inclusive
    Channel c("test", int2{16, 16});
    c.bits_per_sample = 10; // e.g. a 10-bit AVIF
    c.apply([](float, int x, int y) { return (x + y) / 30.f; });

    auto stats = compute(c);
    CHECK(stats.num_bins <= PixelStats::MAX_BINS);
}

TEST_CASE("A selection that misses the channel leaves the statistics empty")
{
    // Box::intersect() does not keep min <= max, so a selection missing the channel in one axis leaves an
    // inverted box whose negative volume() becomes a size_t work range near 2^64. The channel has to be large
    // enough that |volume()| exceeds one block (1 << 20), or the block count rounds to zero and nothing runs.
    Channel c = make_identifiable_channel(2048, 1024);

    SUBCASE("missing in y only")
    {
        PixelStats::Settings settings;
        settings.roi = Box2i{int2{0, 2000}, int2{2048, 3000}};

        auto stats = compute(c, settings);
        CHECK(stats.computed);
        CHECK(stats.summary.valid_pixels == 0);
        CHECK(total_bin_count(stats, AxisScale_Linear) == 0.0);
    }

    SUBCASE("missing in x only")
    {
        PixelStats::Settings settings;
        settings.roi = Box2i{int2{4000, 0}, int2{5000, 1024}};

        auto stats = compute(c, settings);
        CHECK(stats.computed);
        CHECK(stats.summary.valid_pixels == 0);
    }

    SUBCASE("missing in both axes")
    {
        // both axes inverted multiply back to a positive volume() over a region that does not exist
        PixelStats::Settings settings;
        settings.roi = Box2i{int2{4000, 2000}, int2{5000, 3000}};

        auto stats = compute(c, settings);
        CHECK(stats.computed);
        CHECK(stats.summary.valid_pixels == 0);
    }

    SUBCASE("a selection that does overlap is unaffected")
    {
        PixelStats::Settings settings;
        settings.roi = Box2i{int2{10, 20}, int2{40, 50}};

        auto stats = compute(c, settings);
        CHECK(stats.computed);
        CHECK(stats.summary.valid_pixels == 30 * 30);
    }
}

TEST_CASE("Statistics over an arbitrary selection count exactly the pixels it overlaps")
{
    // A selection is never clamped to the image, so calculate() has to cope with any box. The channel is kept
    // small so the sweep stays quick, which means an inverted box here rounds down to no blocks at all;
    // reaching the out-of-bounds read needs the larger channel the case above is sized for.
    constexpr int w = 128, h = 64;
    Channel       c = make_identifiable_channel(w, h);

    // well outside, just outside, straddling each edge, inside, and containing the whole channel
    const int xs[] = {-500, -1, 0, 10, w - 10, w, 500};
    const int ys[] = {-300, -1, 0, 20, h - 20, h, 300};

    for (int x0 : xs)
        for (int x1 : xs)
            for (int y0 : ys)
                for (int y1 : ys)
                {
                    PixelStats::Settings settings;
                    settings.roi = Box2i{int2{x0, y0}, int2{x1, y1}};

                    // a selection with no volume means "no selection" and measures the whole channel;
                    // otherwise the count is the plain rectangle overlap
                    const int ox       = std::max(0, std::min(x1, w) - std::max(x0, 0));
                    const int oy       = std::max(0, std::min(y1, h) - std::max(y0, 0));
                    const int expected = settings.roi.has_volume() ? ox * oy : w * h;

                    CAPTURE(x0);
                    CAPTURE(x1);
                    CAPTURE(y0);
                    CAPTURE(y1);

                    auto stats = compute(c, settings);
                    CHECK(stats.computed);
                    CHECK(stats.summary.valid_pixels == expected);
                    CHECK(total_bin_count(stats, AxisScale_Linear) == doctest::Approx((double)expected));
                }
}
