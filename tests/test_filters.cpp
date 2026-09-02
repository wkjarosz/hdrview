//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/filters.h"

#include <algorithm>
#include <cmath>
#include <limits>

//! The whole of \p a, for the cases that are about the filter and not the region.
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
    // clamping at the border makes this hold at the edges too, which blurring against black would not
    const Array2Df flat = constant(int2{16, 12}, 0.37f);

    for (const Array2Df &out : {gaussian_blurred(flat, whole(flat), 3.f, 3.f), box_blurred(flat, whole(flat), 2, 2)})
        for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == doctest::Approx(0.37f).epsilon(1e-5));
}

TEST_CASE("A blur conserves what it spreads")
{
    // an impulse well away from the border, so every tap lands inside the image
    const Array2Df src = impulse(int2{64, 64});

    CHECK(total(gaussian_blurred(src, whole(src), 2.f, 2.f)) == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(total(box_blurred(src, whole(src), 3, 3)) == doctest::Approx(1.0).epsilon(1e-4));
}

TEST_CASE("A blur spreads an impulse symmetrically about where it was")
{
    const Array2Df out = gaussian_blurred(impulse(int2{33, 33}), Box2i{int2{0}, int2{33, 33}}, 2.f, 2.f);
    const int      c   = 16;

    // the peak stays put and opposite neighbors match; an off-center kernel would break either
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

    // blurring only vertically cannot change a pattern that varies only horizontally
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
    // on a step, the sample just inside the bright side overshoots above it and the one just inside the dark
    // side undershoots below
    Array2Df src{int2{16, 1}};
    for (int x = 0; x < 16; ++x) src(x, 0) = x < 8 ? 0.f : 1.f;

    const Array2Df out = unsharp_masked(src, whole(src), 2.f, 1.f);

    CHECK(out(8, 0) > 1.f);
    CHECK(out(7, 0) < 0.f);
}

TEST_CASE("Filtering a region gives exactly what filtering everything would have")
{
    // every filter is run both ways and compared sample for sample
    Array2Df src{int2{40, 30}};
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x) src(x, y) = std::sin(0.3f * x) * std::cos(0.2f * y);

    // touching neither edge, so the region has real samples on all four sides and reading beyond it matters
    const Box2i region{{7, 5}, {23, 19}};
    const int2  extent = region.size();

    const Array2Df all_g = gaussian_blurred(src, whole(src), 2.5f, 1.5f);
    const Array2Df sub_g = gaussian_blurred(src, region, 2.5f, 1.5f);

    const Array2Df all_b = box_blurred(src, whole(src), 3, 2);
    const Array2Df sub_b = box_blurred(src, region, 3, 2);

    const Array2Df all_u = unsharp_masked(src, whole(src), 2.f, 1.5f);
    const Array2Df sub_u = unsharp_masked(src, region, 2.f, 1.5f);

    // fractional, so the shift reads between samples and a region with the wrong coordinates lands on a
    // different phase, not just in the wrong place
    const Array2Df all_s = shifted(src, whole(src), 3.5f, -2.25f, Sampler_Bicubic, BorderMode_Repeat);
    const Array2Df sub_s = shifted(src, region, 3.5f, -2.25f, Sampler_Bicubic, BorderMode_Repeat);

    REQUIRE(sub_g.size() == extent);
    REQUIRE(sub_b.size() == extent);
    REQUIRE(sub_u.size() == extent);
    REQUIRE(sub_s.size() == extent);

    for (int y = 0; y < extent.y; ++y)
        for (int x = 0; x < extent.x; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            const int sx = region.min.x + x, sy = region.min.y + y;
            CHECK(sub_g(x, y) == doctest::Approx(all_g(sx, sy)));
            CHECK(sub_b(x, y) == doctest::Approx(all_b(sx, sy)));
            CHECK(sub_u(x, y) == doctest::Approx(all_u(sx, sy)));
            CHECK(sub_s(x, y) == doctest::Approx(all_s(sx, sy)));
        }
}

TEST_CASE("A region against the image's edge still clamps rather than reading past it")
{
    Array2Df src{int2{20, 16}};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 20; ++x) src(x, y) = float(x + y);

    // cornered, so the kernel hangs off two sides and the clamping fills it
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

//! A direct box average, for the sliding window to be checked against.
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
    // the running sum is easiest to get wrong at the ends of each line, so it is checked against the definition
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
    // "iterations" means n passes, each reading the last one's output
    Array2Df src{int2{20, 16}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i % 5);

    const Array2Df once   = box_blurred(src, whole(src), 2, 2, 1);
    const Array2Df twice  = box_blurred(src, whole(src), 2, 2, 2);
    const Array2Df manual = box_blurred(once, whole(once), 2, 2, 1);

    for (int i = 0; i < twice.num_elements(); ++i) CHECK(twice(i) == doctest::Approx(manual(i)));
}

TEST_CASE("Iterating a fast Gaussian changes its shape, not its width")
{
    // quality is separate from amount, so raising it does not force the blur to be re-tuned. Measured as the
    // variance of the kernel the filter produces.
    const int2 size{129, 1};
    const int2 center{64, 0};

    Array2Df impulse_src{size};
    impulse_src(center.x, 0) = 1.f;

    const float sigma = 6.f;
    for (int n : {2, 4, 6, 10})
    {
        CAPTURE(n);
        const Array2Df k = fast_gaussian_blurred(impulse_src, Box2i{int2{0}, size}, sigma, 0.f, n);
        // rounding the box width to an odd integer keeps this from being exact
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

    // One box is a long way off; a few converge. Not monotonic at every step, since the box width is rounded
    // to an odd integer and a given count can land better than the next one up.
    CHECK(error_against_exact(6) < error_against_exact(1));
    CHECK(error_against_exact(12) < error_against_exact(2));
}

TEST_CASE("An iterated box over a region agrees with the same over the whole image")
{
    // each pass reads beyond what it produces, so a chain of them grows the region once per pass, and clips
    // to the image or the clamping at the edges stops matching
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

namespace
{

//! Where a coordinate lands under one border mode, written independently of the filter's own version.
int expected_wrap(int p, int extent, int mode)
{
    if (p >= 0 && p < extent)
        return p;

    switch (mode)
    {
    case BorderMode_Edge: return std::clamp(p, 0, extent - 1);
    case BorderMode_Repeat: return ((p % extent) + extent) % extent;
    case BorderMode_Mirror:
    {
        // reflected at each repeat: the period is twice the extent and folds in the middle
        const int period = 2 * extent;
        int       q      = ((p % period) + period) % period;
        return q < extent ? q : period - 1 - q;
    }
    default: return -1; // BorderMode_Black: nothing there
    }
}

Array2Df ramp(int2 size)
{
    Array2Df a{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) a(x, y) = 0.5f + 0.25f * float(x) - 0.125f * float(y);
    return a;
}

Array2Df noise(int2 size)
{
    Array2Df a{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) a(x, y) = std::sin(1.7f * x + 0.3f) * std::cos(2.1f * y - 0.4f);
    return a;
}

} // namespace

TEST_CASE("A whole-sample shift moves samples without touching their values")
{
    // A whole-sample shift is a permutation of the samples, not a filter over them, so no sampler may leave a
    // fingerprint. Swept over all three, since the sampler is not consulted when the offset is whole.
    const Array2Df src = noise(int2{17, 13});

    for (int sampler = 0; sampler < Sampler_COUNT; ++sampler)
        for (int border = 0; border < BorderMode_COUNT; ++border)
            // beyond the image as well as within it, and negative, since wrapping a large offset is where a
            // modulus with the wrong sign shows up
            for (int2 d : {int2{0, 0}, int2{3, 0}, int2{0, -2}, int2{5, 7}, int2{-9, -11}, int2{40, -31}})
            {
                CAPTURE(sampler);
                CAPTURE(border);
                CAPTURE(d.x);
                CAPTURE(d.y);

                const Array2Df out = shifted(src, whole(src), float(d.x), float(d.y), sampler, border, border);

                for (int y = 0; y < src.height(); ++y)
                    for (int x = 0; x < src.width(); ++x)
                    {
                        const int   sx   = expected_wrap(x - d.x, src.width(), border);
                        const int   sy   = expected_wrap(y - d.y, src.height(), border);
                        const float want = (sx < 0 || sy < 0) ? 0.f : src(sx, sy);
                        REQUIRE(out(x, y) == doctest::Approx(want));
                    }
            }
}

TEST_CASE("Wrapping makes a shift reversible, and a shift by the whole image nothing at all")
{
    // sliding a tiling texture to bring its seam into view has to be undoable by sliding back, with no sample
    // lost off the edge in between
    const Array2Df src = noise(int2{16, 12});

    for (int sampler = 0; sampler < Sampler_COUNT; ++sampler)
    {
        CAPTURE(sampler);

        const Array2Df there = shifted(src, whole(src), 5.f, -3.f, sampler, BorderMode_Repeat, BorderMode_Repeat);
        const Array2Df back  = shifted(there, whole(there), -5.f, 3.f, sampler, BorderMode_Repeat, BorderMode_Repeat);

        // a whole turn around the image, which wrapping makes indistinguishable from standing still
        const Array2Df turn = shifted(src, whole(src), float(src.width()), float(-2 * src.height()), sampler,
                                      BorderMode_Repeat, BorderMode_Repeat);

        for (int i = 0; i < src.num_elements(); ++i)
        {
            REQUIRE(back(i) == doctest::Approx(src(i)));
            REQUIRE(turn(i) == doctest::Approx(src(i)));
        }
    }
}

TEST_CASE("A fractional shift reconstructs, rather than snapping to a sample")
{
    // Two things every sampler owes: a constant image survives, or the taps do not sum to one and the result
    // drifts as it moves; and an offset landing between two samples does not round to one of them.
    const Array2Df flat = constant(int2{12, 10}, 0.375f);
    const Array2Df src  = noise(int2{24, 18});

    for (int sampler = 0; sampler < Sampler_COUNT; ++sampler)
        for (int border : {BorderMode_Edge, BorderMode_Repeat, BorderMode_Mirror})
        {
            CAPTURE(sampler);
            CAPTURE(border);

            const Array2Df c = shifted(flat, whole(flat), 0.5f, -0.25f, sampler, border, border);
            for (int i = 0; i < c.num_elements(); ++i) REQUIRE(c(i) == doctest::Approx(0.375f));

            // bracketing is bilinear's promise alone: bicubic's negative lobes overshoot to keep an edge
            // sharp, and its accuracy is measured on a ramp below
            if (sampler != Sampler_Bilinear)
                continue;

            // half a sample across, away from the edges so the border mode does not enter into it
            const Array2Df half = shifted(src, whole(src), 0.5f, 0.f, sampler, border, border);
            for (int y = 2; y < src.height() - 2; ++y)
                for (int x = 2; x < src.width() - 2; ++x)
                {
                    CAPTURE(x);
                    CAPTURE(y);

                    // halfway between the two samples it sits between, which rounding to either would not give
                    REQUIRE(half(x, y) == doctest::Approx(0.5f * (src(x - 1, y) + src(x, y))));
                }
        }
}

TEST_CASE("A ramp lands where the shift says, rather than half a sample off")
{
    // On a ramp the shifted image is the ramp itself, evaluated where the shift moved it, so half a sample of
    // misalignment shows up as an offset of half the slope. Bilinear reproduces a linear function exactly;
    // cubic convolution does so only at a = -1/2, and this is Photoshop's a = -3/4, so it is held to close.
    const Array2Df src = ramp(int2{32, 24});

    for (int sampler : {Sampler_Bilinear, Sampler_Bicubic})
        for (float2 d : {float2{2.5f, 0.f}, float2{0.f, -1.75f}, float2{3.25f, 4.5f}})
        {
            CAPTURE(sampler);
            CAPTURE(d.x);
            CAPTURE(d.y);

            const Array2Df out = shifted(src, whole(src), d.x, d.y, sampler, BorderMode_Edge, BorderMode_Edge);

            // half a sample of misalignment moves a ramp of this slope by at least 0.0625
            const float tolerance = sampler == Sampler_Bilinear ? 1e-4f : 0.02f;

            // inside by the widest kernel's reach, past which the edge clamping flattens the ramp
            for (int y = 6; y < src.height() - 6; ++y)
                for (int x = 6; x < src.width() - 6; ++x)
                {
                    const float want = 0.5f + 0.25f * (float(x) - d.x) - 0.125f * (float(y) - d.y);
                    CAPTURE(x);
                    CAPTURE(y);
                    REQUIRE(std::abs(out(x, y) - want) < tolerance);
                }
        }
}

TEST_CASE("Each border mode fills the exposed strip with what it says it does")
{
    // the four differ only where the shift reaches past the edge, so a column each on an image whose columns
    // are all distinct tells them apart
    Array2Df src{int2{6, 4}};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x) src(x, y) = float(x + 1);

    // two to the right: the first two columns are outside, and what appears there is the mode's answer
    auto column = [&](int mode, int x)
    { return shifted(src, whole(src), 2.f, 0.f, Sampler_Nearest, mode, mode)(x, 1); };

    CHECK(column(BorderMode_Black, 0) == doctest::Approx(0.f));
    CHECK(column(BorderMode_Black, 1) == doctest::Approx(0.f));

    // the leftmost sample, extended outward
    CHECK(column(BorderMode_Edge, 0) == doctest::Approx(1.f));
    CHECK(column(BorderMode_Edge, 1) == doctest::Approx(1.f));

    // what went off the right side comes back
    CHECK(column(BorderMode_Repeat, 0) == doctest::Approx(5.f));
    CHECK(column(BorderMode_Repeat, 1) == doctest::Approx(6.f));

    // reflected at the edge, so the sample next to it repeats instead of jumping to the far side
    CHECK(column(BorderMode_Mirror, 0) == doctest::Approx(2.f));
    CHECK(column(BorderMode_Mirror, 1) == doctest::Approx(1.f));
}

TEST_CASE("The two axes take their border modes independently")
{
    // a lat-long environment map wraps in longitude and not in latitude
    Array2Df src{int2{4, 4}};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) src(x, y) = float(x) + 10.f * float(y);

    const Array2Df out = shifted(src, whole(src), 1.f, 1.f, Sampler_Nearest, BorderMode_Repeat, BorderMode_Black);

    // across: the last column has come round to the first; down: nothing came from above
    CHECK(out(0, 1) == doctest::Approx(3.f));
    CHECK(out(0, 0) == doctest::Approx(0.f));
    CHECK(out(2, 0) == doctest::Approx(0.f));
}

TEST_CASE("A median removes a lone outlier where a mean only spreads it")
{
    // one wild sample, a firefly, vanishes instead of being smeared over its neighbors
    Array2Df src{int2{9, 9}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = 0.5f;
    src(4, 4) = 1000.f;

    const Array2Df med = median_filtered(src, whole(src), 2.f);
    const Array2Df avg = gaussian_blurred(src, whole(src), 2.f, 2.f);

    // gone entirely, and its neighbors untouched
    CHECK(med(4, 4) == doctest::Approx(0.5f));
    CHECK(med(5, 4) == doctest::Approx(0.5f));

    // whereas the blur has pushed it outwards
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
    // a median is an order statistic, so on either side of a step it picks a sample from that side
    Array2Df src{int2{16, 16}};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) src(x, y) = x < 8 ? 0.f : 1.f;

    const Array2Df med = median_filtered(src, whole(src), 2.f);

    // two samples clear of the step, the sides are still what they were
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

    // already canceled before it starts, so what comes back is a partial answer the caller must discard
    AtomicProgress progress{true};
    progress.cancel();

    const Array2Df out = median_filtered(src, whole(src), 4.f, true, progress);

    CHECK(progress.canceled());
    CHECK(out.size() == src.size()); // still the right shape, just not filled in
}

TEST_CASE("An uncancelled filter reports its way to complete")
{
    Array2Df src{int2{16, 16}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i % 5);

    AtomicProgress progress{true};
    median_filtered(src, whole(src), 2.f, true, progress);

    CHECK_FALSE(progress.canceled());
    CHECK(progress.progress() == doctest::Approx(1.f).epsilon(0.001));
}

TEST_CASE("Zapping a gremlin fills it from its neighbors")
{
    // what goes back has to agree with the surroundings, so a firefly in a smooth region leaves no trace
    Array2Df src{int2{9, 9}};
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) src(x, y) = 0.4f;
    src(4, 4) = std::numeric_limits<float>::quiet_NaN();
    src(2, 6) = std::numeric_limits<float>::infinity();

    const Array2Df out = zapped_gremlins(src, whole(src));

    CHECK(out(4, 4) == doctest::Approx(0.4f));
    CHECK(out(2, 6) == doctest::Approx(0.4f));
}

TEST_CASE("Zapping leaves every finite sample exactly as it was")
{
    // including extreme ones: a very bright highlight is data, not a gremlin
    Array2Df src{int2{8, 8}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i) * 0.5f;
    src(3, 3) = 1e30f;
    src(5, 5) = -1e30f;

    const Array2Df out = zapped_gremlins(src, whole(src));
    for (int i = 0; i < src.num_elements(); ++i) CHECK(out(i) == src(i));
}

TEST_CASE("A gremlin with no finite neighbor falls back to the replacement")
{
    // in the middle of a run of them there is nothing to take a median of
    Array2Df src{int2{5, 5}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = std::numeric_limits<float>::quiet_NaN();

    const Array2Df out = zapped_gremlins(src, whole(src), -1.f);
    for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == -1.f);
}

TEST_CASE("Zapping takes the median rather than the mean of the ring")
{
    // a mean would be dragged by an outlier among the neighbors; the median is not
    Array2Df src{int2{3, 3}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = 1.f;
    src(0, 0) = 1000.f;                                  // one wild but finite neighbor
    src(1, 1) = std::numeric_limits<float>::quiet_NaN(); // the gremlin, ringed by the other eight

    const Array2Df out = zapped_gremlins(src, whole(src));
    CHECK(out(1, 1) == doctest::Approx(1.f));
}

TEST_CASE("The median window's shape changes what it sees")
{
    // A square window reaches root-two farther at its corners than along its axes, so near a diagonal edge it
    // takes in samples the inscribed circle does not. Not a smooth or symmetric field: both windows are
    // symmetric about their center, so anything with that symmetry makes the two agree.
    Array2Df src{int2{21, 21}};
    for (int y = 0; y < 21; ++y)
        for (int x = 0; x < 21; ++x)
        {
            const unsigned h = unsigned(x) * 73856093u ^ unsigned(y) * 19349663u;
            src(x, y)        = float(h % 1024u) / 1024.f;
        }

    const Array2Df disc   = median_filtered(src, whole(src), 3.f, true);
    const Array2Df square = median_filtered(src, whole(src), 3.f, false);

    bool any_different = false;
    for (int i = 0; i < disc.num_elements() && !any_different; ++i) any_different = disc(i) != square(i);
    CHECK(any_different);
}

TEST_CASE("Both median window shapes remove a lone outlier")
{
    Array2Df src{int2{21, 21}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = 0.2f;
    src(10, 10) = 50.f;

    CHECK(median_filtered(src, whole(src), 3.f, true)(10, 10) == doctest::Approx(0.2f));
    CHECK(median_filtered(src, whole(src), 3.f, false)(10, 10) == doctest::Approx(0.2f));
}
