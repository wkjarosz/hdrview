//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "chromaticity.h"
#include "image.h"
#include "test_support.h"

#include <cmath>
#include <limits>
#include <numeric>

using namespace hdrview_test;

namespace
{

/// The chromaticity bins of \p img's selected group, counted in full or within \p roi.
ChromaticityBins bin(const Image &img, Box2i roi = Box2i{int2{0}})
{
    const ChannelGroup        &g = img.groups[img.selected_group];
    ChromaticityBins::Settings settings{
        roi, g.channels, g.num_channels, g.type, img.M_RGB_to_XYZ, img.luminance_weights, img.content_version};
    std::atomic<bool> canceled{false};
    ChromaticityBins  bins;
    bins.calculate(img, settings, canceled);
    return bins;
}

int64_t total(const ChromaticityBins &b) { return std::accumulate(b.counts.begin(), b.counts.end(), int64_t(0)); }

uint32_t count_at(const ChromaticityBins &b, float2 xy)
{
    const ChromaticityGrid &g = chromaticity_grid();
    const int2              t = g.texel_of(xy);
    return g.contains(t) ? b.counts[size_t(t.y) * g.size.x + t.x] : 0u;
}

} // namespace

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

    // texel_of() and center() are inverses, and anything far off the grid stays off it
    for (int2 p : {int2{0, 0}, int2{g.size.x - 1, g.size.y - 1}, g.size / 2}) CHECK(g.texel_of(g.center(p)) == p);
    for (float2 xy : {float2{1e30f, 0.3f}, float2{-1e30f, 0.3f}, float2{0.3f, 1e30f}, float2{0.3f, -1e30f}})
        CHECK_FALSE(g.contains(g.texel_of(xy)));
}

TEST_CASE("Chromaticity bins count each primary and white at its chromaticity, at any brightness")
{
    // Each row is one color at brightnesses spanning many stops; the last rows have no chromaticity.
    const float3  colors[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0, 0}, {-1, -1, -1}, {NAN, 0, 0}};
    const float   scales[] = {1e-4f, 0.01f, 0.18f, 1.f, 7.f, 1e4f};
    constexpr int n_colors = 7, n_scales = 6, n_without = 3;

    for (auto gamut : {ColorGamut_sRGB_BT709, ColorGamut_BT2020_2100, ColorGamut_ACES_AP0})
    {
        CAPTURE(color_gamut_name(gamut));
        auto img =
            test_image(int2{n_scales, n_colors}, 3, [&](int c, int x, int y) { return colors[y][c] * scales[x]; });
        img->chromaticities = gamut_chromaticities(gamut);
        img->compute_color_transform();
        const ChromaticityBins b = bin(*img);
        REQUIRE(b.computed);

        const Chromaticities &chr  = *img->chromaticities;
        int64_t               off  = 0;
        const float2          xy[] = {chr.red, chr.green, chr.blue, chr.white};
        for (int i = 0; i < 4; ++i)
        {
            CAPTURE(i);
            const bool on_grid = chromaticity_grid().contains(chromaticity_grid().texel_of(xy[i]));
            CHECK(count_at(b, xy[i]) == (on_grid ? n_scales : 0));
            off += on_grid ? 0 : n_scales;
        }

        // every pixel is accounted for exactly once
        CHECK(b.skipped == n_without * n_scales);
        CHECK(b.outside == off);
        CHECK(total(b) + b.outside + b.skipped == int64_t(n_colors) * n_scales);
        CHECK(b.max_count == n_scales);
    }
}

TEST_CASE("Chromaticity bins ignore alpha and count only the selected region")
{
    const int2 size{16, 8};
    // a spread of positive colors, varying across the image
    auto color = [](int c, int x, int y) { return 0.05f + 0.9f * std::fmod(0.37f * x + 0.61f * y + 0.29f * c, 1.f); };
    auto rgb   = test_image(size, 3, color);
    const ChromaticityBins reference = bin(*rgb);
    REQUIRE(reference.computed);
    REQUIRE(total(reference) == size.x * size.y);

    // premultiplied on load, by powers of two so the scaled samples are exact
    auto rgba = std::make_shared<Image>(size, 4);
    for (int c = 0; c < 4; ++c)
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
                rgba->channels[size_t(c)](x, y) = c < 3 ? color(c, x, y) : std::ldexp(1.f, -((x + y) % 4));
    rgba->transparency = TransparencyType_Straight;
    rgba->finalize();
    REQUIRE(rgba->groups[rgba->selected_group].num_channels == 4);
    REQUIRE(rgba->channels[0](1, 0) != rgb->channels[0](1, 0)); // the color really was premultiplied
    CHECK(bin(*rgba).counts == reference.counts);

    // the selection restricts the count to its pixels, in the image's coordinates: here its left half, which
    // an image of just those pixels counts the same
    rgb->data_window = Box2i{int2{3, 5}, int2{3, 5} + size};
    auto left_half   = test_image(int2{size.x / 2, size.y}, 3, color);
    CHECK(bin(*rgb, Box2i{int2{0, 0}, int2{3 + size.x / 2, 100}}).counts == bin(*left_half).counts);
    const ChromaticityBins none = bin(*rgb, Box2i{int2{-50, -50}, int2{-40, -40}});
    CHECK(none.computed);
    CHECK(total(none) + none.skipped + none.outside == 0);
}
