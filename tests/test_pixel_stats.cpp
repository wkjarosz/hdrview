//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "colorspace.h"
#include "common.h"
#include "image.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

/// Statistics over `c`, whose data window starts at `origin` in the space the selection is expressed in.
PixelStats compute(const Channel &c, PixelStats::Settings settings = {}, int2 origin = int2{0, 0})
{
    std::atomic<bool> canceled{false};
    PixelStats        stats;
    stats.calculate(c, nullptr, origin, nullptr, nullptr, int2{0, 0}, settings, canceled);
    return stats;
}

} // namespace

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
    constexpr int w = 8, h = 6;

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

TEST_CASE("PixelStats::calculate takes its bin count from the channel's bit depth")
{
    // An n-bit source holds 2^n levels, so it gets that many bins up to the storage the histogram has, and a
    // float or unknown depth (0, or anything nonsensical) gets the full resolution. Depths of 9..15 bits are
    // ordinary: 10- and 12-bit HEIF/AVIF, 12- and 14-bit camera raw, 12-bit TIFF.
    Channel c("test", int2{16, 16});
    c.apply([](float, int x, int y) { return (x + y) / 30.f; });

    for (int bits = -1; bits <= 64; ++bits)
    {
        CAPTURE(bits);
        const int bins = PixelStats::bins_for_bit_depth(bits);
        CHECK(bins >= 1);
        CHECK(bins <= PixelStats::MAX_BINS); //< hist_xs/hist_ys are sized MAX_BINS
        CHECK(bins == (bits >= 1 && bits <= 15 ? std::min(1 << bits, PixelStats::MAX_BINS) : PixelStats::MAX_BINS));

        // and calculate(), which fills hist_xs[0..num_bins] inclusive, uses that count
        c.bits_per_sample = bits;
        CHECK(compute(c).num_bins == bins);
    }
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

TEST_CASE("value_exceeded_by() agrees with the exact order statistic to within an asinh bin")
{
    // a gentle ramp, and a heavy-tailed one spanning many stops, whose top fraction a linear binning would lump
    // into one bin
    for (bool heavy_tail : {false, true})
    {
        INFO("heavy tail = ", heavy_tail);
        Channel c("test", int2{100, 100});
        c.apply(
            [heavy_tail](float, int x, int y)
            {
                const float t = (x * 100 + y) / 10000.f;
                return heavy_tail ? std::exp2(t * 16.f - 4.f) : t;
            });
        std::vector<float> sorted;
        for (int y = 0; y < c.height(); ++y)
            for (int x = 0; x < c.width(); ++x) sorted.push_back(c(x, y));
        std::sort(sorted.begin(), sorted.end());

        auto stats = compute(c);
        REQUIRE(stats.computed);
        CHECK(stats.value_exceeded_by(0.f) == stats.summary.maximum);

        const double bin_width = stats.hist_normalization[AxisScale_Asinh][1] / stats.num_bins;
        for (float fraction : {1e-3f, 1e-2f, 0.1f, 0.5f, 0.9f})
        {
            INFO("fraction = ", fraction);
            const float exact = sorted[size_t(std::lround((1.0 - fraction) * (sorted.size() - 1)))];
            const float got   = stats.value_exceeded_by(fraction);
            CHECK(std::abs(axis_scale_fwd(got, AxisScale_Asinh) - axis_scale_fwd(exact, AxisScale_Asinh)) <=
                  1.01 * bin_width);
        }
    }

    // nothing measured, nothing to report
    CHECK(std::isnan(PixelStats{}.value_exceeded_by(1e-3f)));
}

TEST_CASE("Non-finite samples and FLT_MAX markers are counted apart from the measurements")
{
    // The ordinary values 0..43 surround one of each kind of sample that is not a measurement: NaN, the two
    // infinities, and FLT_MAX at either end of the range, which a renderer's depth channel stores wherever the
    // ray hit nothing (OpenEXR's Blobbies.exr is a third such pixels). The value one step short of FLT_MAX is
    // a measurement, however enormous. Without the infinities, NaN takes their pixels, so the clip extremes
    // are seen to come from the markers as much as from the infinities.
    constexpr float k_inf = std::numeric_limits<float>::infinity(), k_nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float k_max = std::numeric_limits<float>::max(), k_lowest = std::numeric_limits<float>::lowest();
    const float     k_almost_max = std::nextafterf(k_max, 0.f);

    for (bool with_infinities : {true, false})
    {
        CAPTURE(with_infinities);
        Channel c = make_identifiable_channel(5, 4);
        c(1, 1)   = k_nan;
        c(2, 1)   = with_infinities ? k_inf : k_nan;
        c(3, 1)   = with_infinities ? -k_inf : k_nan;
        c(1, 2)   = k_max;
        c(2, 2)   = k_lowest;
        c(3, 2)   = k_almost_max;

        std::vector<double> measurements;
        for (int y = 0; y < c.height(); ++y)
            for (int x = 0; x < c.width(); ++x)
                if (std::isfinite(c(x, y)) && std::abs(c(x, y)) < k_max)
                    measurements.push_back(c(x, y));
        REQUIRE(measurements.size() == 15);
        double mean = 0.0;
        for (double v : measurements) mean += v / double(measurements.size());

        auto stats = compute(c);
        REQUIRE(stats.computed);
        CHECK(stats.summary.nan_pixels == (with_infinities ? 1 : 3));
        CHECK(stats.summary.inf_pixels == (with_infinities ? 2 : 0));
        CHECK(stats.summary.huge_pixels == 2);
        CHECK(stats.summary.valid_pixels == int(measurements.size()));

        // (0,0) holds the smallest ordinary value and (3,2) the largest measurement, so the range staying at
        // those two says nothing else reached it
        CHECK(stats.summary.minimum == 0.f);
        CHECK(stats.summary.maximum == k_almost_max);
        CHECK(stats.summary.average == doctest::Approx(mean).epsilon(1e-6));

        // The clip warnings' extremes take in every non-NaN sample, as the shader's stripes do, so a channel
        // whose only content past a bound is an infinity or a marker still reports crossing it.
        CHECK(stats.summary.extreme_maximum == (with_infinities ? k_inf : k_max));
        CHECK(stats.summary.extreme_minimum == (with_infinities ? -k_inf : k_lowest));

        // left out of the histogram entirely, not piled into the end bins the range stops at
        for (int s = 0; s < AxisScale_COUNT; ++s)
        {
            CAPTURE(s);
            CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(double(measurements.size())));
        }
    }
}

TEST_CASE("A channel with a single value, or no measurements at all, still produces a usable histogram")
{
    // A constant transforms to a zero-width range, which would divide by zero when indexing bins; a channel
    // holding nothing but NaNs, infinities or markers has no range to bin over at all.
    constexpr float k_inf = std::numeric_limits<float>::infinity(), k_max = std::numeric_limits<float>::max();
    for (float v : {0.5f, std::numeric_limits<float>::quiet_NaN(), k_inf, -k_inf, k_max, -k_max})
    {
        CAPTURE(v);
        Channel c("test", int2{4, 4});
        c.apply([v](float, int, int) { return v; });
        const bool measured = std::isfinite(v) && !PixelStats::is_marker(v);

        auto stats = compute(c);
        REQUIRE(stats.computed);
        const auto &sum = stats.summary;
        CHECK(sum.valid_pixels == (measured ? 16 : 0));
        CHECK(sum.nan_pixels + sum.inf_pixels + sum.huge_pixels + sum.valid_pixels == 16);
        CHECK(std::isfinite(sum.minimum) == measured);
        CHECK(std::isfinite(sum.maximum) == measured);

        // NaN compares false against either bound, so it leaves the clip extremes unset; anything else sets both
        if (std::isnan(v))
        {
            CHECK(sum.extreme_minimum == k_inf);
            CHECK(sum.extreme_maximum == -k_inf);
        }
        else
        {
            CHECK(sum.extreme_minimum == v);
            CHECK(sum.extreme_maximum == v);
        }

        for (int s = 0; s < AxisScale_COUNT; ++s)
        {
            CAPTURE(s);
            CHECK(total_bin_count(stats, (AxisScale)s) == doctest::Approx(double(sum.valid_pixels)));
            if (measured)
                CHECK(stats.hist_ys[s][0] == doctest::Approx(16.f)); //< all of them in the first bin
            CHECK(std::isfinite(stats.hist_y_limits[s][0]));
            CHECK(std::isfinite(stats.hist_y_limits[s][1]));
            CHECK(stats.hist_y_limits[s][1] > stats.hist_y_limits[s][0]);
            for (int i = 0; i <= stats.num_bins; ++i) CHECK(std::isfinite(stats.hist_xs[s][i]));
        }
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

TEST_CASE("PixelStats::calculate applies each blend mode against a reference image")
{
    // Both channels vary, and cross: the image is below the reference on the left and above it on the right,
    // so a mode that swapped its operands, or a reference read at the wrong pixel, changes the result. The
    // reference stays positive, so dividing by it is finite.
    static constexpr int w = 8, h = 6;
    Channel   img("img", int2{w, h});
    Channel   ref("ref", int2{w, h});
    img.apply([](float, int x, int y) { return float(x) - 3.f + 0.25f * float(y); });
    ref.apply([](float, int x, int y) { return 1.f + 0.5f * float(w - 1 - x) + 0.1f * float(y); });

    for (int m = 0; m < BlendMode_COUNT; ++m)
    {
        const auto mode = BlendMode_(m);
        CAPTURE(blend_mode_names()[m]);

        double lo = INFINITY, hi = -INFINITY, sum = 0.0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const double v = blend(img(x, y), ref(x, y), mode);
                lo             = std::min(lo, v);
                hi             = std::max(hi, v);
                sum += v;
            }

        PixelStats::Settings settings;
        settings.blend_mode = mode;
        PixelStats        stats;
        std::atomic<bool> canceled{false};
        stats.calculate(img, nullptr, int2{0, 0}, &ref, nullptr, int2{0, 0}, settings, canceled);

        REQUIRE(stats.computed);
        CHECK(stats.summary.valid_pixels == w * h);
        CHECK(stats.summary.minimum == doctest::Approx(lo));
        CHECK(stats.summary.maximum == doctest::Approx(hi));
        CHECK(stats.summary.average == doctest::Approx(sum / (w * h)));
    }
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

    // On the low edge (or outside it, under the shifted origin), inside, on the high edge, and far outside;
    // every ordered pair of these makes a box with volume.
    const int          xs[] = {0, 10, w - 10, w, 500};
    const int          ys[] = {0, 20, h - 20, h, 300};
    std::vector<Box2i> boxes;
    for (int x0 : xs)
        for (int x1 : xs)
            for (int y0 : ys)
                for (int y1 : ys)
                    if (x0 < x1 && y0 < y1)
                        boxes.push_back(Box2i{int2{x0, y0}, int2{x1, y1}});
    // and the boxes without volume, which mean "no selection": an empty one, and one inverted in each axis
    boxes.push_back(Box2i{int2{10, 20}, int2{10, 40}});
    boxes.push_back(Box2i{int2{40, 20}, int2{10, 40}});
    boxes.push_back(Box2i{int2{10, 40}, int2{40, 20}});

    // An image's data window need not start at the origin, as an EXR's may not: the channel's own array is
    // still 0-indexed, and the origin says where that array sits in the space the selection is in.
    for (int2 origin : {int2{0, 0}, int2{5, 7}})
        for (const Box2i &roi : boxes)
        {
            PixelStats::Settings settings;
            settings.roi = roi;

            // the overlap in the channel's own coordinates, or the whole channel for a box without volume
            const bool whole   = !roi.has_volume();
            const int  first_x = whole ? 0 : std::max(roi.min.x - origin.x, 0);
            const int  first_y = whole ? 0 : std::max(roi.min.y - origin.y, 0);
            const int  end_x   = whole ? w : std::min(roi.max.x - origin.x, w);
            const int  end_y   = whole ? h : std::min(roi.max.y - origin.y, h);
            const int  count   = std::max(0, end_x - first_x) * std::max(0, end_y - first_y);

            CAPTURE(origin.x);
            CAPTURE(origin.y);
            CAPTURE(roi.min.x);
            CAPTURE(roi.min.y);
            CAPTURE(roi.max.x);
            CAPTURE(roi.max.y);

            auto stats = compute(c, settings, origin);
            CHECK(stats.computed);
            CHECK(stats.summary.valid_pixels == count);
            CHECK(total_bin_count(stats, AxisScale_Linear) == doctest::Approx((double)count));

            if (count == 0)
                continue;

            // make_identifiable_channel's value is x + 10*y, so the corners and the mean of the covered
            // rectangle are all closed forms
            const int last_x = end_x - 1, last_y = end_y - 1;
            CHECK(stats.summary.minimum == doctest::Approx(float(first_x + 10 * first_y)));
            CHECK(stats.summary.maximum == doctest::Approx(float(last_x + 10 * last_y)));
            CHECK(stats.summary.average == doctest::Approx(0.5 * (first_x + last_x) + 5.0 * (first_y + last_y)));
        }
}
