//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/filters.h"

#include <cmath>

//! The whole of \p a, for the cases that are about the filter rather than about the region.
static Box2i whole(const Array2Df &a) { return Box2i{int2{0}, a.size()}; }

namespace
{

//! Sum of every sample, which a normalized blur must preserve away from the edges.
double total(const Array2Df &a)
{
    double sum = 0.0;
    for (int i = 0; i < a.num_elements(); ++i) sum += a(i);
    return sum;
}

Array2Df impulse(int2 size)
{
    Array2Df a{size};
    a(size.x / 2, size.y / 2) = 1.f;
    return a;
}

Array2Df constant(int2 size, float v)
{
    Array2Df a{size};
    for (int i = 0; i < a.num_elements(); ++i) a(i) = v;
    return a;
}

} // namespace

TEST_CASE("A blur leaves a constant image constant")
{
    // The property that catches an unnormalized kernel: if the weights do not sum to one, a flat field
    // comes back brighter or darker than it went in. Clamping at the border means this holds at the edges
    // too, which blurring against black would not.
    const Array2Df flat = constant(int2{16, 12}, 0.37f);

    for (const Array2Df &out : {gaussian_blurred(flat, whole(flat), 3.f, 3.f), box_blurred(flat, whole(flat), 2, 2)})
        for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == doctest::Approx(0.37f).epsilon(1e-5));
}

TEST_CASE("A blur conserves what it spreads")
{
    // An impulse well away from the border: every tap lands inside the image, so the total is unchanged.
    const Array2Df src = impulse(int2{64, 64});

    CHECK(total(gaussian_blurred(src, whole(src), 2.f, 2.f)) == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(total(box_blurred(src, whole(src), 3, 3)) == doctest::Approx(1.0).epsilon(1e-4));
}

TEST_CASE("A blur spreads an impulse symmetrically about where it was")
{
    const Array2Df out = gaussian_blurred(impulse(int2{33, 33}), Box2i{int2{0}, int2{33, 33}}, 2.f, 2.f);
    const int      c   = 16;

    // The peak stays put, and opposite neighbors match -- an off-center kernel would break either.
    CHECK(out(c, c) > out(c + 1, c));
    CHECK(out(c + 1, c) == doctest::Approx(out(c - 1, c)));
    CHECK(out(c, c + 1) == doctest::Approx(out(c, c - 1)));
    CHECK(out(c + 1, c) == doctest::Approx(out(c, c + 1)));
}

TEST_CASE("A sigma of zero along an axis leaves that axis alone")
{
    Array2Df src{int2{9, 9}};
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) src(x, y) = float(x);

    // Blurring only vertically cannot change a pattern that varies only horizontally.
    const Array2Df out = gaussian_blurred(src, whole(src), 0.f, 3.f);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) CHECK(out(x, y) == doctest::Approx(float(x)));
}

TEST_CASE("A wider blur flattens more than a narrower one")
{
    const Array2Df src   = impulse(int2{64, 64});
    const Array2Df tight = gaussian_blurred(src, whole(src), 1.f, 1.f);
    const Array2Df wide  = gaussian_blurred(src, whole(src), 8.f, 8.f);

    CHECK(tight(32, 32) > wide(32, 32));
}

TEST_CASE("Unsharp masking with no amount changes nothing")
{
    Array2Df src{int2{16, 16}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i % 7) * 0.1f;

    const Array2Df out = unsharp_masked(src, whole(src), 2.f, 0.f);
    for (int i = 0; i < src.num_elements(); ++i) CHECK(out(i) == doctest::Approx(src(i)));
}

TEST_CASE("Unsharp masking increases the contrast of an edge")
{
    // A step, which is what sharpening is meant to act on: the sample just inside the bright side should
    // overshoot above it, and the one just inside the dark side undershoot below.
    Array2Df src{int2{16, 1}};
    for (int x = 0; x < 16; ++x) src(x, 0) = x < 8 ? 0.f : 1.f;

    const Array2Df out = unsharp_masked(src, whole(src), 2.f, 1.f);

    CHECK(out(8, 0) > 1.f);
    CHECK(out(7, 0) < 0.f);
}

TEST_CASE("Filtering a region gives exactly what filtering everything would have")
{
    // The point of taking a region: a selection costs the selection, not the image. That is only a saving
    // if the answer is the same one, so every filter is run both ways and compared sample for sample.
    Array2Df src{int2{40, 30}};
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x) src(x, y) = std::sin(0.3f * x) * std::cos(0.2f * y);

    // Deliberately touching neither edge, so the region has real samples on all four sides -- the case
    // where reading beyond it matters and a naive implementation would pad instead.
    const Box2i region{{7, 5}, {23, 19}};
    const int2  extent = region.size();

    const Array2Df all_g = gaussian_blurred(src, whole(src), 2.5f, 1.5f);
    const Array2Df sub_g = gaussian_blurred(src, region, 2.5f, 1.5f);

    const Array2Df all_b = box_blurred(src, whole(src), 3, 2);
    const Array2Df sub_b = box_blurred(src, region, 3, 2);

    const Array2Df all_u = unsharp_masked(src, whole(src), 2.f, 1.5f);
    const Array2Df sub_u = unsharp_masked(src, region, 2.f, 1.5f);

    REQUIRE(sub_g.size() == extent);
    REQUIRE(sub_b.size() == extent);
    REQUIRE(sub_u.size() == extent);

    for (int y = 0; y < extent.y; ++y)
        for (int x = 0; x < extent.x; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            const int sx = region.min.x + x, sy = region.min.y + y;
            CHECK(sub_g(x, y) == doctest::Approx(all_g(sx, sy)));
            CHECK(sub_b(x, y) == doctest::Approx(all_b(sx, sy)));
            CHECK(sub_u(x, y) == doctest::Approx(all_u(sx, sy)));
        }
}

TEST_CASE("A region against the image's edge still clamps rather than reading past it")
{
    Array2Df src{int2{20, 16}};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 20; ++x) src(x, y) = float(x + y);

    // Cornered, so the kernel hangs off two sides and the clamping is what fills it.
    const Box2i    region{{0, 0}, {5, 4}};
    const Array2Df all = gaussian_blurred(src, whole(src), 3.f, 3.f);
    const Array2Df sub = gaussian_blurred(src, region, 3.f, 3.f);

    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(sub(x, y) == doctest::Approx(all(x, y)));
        }
}

namespace
{

//! A direct, obviously-correct box average, for the sliding window to be checked against.
float box_reference(const Array2Df &a, int x, int y, int rx, int ry)
{
    float sum = 0.f;
    for (int dy = -ry; dy <= ry; ++dy)
        for (int dx = -rx; dx <= rx; ++dx)
            sum += a(std::clamp(x + dx, 0, a.width() - 1), std::clamp(y + dy, 0, a.height() - 1));
    return sum / float((2 * rx + 1) * (2 * ry + 1));
}

//! Variance of a kernel about its center, which is what the Gaussian approximations are matched on.
double kernel_variance(const Array2Df &k, int2 center)
{
    double total = 0.0, moment = 0.0;
    for (int y = 0; y < k.height(); ++y)
        for (int x = 0; x < k.width(); ++x)
        {
            const double w = k(x, y);
            total += w;
            moment += w * double(x - center.x) * double(x - center.x);
        }
    return moment / total;
}

} // namespace

TEST_CASE("The sliding-window box matches a direct box average")
{
    // The running sum is the whole reason box blur is worth having, and also the easiest thing to get
    // subtly wrong at the ends of each line -- so it is checked against the definition.
    Array2Df src{int2{24, 18}};
    for (int y = 0; y < 18; ++y)
        for (int x = 0; x < 24; ++x) src(x, y) = float((x * 7 + y * 13) % 11) * 0.1f;

    for (int2 r : {int2{1, 1}, int2{3, 2}, int2{0, 4}, int2{5, 0}})
    {
        CAPTURE(r.x);
        CAPTURE(r.y);
        const Array2Df out = box_blurred(src, whole(src), r.x, r.y, 1);
        for (int y = 0; y < 18; ++y)
            for (int x = 0; x < 24; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(out(x, y) == doctest::Approx(box_reference(src, x, y, r.x, r.y)));
            }
    }
}

TEST_CASE("Repeating a box blur is the same as blurring the result again")
{
    // What "iterations" has to mean for the box mode: n passes, each reading the last one's output.
    Array2Df src{int2{20, 16}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i % 5);

    const Array2Df once   = box_blurred(src, whole(src), 2, 2, 1);
    const Array2Df twice  = box_blurred(src, whole(src), 2, 2, 2);
    const Array2Df manual = box_blurred(once, whole(once), 2, 2, 1);

    for (int i = 0; i < twice.num_elements(); ++i) CHECK(twice(i) == doctest::Approx(manual(i)));
}

TEST_CASE("Iterating a fast Gaussian changes its shape, not its width")
{
    // The property the whole interface rests on: quality is separate from amount, so raising it does not
    // force the blur to be re-tuned. Measured as the variance of the kernel the filter produces.
    const int2 size{129, 1};
    const int2 center{64, 0};

    Array2Df impulse_src{size};
    impulse_src(center.x, 0) = 1.f;

    const float sigma = 6.f;
    for (int n : {2, 4, 6, 10})
    {
        CAPTURE(n);
        const Array2Df k = fast_gaussian_blurred(impulse_src, Box2i{int2{0}, size}, sigma, 0.f, n);
        // Rounding the box width to an odd integer means this cannot be exact, but it stays close.
        CHECK(std::sqrt(kernel_variance(k, center)) == doctest::Approx(sigma).epsilon(0.06));
    }
}

TEST_CASE("A fast Gaussian approaches the real one as it iterates")
{
    const int2 size{129, 1};
    const int2 center{64, 0};

    Array2Df impulse_src{size};
    impulse_src(center.x, 0) = 1.f;

    const Box2i    all   = Box2i{int2{0}, size};
    const Array2Df exact = gaussian_blurred(impulse_src, all, 6.f, 0.f);

    auto error_against_exact = [&](int n)
    {
        const Array2Df k = fast_gaussian_blurred(impulse_src, all, 6.f, 0.f, n);
        double         e = 0.0;
        for (int x = 0; x < size.x; ++x) e += std::abs(double(k(x, 0)) - double(exact(x, 0)));
        return e;
    };

    // One box is a long way off; a few converge. Not monotonic at every step -- the box width is rounded
    // to an odd integer, so a given count can land better than the next one up -- but the trend is clear
    // across a wide enough gap.
    CHECK(error_against_exact(6) < error_against_exact(1));
    CHECK(error_against_exact(12) < error_against_exact(2));
}

TEST_CASE("An iterated box over a region agrees with the same over the whole image")
{
    // Each pass reads beyond what it produces, so a chain of them has to grow the region once per pass --
    // and clip to the image, or the clamping at the edges stops matching.
    Array2Df src{int2{48, 36}};
    for (int y = 0; y < 36; ++y)
        for (int x = 0; x < 48; ++x) src(x, y) = std::sin(0.4f * x) * std::cos(0.3f * y);

    for (const Box2i &region : {Box2i{{9, 7}, {26, 22}}, Box2i{{0, 0}, {6, 5}}})
    {
        const Array2Df all = box_blurred(src, whole(src), 2, 3, 4);
        const Array2Df sub = box_blurred(src, region, 2, 3, 4);

        REQUIRE(sub.size() == region.size());
        for (int y = 0; y < region.size().y; ++y)
            for (int x = 0; x < region.size().x; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(sub(x, y) == doctest::Approx(all(region.min.x + x, region.min.y + y)));
            }
    }
}

TEST_CASE("A median removes a lone outlier where a mean only spreads it")
{
    // The reason to have a median at all: one wild sample -- a firefly -- should vanish rather than be
    // smeared over its neighbors.
    Array2Df src{int2{9, 9}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = 0.5f;
    src(4, 4) = 1000.f;

    const Array2Df med = median_filtered(src, whole(src), 2.f);
    const Array2Df avg = gaussian_blurred(src, whole(src), 2.f, 2.f);

    // Gone entirely, and its neighbors untouched.
    CHECK(med(4, 4) == doctest::Approx(0.5f));
    CHECK(med(5, 4) == doctest::Approx(0.5f));

    // Whereas the blur has pushed it outwards instead.
    CHECK(avg(4, 4) > 0.6f);
    CHECK(avg(5, 4) > 0.6f);
}

TEST_CASE("A median leaves a constant image alone")
{
    Array2Df flat{int2{12, 10}};
    for (int i = 0; i < flat.num_elements(); ++i) flat(i) = 0.25f;

    const Array2Df out = median_filtered(flat, whole(flat), 3.f);
    for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == doctest::Approx(0.25f));
}

TEST_CASE("A median keeps an edge that a blur would soften")
{
    // A median is an order statistic, so on either side of a step it still picks a sample from that side.
    Array2Df src{int2{16, 16}};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) src(x, y) = x < 8 ? 0.f : 1.f;

    const Array2Df med = median_filtered(src, whole(src), 2.f);

    // Two samples clear of the step, the sides are still exactly what they were.
    CHECK(med(5, 8) == doctest::Approx(0.f));
    CHECK(med(10, 8) == doctest::Approx(1.f));
}

TEST_CASE("A median over a region agrees with the same over the whole image")
{
    Array2Df src{int2{30, 24}};
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24 && x < 30; ++x) src(x, y) = float((x * 5 + y * 3) % 7);

    const Box2i    region{{6, 5}, {19, 16}};
    const Array2Df all = median_filtered(src, whole(src), 2.f);
    const Array2Df sub = median_filtered(src, region, 2.f);

    REQUIRE(sub.size() == region.size());
    for (int y = 0; y < region.size().y; ++y)
        for (int x = 0; x < region.size().x; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(sub(x, y) == doctest::Approx(all(region.min.x + x, region.min.y + y)));
        }
}

TEST_CASE("A radius of zero leaves every sample as it was")
{
    Array2Df src{int2{8, 6}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i);

    const Array2Df out = median_filtered(src, whole(src), 0.f);
    for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == doctest::Approx(src(i)));
}

TEST_CASE("A canceled filter stops early and reports that it did")
{
    Array2Df src{int2{64, 64}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i % 13);

    // Already canceled before it starts, which is the case the caller has to notice: what comes back is a
    // partial answer and must be discarded rather than applied.
    FilterProgress progress;
    progress.canceled.store(true);

    const Array2Df out = median_filtered(src, whole(src), 4.f, &progress);

    CHECK(progress.stop());
    CHECK(out.size() == src.size()); // still the right shape, just not filled in
}

TEST_CASE("An uncancelled filter reports its way to complete")
{
    Array2Df src{int2{16, 16}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i % 5);

    FilterProgress progress;
    median_filtered(src, whole(src), 2.f, &progress);

    CHECK_FALSE(progress.stop());
    CHECK(progress.fraction.load() == doctest::Approx(1.f));
}
