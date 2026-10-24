//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/filters.h"
#include "edit/poisson.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

/// The whole of \p a, for the cases that are about the filter and not the region.
static Box2i whole(const Array2Df &a) { return Box2i{int2{0}, a.size()}; }

namespace
{

Array2Df constant(int2 size, float v)
{
    Array2Df a{size};
    for (int i = 0; i < a.num_elements(); ++i) a(i) = v;
    return a;
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

float max_difference(const Array2Df &a, const Array2Df &b)
{
    float worst = 0.f;
    for (int i = 0; i < a.num_elements(); ++i) worst = std::max(worst, std::abs(a(i) - b(i)));
    return worst;
}

} // namespace

TEST_CASE("Every filter leaves a constant image alone")
{
    // Clamping at the border makes this hold at the edges too, which filtering against black would not. A
    // filter whose weights do not sum to one drifts off the constant, and a resampling one that reconstructs
    // between samples has to keep it there as well.
    const Array2Df flat = constant(int2{16, 12}, 0.37f);
    const Box2i    all  = whole(flat);

    auto is_flat = [](const char *what, const Array2Df &out)
    {
        CAPTURE(what);
        for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == doctest::Approx(0.37f).epsilon(1e-5));
    };

    is_flat("gaussian", gaussian_blurred(flat, all, 3.f, 3.f));
    is_flat("fast gaussian", fast_gaussian_blurred(flat, all, 3.f, 3.f));
    is_flat("box", box_blurred(flat, all, 2, 2));
    is_flat("iterated box", box_blurred(flat, all, 2, 2, 4));
    is_flat("unsharp masked", unsharp_masked(flat, all, 2.f, 1.5f));
    is_flat("median over a disc", median_filtered(flat, all, 3.f, true));
    is_flat("median over a square", median_filtered(flat, all, 3.f, false));
    is_flat("zapped", zapped_gremlins(flat, all));

    // a fractional shift, so every sampler reconstructs between samples instead of picking one
    for (int sampler = 0; sampler < Sampler_COUNT; ++sampler)
        for (int border : {BorderMode_Edge, BorderMode_Repeat, BorderMode_Mirror})
        {
            CAPTURE(sampler);
            CAPTURE(border);
            is_flat("shifted", shifted(flat, all, 0.5f, -0.25f, sampler, border, border));
        }
}

TEST_CASE("A blur's impulse response is centered, symmetric, conserves its mass, and is as wide as asked")
{
    // An impulse well away from the border, so every tap lands inside the image, blurred along one axis at a
    // time: the other axis then has to stay a single row or column, with no variance at all. Each kernel's
    // variance is known in closed form: a box of half-width r is the discrete uniform on [-r, r], of variance
    // r(r+1)/3, and n passes of it add up to n times that. Long along the blurred axis, and a few samples
    // across the other, which is all it takes to see one spread there.
    struct Case
    {
        std::string                                     what;
        std::function<Array2Df(const Array2Df &, bool)> blur; ///< blurs across when the flag is set, else down
        double                                          variance, tolerance;
    };
    std::vector<Case> cases;

    // truncated at three sigma and renormalized, which takes up to about two percent off the variance
    for (float s : {1.f, 2.5f, 6.f})
        cases.push_back({"gaussian " + std::to_string(s), [s](const Array2Df &a, bool x)
                         { return gaussian_blurred(a, whole(a), x ? s : 0.f, x ? 0.f : s); }, double(s) * s, 0.03});

    // the box widths are odd integers, which keeps the variance within a few percent of the target
    for (float s : {2.5f, 6.f})
        for (int n : {2, 4, 6, 10})
            cases.push_back(
                {"fast gaussian " + std::to_string(s) + " x" + std::to_string(n), [s, n](const Array2Df &a, bool x)
                 { return fast_gaussian_blurred(a, whole(a), x ? s : 0.f, x ? 0.f : s, n); }, double(s) * s, 0.08});

    for (int r : {1, 3})
        for (int n : {1, 2, 4})
            cases.push_back({"box " + std::to_string(r) + " x" + std::to_string(n), [r, n](const Array2Df &a, bool x)
                             { return box_blurred(a, whole(a), x ? r : 0, x ? 0 : r, n); },
                             double(n) * r * (r + 1) / 3.0, 1e-3});

    for (const auto &c : cases)
        for (bool across : {true, false})
        {
            CAPTURE(c.what);
            CAPTURE(across);

            const int2 size = across ? int2{81, 9} : int2{9, 81};
            const int2 center{size.x / 2, size.y / 2};
            Array2Df   impulse{size};
            impulse(center.x, center.y) = 1.f;

            const Array2Df out = c.blur(impulse, across);
            REQUIRE(out.size() == size);

            double total = 0.0, mean[2] = {0.0, 0.0};
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    total += out(x, y);
                    mean[0] += out(x, y) * double(x);
                    mean[1] += out(x, y) * double(y);
                }
            mean[0] /= total;
            mean[1] /= total;

            double var[2] = {0.0, 0.0};
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    var[0] += out(x, y) * (double(x) - mean[0]) * (double(x) - mean[0]);
                    var[1] += out(x, y) * (double(y) - mean[1]) * (double(y) - mean[1]);
                }
            var[0] /= total;
            var[1] /= total;

            CHECK(total == doctest::Approx(1.0).epsilon(1e-4));
            CHECK(mean[0] == doctest::Approx(center.x).epsilon(1e-5));
            CHECK(mean[1] == doctest::Approx(center.y).epsilon(1e-5));

            const int blurred = across ? 0 : 1;
            CHECK(var[blurred] == doctest::Approx(c.variance).epsilon(c.tolerance));
            CHECK(var[1 - blurred] < 1e-6);

            // mirrored through the center along the line it spread over; a lopsided kernel can keep its mean
            for (int i = 0; i <= center[blurred]; ++i)
            {
                CAPTURE(i);
                const int2 p = across ? int2{i, center.y} : int2{center.x, i};
                const int2 q = 2 * center - p;
                CHECK(std::abs(out(p.x, p.y) - out(q.x, q.y)) < 1e-6f);
            }
        }
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
    // Every filter, run both ways and compared sample for sample, over a region with real samples on every
    // side, where reading beyond it matters, and over one in each corner, where the kernel hangs off two
    // sides and has to clamp against the image's edge rather than the region's. The iterated boxes read
    // beyond what each pass produces, so their chain grows the region once per pass and has to clip it.
    Array2Df src{int2{40, 30}};
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x) src(x, y) = std::sin(0.3f * x) * std::cos(0.2f * y);

    // with gremlins, for the one filter that reads them, scattered so every region has some
    Array2Df gremlins = src;
    for (int y = 0; y < 30; y += 4)
        for (int x = (y / 4) % 3; x < 40; x += 5)
            gremlins(x, y) =
                (x + y) % 2 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();

    using Filter = std::function<Array2Df(const Array2Df &, const Box2i &)>;
    struct Case
    {
        std::string     what;
        Filter          filter;
        const Array2Df *input;
    };
    std::vector<Case> cases = {
        {"gaussian", [](const Array2Df &a, const Box2i &r) { return gaussian_blurred(a, r, 2.5f, 1.5f); }, &src},
        {"fast gaussian", [](const Array2Df &a, const Box2i &r) { return fast_gaussian_blurred(a, r, 3.f, 2.f, 4); },
         &src},
        {"box", [](const Array2Df &a, const Box2i &r) { return box_blurred(a, r, 3, 2); }, &src},
        {"iterated box", [](const Array2Df &a, const Box2i &r) { return box_blurred(a, r, 2, 3, 4); }, &src},
        {"unsharp masked", [](const Array2Df &a, const Box2i &r) { return unsharp_masked(a, r, 2.f, 1.5f); }, &src},
        {"median over a disc", [](const Array2Df &a, const Box2i &r) { return median_filtered(a, r, 2.f, true); },
         &src},
        {"median over a square", [](const Array2Df &a, const Box2i &r) { return median_filtered(a, r, 2.f, false); },
         &src},
        {"zapped", [](const Array2Df &a, const Box2i &r) { return zapped_gremlins(a, r); }, &gremlins},
    };
    // fractional, so the shift reads between samples and a region with the wrong coordinates lands on a
    // different phase, not just in the wrong place; far enough that every border mode is reached
    for (int border = 0; border < BorderMode_COUNT; ++border)
        cases.push_back({"shifted, border " + std::to_string(border), [border](const Array2Df &a, const Box2i &r)
                         { return shifted(a, r, 3.5f, -2.25f, Sampler_Bicubic, border, border); }, &src});

    const Box2i regions[] = {Box2i{{7, 5}, {23, 19}}, Box2i{{0, 0}, {6, 5}}, Box2i{{33, 26}, {40, 30}}};

    for (const auto &c : cases)
    {
        CAPTURE(c.what);
        const Array2Df all = c.filter(*c.input, whole(*c.input));

        for (const Box2i &region : regions)
        {
            CAPTURE(region.min.x);
            CAPTURE(region.min.y);
            const Array2Df sub = c.filter(*c.input, region);

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
}

namespace
{

/// A direct box average, for the sliding window to be checked against.
float box_reference(const Array2Df &a, int x, int y, int rx, int ry)
{
    float sum = 0.f;
    for (int dy = -ry; dy <= ry; ++dy)
        for (int dx = -rx; dx <= rx; ++dx)
            sum += a(std::clamp(x + dx, 0, a.width() - 1), std::clamp(y + dy, 0, a.height() - 1));
    return sum / float((2 * rx + 1) * (2 * ry + 1));
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

namespace
{

/// Where a coordinate lands under one border mode, written independently of the filter's own version.
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

} // namespace

TEST_CASE("A whole-sample shift moves samples without touching their values")
{
    // A whole-sample shift is a permutation of the samples, not a filter over them, so no sampler may leave a
    // fingerprint. Swept over all three, since the sampler is not consulted when the offset is whole, under
    // each border mode; and under every pair of them for one sampler, since the axes take theirs
    // independently: a lat-long environment map wraps in longitude and not in latitude.
    const Array2Df src = noise(int2{17, 13});

    for (int sampler = 0; sampler < Sampler_COUNT; ++sampler)
        for (int border_x = 0; border_x < BorderMode_COUNT; ++border_x)
            for (int border_y = 0; border_y < BorderMode_COUNT; ++border_y)
                // beyond the image as well as within it, and negative, since wrapping a large offset is where a
                // modulus with the wrong sign shows up; and a whole number of turns around the image, which
                // wrapping makes indistinguishable from standing still
                for (int2 d :
                     {int2{0, 0}, int2{3, 0}, int2{0, -2}, int2{5, 7}, int2{-9, -11}, int2{40, -31}, int2{17, -26}})
                {
                    if (border_x != border_y && sampler != Sampler_Bilinear)
                        continue;

                    CAPTURE(sampler);
                    CAPTURE(border_x);
                    CAPTURE(border_y);
                    CAPTURE(d.x);
                    CAPTURE(d.y);

                    const Array2Df out = shifted(src, whole(src), float(d.x), float(d.y), sampler, border_x, border_y);

                    for (int y = 0; y < src.height(); ++y)
                        for (int x = 0; x < src.width(); ++x)
                        {
                            const int   sx   = expected_wrap(x - d.x, src.width(), border_x);
                            const int   sy   = expected_wrap(y - d.y, src.height(), border_y);
                            const float want = (sx < 0 || sy < 0) ? 0.f : src(sx, sy);
                            REQUIRE(out(x, y) == want);
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

TEST_CASE("A median removes a lone outlier and keeps an edge, over either window shape")
{
    // A median is an order statistic: one wild sample, a firefly, vanishes instead of being smeared over its
    // neighbors, and on either side of a step it picks a sample from that side, where a blur would soften it.
    const int2 size{21, 21};

    Array2Df firefly = constant(size, 0.2f);
    firefly(10, 10)  = 50.f;

    Array2Df step{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) step(x, y) = x < 10 ? 0.f : 1.f;

    for (bool disc : {true, false})
    {
        CAPTURE(disc);

        // gone entirely, and its neighbors untouched
        const Array2Df clean = median_filtered(firefly, whole(firefly), 3.f, disc);
        for (int i = 0; i < clean.num_elements(); ++i) CHECK(clean(i) == 0.2f);

        // the step stays where it was, as sharp as it was
        const Array2Df kept = median_filtered(step, whole(step), 3.f, disc);
        for (int i = 0; i < kept.num_elements(); ++i) CHECK(kept(i) == step(i));
    }
}

TEST_CASE("A radius of zero leaves every sample as it was")
{
    Array2Df src{int2{8, 6}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i);

    const Array2Df out = median_filtered(src, whole(src), 0.f);
    for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == doctest::Approx(src(i)));
}

TEST_CASE("Every filter that reports progress runs it to the end, and stops early once canceled")
{
    // Canceled before it starts, a filter returns a partial answer the caller must discard, so on input with
    // something to filter it has to differ from the finished one: a filter ignoring the flag returns that
    // instead. Box chains take two passes or more, since a cancel is read between passes.
    const int2     size{32, 24};
    const Array2Df src = noise(size);

    Array2Df mask{size};
    for (int y = 2; y < size.y - 2; ++y)
        for (int x = 2; x < size.x - 2; ++x) mask(x, y) = 1.f;
    const Array2Df flat = constant(size, 0.5f);

    using Filter = std::function<Array2Df(AtomicProgress)>;
    const struct
    {
        const char *what;
        Filter      filter;
    } cases[] = {
        {"gaussian", [&](AtomicProgress p) { return gaussian_blurred(src, whole(src), 2.f, 3.f, p); }},
        {"box", [&](AtomicProgress p) { return box_blurred(src, whole(src), 2, 1, 3, p); }},
        {"fast gaussian", [&](AtomicProgress p) { return fast_gaussian_blurred(src, whole(src), 2.f, 3.f, 4, p); }},
        {"median over a disc", [&](AtomicProgress p) { return median_filtered(src, whole(src), 2.f, true, p); }},
        {"median over a square", [&](AtomicProgress p) { return median_filtered(src, whole(src), 2.f, false, p); }},
        {"unsharp masked", [&](AtomicProgress p) { return unsharp_masked(src, whole(src), 2.f, 1.5f, p); }},
        {"poisson", [&](AtomicProgress p) { return poisson_blended(flat, src, mask, 500, 1e-6f, p); }},
    };

    for (const auto &c : cases)
    {
        CAPTURE(c.what);

        AtomicProgress finished{true};
        const Array2Df done = c.filter(finished);
        CHECK_FALSE(finished.canceled());
        CHECK(finished.progress() == doctest::Approx(1.f).epsilon(0.001));

        AtomicProgress canceled{true};
        canceled.cancel();
        const Array2Df partial = c.filter(canceled);
        CHECK(partial.size() == done.size()); // still the right shape, just not filled in
        CHECK(max_difference(partial, done) > 1e-3f);
    }
}

TEST_CASE("Zapping replaces exactly the non-finite samples, each with the median of its finite neighbors")
{
    // One image holding every case: a NaN in a smooth area; an infinity beside a wild but finite neighbor,
    // which a mean of the ring would be dragged by and its median is not; finite extremes, which are data
    // and not gremlins; and a 3x3 block of NaNs, whose center has no finite neighbor and takes the
    // replacement, and whose rim takes the median of the finite samples it does reach.
    constexpr float nan  = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf  = std::numeric_limits<float>::infinity();
    constexpr float base = 0.4f, replacement = -1.f;

    Array2Df src = constant(int2{12, 10}, base);
    src(2, 2)    = nan;
    src(6, 2)    = inf;
    src(7, 2)    = 1000.f;
    src(0, 9)    = 1e30f;
    src(11, 8)   = -1e30f;
    for (int y = 5; y < 8; ++y)
        for (int x = 3; x < 6; ++x) src(x, y) = nan;

    const Array2Df out = zapped_gremlins(src, whole(src), replacement);

    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            if (std::isfinite(src(x, y)))
                CHECK(out(x, y) == src(x, y)); // exactly, however extreme
            else if (x == 4 && y == 6)
                CHECK(out(x, y) == replacement);
            else
                CHECK(out(x, y) == base);
        }
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
