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

//! A mask covering everything but a border \p margin samples wide, which is what the solve requires.
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

//! Values varying in both directions, whose Laplacian is not trivially zero.
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

TEST_CASE("The Laplacian of a plane is zero, and of a known quadratic is its known value")
{
    // The operator the solve inverts, checked against what it is: a linear function has no curvature, and
    // x^2 + y^2 has the same curvature everywhere. Away from the edges, where clamping takes over.
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

            // Eight neighbors: the four along the axes contribute 2+2, and the four diagonals 4+4.
            CHECK(lap_quad(x, y) == doctest::Approx(12.f).epsilon(1e-4));
        }
}

TEST_CASE("A seamless paste keeps the background exactly, outside the mask")
{
    // The whole point of solving rather than pasting: the border is the background's own values, so there
    // is nothing for a seam to appear at.
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

TEST_CASE("A source with no gradients leaves the background's own values behind")
{
    // A constant has a Laplacian of zero, so the solution inside is the harmonic function agreeing with
    // the background at the border -- and where the background is itself flat, that is the background.
    // The pasted constant's own value never appears, which is what "gradient domain" means.
    const int2     size{24, 24};
    const Array2Df background = filled(size, 0.25f);
    const Array2Df mask       = interior_mask(size, 2);

    for (float v : {0.f, 5.f, -3.f})
    {
        CAPTURE(v);
        const Array2Df out = poisson_blended(background, filled(size, v), mask);
        CHECK(max_difference(out, background) < 1e-3f);
    }
}

TEST_CASE("Pasting an image onto itself changes nothing")
{
    // The strongest statement of correctness available without a reference solver: when the source and
    // the background already agree, the background is the exact solution, and the iteration must not
    // wander away from an answer it starts on.
    const int2     size{28, 20};
    const Array2Df background = bumpy(size, 1.f);
    const Array2Df mask       = interior_mask(size, 2);

    const Array2Df out = poisson_blended(background, background, mask);

    CHECK(max_difference(out, background) < 1e-3f);
}

TEST_CASE("The solved interior has the gradients it was given")
{
    // What the solve is for: inside the mask the Laplacian must match the source's, which is the equation
    // being solved. Checked away from the mask's own border, where the equation is not imposed.
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
    // The behavior that distinguishes this from an ordinary paste: brightening the whole source shifts
    // its values but not its gradients, so the result is unchanged. A paste would have moved with it.
    const int2     size{32, 32};
    const Array2Df background = filled(size, 0.5f);
    const Array2Df mask       = interior_mask(size, 2);
    const Array2Df source     = bumpy(size, 1.f);

    Array2Df brighter = source;
    for (int i = 0; i < brighter.num_elements(); ++i) brighter(i) += 10.f;

    const Array2Df a = poisson_blended(background, source, mask, 1000, 1e-6f);
    const Array2Df b = poisson_blended(background, brighter, mask, 1000, 1e-6f);

    CHECK(max_difference(a, b) < 1e-2f);

    // And it did do something: the interior is not simply the background it started from.
    CHECK(max_difference(a, background) > 0.05f);
}

TEST_CASE("A canceled solve stops early and reports that it did")
{
    const int2     size{64, 64};
    const Array2Df background = filled(size, 0.5f);
    const Array2Df source     = bumpy(size, 1.f);
    const Array2Df mask       = interior_mask(size, 2);

    AtomicProgress progress{true};
    progress.cancel();

    const Array2Df out = poisson_blended(background, source, mask, 500, 1e-6f, progress);

    // Whatever it had reached, which for a cancel before the first step is where it started.
    CHECK(max_difference(out, background) == doctest::Approx(0.f));
    CHECK(progress.canceled());
}

TEST_CASE("Converging early costs fewer iterations than the bound allows")
{
    // 1.8 ran its full iteration count every time, its own convergence test commented out. The residual
    // check is what makes a large paste finish in a reasonable time, so it is worth pinning that it fires.
    const int2     size{32, 32};
    const Array2Df background = filled(size, 0.5f);
    const Array2Df mask       = interior_mask(size, 2);

    // Already the answer, so the residual starts at zero and nothing should be done at all.
    AtomicProgress progress{true};
    const Array2Df out = poisson_blended(background, background, mask, 500, 1e-4f, progress);

    CHECK(max_difference(out, background) < 1e-4f);
    CHECK(progress.progress() < 0.5f); // nowhere near the five hundred it was allowed
}
