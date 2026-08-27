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

// A 256x1 channel holding every 8-bit sRGB code, decoded to linear the way the PNG and JPEG loaders do.
Channel make_8bit_sRGB_ramp()
{
    Channel c("ramp", int2{256, 1});
    c.bits_per_sample = 8;
    for (int k = 0; k < 256; ++k) c(k, 0) = sRGB_to_linear(k / 255.f);
    return c;
}

// Number of bins holding no samples at all, which is what the "comb" artifact looks like in the data.
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
    // Binning follows the axis the histogram is drawn in, and all of them are computed up front so that
    // switching the x-axis combo never has to rescan the image.
    auto stats = compute(make_8bit_sRGB_ramp());

    REQUIRE(stats.computed);
    for (int s = 0; s < AxisScale_COUNT; ++s)
    {
        CAPTURE(s);
        CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(stats.summary.valid_pixels));
        CHECK(stats.hist_normalization[s][1] > 0.f);
    }

    // The scales agree at the ends of the range but place the bins in between differently.
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
    // EXR records a pixel type per channel, so the bin count has to be a per-channel property.
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
        // The bins span the channel's own range, so the outer edges land on its extremes.
        CHECK(stats.hist_xs[s][0] == doctest::Approx(stats.summary.minimum));
        CHECK(stats.hist_xs[s][stats.num_bins] == doctest::Approx(stats.summary.maximum));
    }
}

TEST_CASE("Every 8-bit level gets its own bin on the sRGB axis")
{
    // The comb artifact: 8-bit content has at most 256 distinct levels, so binning it any finer leaves
    // gaps that read as a row of detached spikes.
    auto ramp = make_8bit_sRGB_ramp();

    SUBCASE("at a bin count matched to the source's depth")
    {
        auto stats = compute(ramp);
        REQUIRE(stats.num_bins == 256);
        // The 1/255 lattice and the 1/256 bin grid drift by up to a bin, leaving at most one gap.
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
    // whole of [0,1] and flings consecutive dark levels tens of bins apart.
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
    // Bins are equally wide on screen, so their heights are counts rather than densities.
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
    // min == max transforms to a zero-width range, which would divide by zero when indexing bins.
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
    // One flat region -- a black background, a blown highlight -- would otherwise squash the rest flat.
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

        // Non-finite input has no bin, and must not be cast to int unclamped.
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
    stats.calculate(img, nullptr, int2{0, 0}, nullptr, nullptr, int2{0, 0}, settings, canceled);

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
    stats.calculate(img, nullptr, int2{5, 5}, nullptr, nullptr, int2{0, 0}, settings, canceled);

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
    // BlendMode_Normal ignores the reference sample entirely (blend() just returns the image's own value), but
    // a reference image/channel of a different size than the current one is a completely ordinary thing to
    // select (e.g. comparing two unrelated photos) -- it must not crash just because Normal is the active mode.
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
    // A channel premultiplied on load (see Image::finalize()) has to be divided back out so the histogram
    // and summary describe what the file holds rather than HDRView's internal representation.
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
    // denominator, so the file's levels survive the round trip and still bin one-to-one.
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
    // hist_xs/hist_ys are sized MAX_BINS, so bins_for_bit_depth() must never return more than that.
    // Depths of 9..15 bits are ordinary: 10- and 12-bit HEIF/AVIF, 12- and 14-bit camera raw, 12-bit TIFF.
    for (int bits = -1; bits <= 64; ++bits)
    {
        CAPTURE(bits);
        CHECK(PixelStats::bins_for_bit_depth(bits) <= PixelStats::MAX_BINS);
        CHECK(PixelStats::bins_for_bit_depth(bits) >= 1);
    }
}

TEST_CASE("A 10-bit channel bins without running off the end of its storage")
{
    // Reaches the out-of-bounds write directly: calculate() fills hist_xs[0..num_bins] inclusive.
    Channel c("test", int2{16, 16});
    c.bits_per_sample = 10; // e.g. a 10-bit AVIF
    c.apply([](float, int x, int y) { return (x + y) / 30.f; });

    auto stats = compute(c);
    CHECK(stats.num_bins <= PixelStats::MAX_BINS);
}

TEST_CASE("A selection that misses the channel leaves the statistics empty")
{
    // Box::intersect() does not keep min <= max, so a selection that misses the channel in one axis leaves
    // an inverted box whose volume() is negative -- as the size_t length of a parallel work range, a count
    // near 2^64. The channel has to be large enough that |volume()| exceeds one block (1 << 20); below
    // that the same conversion lands on a block count of zero and nothing would run either way.
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
        // Both axes inverted multiply back to a positive volume(): a plausible-looking count over a
        // region that does not exist.
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
