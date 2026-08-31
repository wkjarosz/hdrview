//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/filters.h"

#include <cmath>

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

    for (const Array2Df &out : {gaussian_blurred(flat, 3.f, 3.f), box_blurred(flat, 2, 2)})
        for (int i = 0; i < out.num_elements(); ++i) CHECK(out(i) == doctest::Approx(0.37f).epsilon(1e-5));
}

TEST_CASE("A blur conserves what it spreads")
{
    // An impulse well away from the border: every tap lands inside the image, so the total is unchanged.
    const Array2Df src = impulse(int2{64, 64});

    CHECK(total(gaussian_blurred(src, 2.f, 2.f)) == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(total(box_blurred(src, 3, 3)) == doctest::Approx(1.0).epsilon(1e-4));
}

TEST_CASE("A blur spreads an impulse symmetrically about where it was")
{
    const Array2Df out = gaussian_blurred(impulse(int2{33, 33}), 2.f, 2.f);
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
    const Array2Df out = gaussian_blurred(src, 0.f, 3.f);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) CHECK(out(x, y) == doctest::Approx(float(x)));
}

TEST_CASE("A wider blur flattens more than a narrower one")
{
    const Array2Df src   = impulse(int2{64, 64});
    const Array2Df tight = gaussian_blurred(src, 1.f, 1.f);
    const Array2Df wide  = gaussian_blurred(src, 8.f, 8.f);

    CHECK(tight(32, 32) > wide(32, 32));
}

TEST_CASE("Unsharp masking with no amount changes nothing")
{
    Array2Df src{int2{16, 16}};
    for (int i = 0; i < src.num_elements(); ++i) src(i) = float(i % 7) * 0.1f;

    const Array2Df out = unsharp_masked(src, 2.f, 0.f);
    for (int i = 0; i < src.num_elements(); ++i) CHECK(out(i) == doctest::Approx(src(i)));
}

TEST_CASE("Unsharp masking increases the contrast of an edge")
{
    // A step, which is what sharpening is meant to act on: the sample just inside the bright side should
    // overshoot above it, and the one just inside the dark side undershoot below.
    Array2Df src{int2{16, 1}};
    for (int x = 0; x < 16; ++x) src(x, 0) = x < 8 ? 0.f : 1.f;

    const Array2Df out = unsharp_masked(src, 2.f, 1.f);

    CHECK(out(8, 0) > 1.f);
    CHECK(out(7, 0) < 0.f);
}
