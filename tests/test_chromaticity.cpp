//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "chromaticity.h"

#include <limits>

TEST_CASE("The chromaticity grid holds the spectral locus with one texel to spare on every side")
{
    const ChromaticityGrid &g = chromaticity_grid();
    const float             t = ChromaticityGrid::texel;

    REQUIRE(g.size.x > 0);
    REQUIRE(g.size.y > 0);
    CHECK(la::maxelem(la::abs(g.max - g.min - float2(g.size) * t)) < 1e-5f);

    float2 lo{std::numeric_limits<float>::infinity()}, hi{-std::numeric_limits<float>::infinity()};
    for (auto p : spectral_locus())
    {
        lo = la::min(lo, p);
        hi = la::max(hi, p);
    }

    // a full texel between the locus and each edge, so bilinear filtering fades it out inside the texture...
    CHECK(la::all(la::gequal(lo, g.min + t)));
    CHECK(la::all(la::lequal(hi, g.max - t)));
    // ...and no more than two, so the texels go to the locus
    CHECK(la::all(la::less(lo, g.min + 2.f * t)));
    CHECK(la::all(la::greater(hi, g.max - 2.f * t)));

    // the outermost ring of texels lies wholly outside the locus
    int inside = 0;
    for (int x = 0; x < g.size.x; ++x)
        for (int y : {0, g.size.y - 1}) inside += inside_spectral_locus(g.center(int2{x, y}));
    for (int y = 0; y < g.size.y; ++y)
        for (int x : {0, g.size.x - 1}) inside += inside_spectral_locus(g.center(int2{x, y}));
    CHECK(inside == 0);

    // texel_of() and center() are inverses
    for (int2 p : {int2{0, 0}, int2{g.size.x - 1, g.size.y - 1}, g.size / 2}) CHECK(g.texel_of(g.center(p)) == p);
}
