//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/poisson.h"

#include <cmath>

namespace
{

/// An iteration bound no solve here comes near, so whatever ends one, it is not the bound.
constexpr int k_unbounded = 100000;

/// A mask covering everything but a border \p margin samples wide, which is what the solve requires.
Array2Df interior_mask(int2 size, int margin = 1)
{
    Array2Df m{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
            m(x, y) = (x >= margin && y >= margin && x < size.x - margin && y < size.y - margin) ? 1.f : 0.f;
    return m;
}

Array2Df filled(int2 size, float v)
{
    Array2Df a{size};
    for (int i = 0; i < a.num_elements(); ++i) a(i) = v;
    return a;
}

/// Values varying in both directions, whose Laplacian is not trivially zero.
Array2Df bumpy(int2 size, float scale = 1.f)
{
    Array2Df a{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) a(x, y) = scale * (std::sin(0.4f * x) * std::cos(0.3f * y) + 0.1f * x);
    return a;
}

float max_difference(const Array2Df &a, const Array2Df &b)
{
    float worst = 0.f;
    for (int i = 0; i < a.num_elements(); ++i) worst = std::max(worst, std::abs(a(i) - b(i)));
    return worst;
}

} // namespace

TEST_CASE("A source with no gradients leaves a smooth patch, symmetric where the setup is")
{
    // With a source that does not vary, the answer is the harmonic function agreeing with the background
    // around the mask. A checkerboard background is what catches a patch landing anywhere but where it was
    // asked to; over a flat one, a misplaced patch looks like a correct one.
    const int2 size{64, 64};
    const int  square = 8;

    Array2Df background{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) background(x, y) = ((x / square + y / square) % 2) ? 1.f : 0.f;

    const Array2Df source = filled(size, 0.5f);

    // centered, so the mask shares the background's symmetries
    Array2Df mask{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) mask(x, y) = (x >= 24 && x < 40 && y >= 24 && y < 40) ? 1.f : 0.f;

    const Array2Df out = poisson_blended(background, source, mask, 300, 1e-6f);

    // A half turn and a reflection through the diagonal leave this checkerboard, the mask and the source
    // alone, so they must leave the answer alone. Confirmed of the background first, or this is vacuous.
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            REQUIRE(background(x, y) == background(size.x - 1 - x, size.y - 1 - y));
            REQUIRE(background(x, y) == background(y, x));

            CHECK(out(x, y) == doctest::Approx(out(size.x - 1 - x, size.y - 1 - y)).epsilon(1e-5));
            CHECK(out(x, y) == doctest::Approx(out(y, x)).epsilon(1e-5));
        }

    // with no gradients asked for, every interior sample is the average of its neighbors, so a step
    // anywhere inside shows up as curvature
    const Array2Df lap = laplacian(out);
    for (int y = 25; y < 39; ++y)
        for (int x = 25; x < 39; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(std::abs(lap(x, y)) < 1e-4f);
        }

    // far enough in, the border's alternation has averaged out to its mean
    CHECK(out(32, 32) == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("The default iteration count is enough for the regions a paste actually covers")
{
    // Measured by how far from harmonic the result is, which a constant source asks for and which needs no
    // reference solve to compare against. The size is about the largest a paste covers in practice.
    const int2 size{256, 256};

    Array2Df background{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) background(x, y) = ((x / 8 + y / 8) % 2) ? 1.f : 0.f;

    const Array2Df source = filled(size, 0.5f);
    const Array2Df mask   = interior_mask(size, 1);

    const Array2Df got = poisson_blended(background, source, mask, 300, 1e-6f);
    const Array2Df lap = laplacian(got);

    float worst = 0.f;
    for (int y = 2; y < size.y - 2; ++y)
        for (int x = 2; x < size.x - 2; ++x) worst = std::max(worst, std::abs(lap(x, y)));

    CAPTURE(worst);
    CHECK(worst < 1e-3f);

    // and a bound no solve comes near gives the same answer to the bit, so the solve stopped on its own and
    // not at the bound
    CHECK(max_difference(got, poisson_blended(background, source, mask, k_unbounded, 1e-6f)) == 0.f);
}

TEST_CASE("The Laplacian of a plane is zero, and of a known quadratic is its known value")
{
    // A linear function has no curvature and x^2 + y^2 has the same curvature everywhere. Checked away from
    // the edges, where clamping takes over.
    const int2 size{16, 16};

    Array2Df plane{size}, quad{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            plane(x, y) = 2.f + 0.5f * float(x) - 0.25f * float(y);
            quad(x, y)  = float(x) * float(x) + float(y) * float(y);
        }

    const Array2Df lap_plane = laplacian(plane);
    const Array2Df lap_quad  = laplacian(quad);

    for (int y = 1; y < size.y - 1; ++y)
        for (int x = 1; x < size.x - 1; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(lap_plane(x, y) == doctest::Approx(0.f).epsilon(1e-4).scale(1.f));

            // eight neighbors: the four along the axes contribute 2+2, and the four diagonals 4+4
            CHECK(lap_quad(x, y) == doctest::Approx(12.f).epsilon(1e-4));
        }
}

TEST_CASE("A seamless paste keeps the background exactly, outside the mask")
{
    // the border is the background's own values, so there is nothing for a seam to appear at
    const int2     size{32, 24};
    const Array2Df background = bumpy(size, 1.f);
    const Array2Df source     = filled(size, 5.f);
    const Array2Df mask       = interior_mask(size, 3);

    const Array2Df out = poisson_blended(background, source, mask);

    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
            if (mask(x, y) <= 0.f)
            {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(out(x, y) == background(x, y));
            }
}

TEST_CASE("The solved interior has the gradients it was given")
{
    // checked away from the mask's own border, where the equation is not imposed
    const int2     size{40, 32};
    const Array2Df background = filled(size, 0.5f);
    const Array2Df source     = bumpy(size, 1.f);
    const Array2Df mask       = interior_mask(size, 3);

    const Array2Df out     = poisson_blended(background, source, mask, 2000, 1e-6f);
    const Array2Df lap_out = laplacian(out);
    const Array2Df lap_src = laplacian(source);

    double worst = 0.0;
    for (int y = 4; y < size.y - 4; ++y)
        for (int x = 4; x < size.x - 4; ++x) worst = std::max(worst, std::abs(double(lap_out(x, y) - lap_src(x, y))));

    CHECK(worst < 1e-2);
}

TEST_CASE("An offset source is carried to the background's level rather than pasted at its own")
{
    // brightening the whole source shifts its values but not its gradients, so the result is unchanged
    const int2     size{32, 32};
    const Array2Df background = filled(size, 0.5f);
    const Array2Df mask       = interior_mask(size, 2);
    const Array2Df source     = bumpy(size, 1.f);

    Array2Df brighter = source;
    for (int i = 0; i < brighter.num_elements(); ++i) brighter(i) += 10.f;

    const Array2Df a = poisson_blended(background, source, mask, 1000, 1e-6f);
    const Array2Df b = poisson_blended(background, brighter, mask, 1000, 1e-6f);

    CHECK(max_difference(a, b) < 1e-2f);

    // and it did do something: the interior is not the background it started from
    CHECK(max_difference(a, background) > 0.05f);
}

TEST_CASE("A solve stops once the residual is small enough, however much further its bound would let it go")
{
    // A source with gradients of its own, so there is something to iterate on rather than an answer to
    // return at once. For each tolerance, the fewest iterations reaching the unbounded solve's answer bit for
    // bit are found by bisection: that is where the unbounded solve stopped. A looser tolerance has to stop
    // it sooner, which nothing but the residual check can do; the solve also stops once its step vanishes,
    // and that alone would land both tolerances on the same count.
    const int2     size{32, 32};
    const Array2Df background = filled(size, 0.5f);
    const Array2Df source     = bumpy(size, 1.f);
    const Array2Df mask       = interior_mask(size, 2);

    int      stopped_at[2] = {0, 0};
    Array2Df answers[2];
    int      i = 0;
    for (float tolerance : {1e-2f, 1e-4f})
    {
        CAPTURE(tolerance);
        answers[i]   = poisson_blended(background, source, mask, k_unbounded, tolerance);
        auto reaches = [&](int n)
        { return max_difference(poisson_blended(background, source, mask, n, tolerance), answers[i]) == 0.f; };

        int lo = 0, hi = 1;
        while (!reaches(hi) && hi < k_unbounded) hi *= 2;
        REQUIRE(reaches(hi));
        while (hi - lo > 1)
        {
            const int mid            = (lo + hi) / 2;
            (reaches(mid) ? hi : lo) = mid;
        }
        CHECK_FALSE(reaches(hi - 1));

        // a few dozen conjugate-gradient steps for a thousand unknowns, far below the bound
        CAPTURE(hi);
        CHECK(hi > 1);
        CHECK(hi < 1000);

        // and what it stopped at is an answer, not the background it started from
        CHECK(max_difference(answers[i], background) > 0.05f);
        stopped_at[i++] = hi;
    }

    CHECK(stopped_at[0] < stopped_at[1]);
    CHECK(max_difference(answers[0], answers[1]) > 0.f);
}

TEST_CASE("Several shares of one job add up to it, whether or not they run to their bound")
{
    // A share that stops early still owes the rest of its share, and a share that finishes must not report
    // the whole job as finished.
    AtomicProgress whole{true};

    SUBCASE("a share that stops short of its bound still fills its share")
    {
        AtomicProgress third{whole, 1.f / 3.f};
        third.set_num_steps(1000);
        for (int i = 0; i < 300; ++i) ++third;

        // three tenths of a third of the job
        CHECK(whole.progress() == doctest::Approx(0.1f).epsilon(0.01));

        third.finish_share();
        CHECK(whole.progress() == doctest::Approx(1.f / 3.f).epsilon(0.01));
    }

    SUBCASE("finishing one share leaves the others still to do")
    {
        AtomicProgress a{whole, 1.f / 3.f}, b{whole, 1.f / 3.f}, c{whole, 1.f / 3.f};

        a.finish_share();
        CHECK(whole.progress() == doctest::Approx(1.f / 3.f).epsilon(0.01));

        b.finish_share();
        CHECK(whole.progress() == doctest::Approx(2.f / 3.f).epsilon(0.01));

        c.finish_share();
        CHECK(whole.progress() == doctest::Approx(1.f).epsilon(0.01));
    }

    SUBCASE("finishing a share twice hands in nothing the second time")
    {
        AtomicProgress half{whole, 0.5f};
        half.finish_share();
        half.finish_share();
        CHECK(whole.progress() == doctest::Approx(0.5f).epsilon(0.01));
    }

    SUBCASE("a share of a share reports into the same total")
    {
        AtomicProgress half{whole, 0.5f};
        AtomicProgress quarter{half, 0.5f};
        quarter.finish_share();
        CHECK(whole.progress() == doctest::Approx(0.25f).epsilon(0.01));
    }
}

TEST_CASE("A solve reports its whole share and no more, however it ends")
{
    // The solver stops as soon as the residual is small enough, so a bar counting only the iterations taken
    // would stall wherever the answer arrived; and one solve among several must not report the job as done.
    const int2     size{32, 32};
    const Array2Df background = filled(size, 0.5f);
    const Array2Df mask       = interior_mask(size, 1);

    SUBCASE("having run and converged inside the bound")
    {
        AtomicProgress whole{true};
        AtomicProgress share{whole, 0.5f};

        poisson_blended(background, bumpy(size, 0.2f), mask, 100000, 1e-4f, share);

        CHECK(whole.progress() == doctest::Approx(0.5f).epsilon(0.01));
    }

    SUBCASE("having found there was nothing to solve at all")
    {
        AtomicProgress whole{true};
        AtomicProgress share{whole, 0.5f};

        // Pasting an image onto itself: the background is already the exact answer, so the solve returns
        // before iterating, and must hand back the background untouched rather than wander away from it.
        const Array2Df bumps = bumpy(size, 1.f);
        const Array2Df out   = poisson_blended(bumps, bumps, mask, k_unbounded, 1e-4f, share);

        CHECK(max_difference(out, bumps) == 0.f);
        CHECK(whole.progress() == doctest::Approx(0.5f).epsilon(0.01));
    }
}
