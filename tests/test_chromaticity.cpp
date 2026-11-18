//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "chromaticity.h"
#include "colorspace.h"
#include "image.h"
#include "test_support.h"

#include <algorithm>
#include <array>
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
    const int2 t = b.grid.texel_of(xy);
    return b.grid.contains(t) ? b.counts[size_t(t.y) * b.grid.size.x + t.x] : 0u;
}

/// The xy of each primary and the white point.
std::array<float2, 4> corners(const Chromaticities &chr) { return {chr.red, chr.green, chr.blue, chr.white}; }

} // namespace

TEST_CASE("Every chromaticity grid holds what it covers with one texel to spare, on one lattice")
{
    const ChromaticityGrid &locus = chromaticity_grid();
    const float             t     = ChromaticityGrid::texel;

    float2 locus_lo{std::numeric_limits<float>::infinity()}, locus_hi{-std::numeric_limits<float>::infinity()};
    for (auto p : spectral_locus())
    {
        locus_lo = la::min(locus_lo, p);
        locus_hi = la::max(locus_hi, p);
    }

    // The locus's own grid, then each gamut's: every named one, sRGB with blue dragged across the opposite edge
    // past white, and sRGB with each primary and the white in turn dragged off the locus, beyond the grid's reach
    // on every side or just past the locus.
    std::vector<Chromaticities> gamuts;
    for (int i = ColorGamut_FirstNamed; i <= ColorGamut_LastNamed; ++i)
        gamuts.push_back(gamut_chromaticities(ColorGamut_(i)));
    const Chromaticities srgb    = gamut_chromaticities(ColorGamut_sRGB_BT709);
    Chromaticities       flipped = srgb;
    flipped.blue                 = float2{0.5f, 0.6f};
    gamuts.push_back(flipped);
    for (int corner = 0; corner < 4; ++corner)
        for (float2 xy : {float2{-3.f, 0.3f}, float2{3.f, 0.3f}, float2{0.3f, -3.f}, float2{0.3f, 3.f},
                          float2{1.2f, 0.1f}, float2{0.1f, -0.2f}})
        {
            Chromaticities chr  = srgb;
            float2        *ps[] = {&chr.red, &chr.green, &chr.blue, &chr.white};
            *ps[corner]         = xy;
            gamuts.push_back(chr);
        }

    std::vector<std::pair<ChromaticityGrid, std::vector<float2>>> grids{{locus, {}}};
    for (const Chromaticities &chr : gamuts)
    {
        // the corners the grid reaches, which stops at xy in [-0.5, 1.5]
        std::vector<float2> reached;
        for (float2 xy : corners(chr)) reached.push_back(la::clamp(xy, float2{-0.5f}, float2{1.5f}));
        grids.push_back({chromaticity_grid(RGB_to_XYZ(chr, 1.f)), reached});
    }

    for (const auto &[g, reached] : grids)
    {
        CAPTURE(g.first);
        CAPTURE(g.size);
        REQUIRE(la::all(la::greater(g.size, int2{0})));

        // a full texel beyond the locus and each corner, so bilinear filtering fades it out inside the texture,
        // and at most two beyond the farthest of them, so the texels go to the content
        float2 lo = locus_lo, hi = locus_hi;
        for (float2 xy : reached)
        {
            lo = la::min(lo, xy);
            hi = la::max(hi, xy);
        }
        CHECK(la::all(la::gequal(lo, g.min() + t)));
        CHECK(la::all(la::lequal(hi, g.max() - t)));
        CHECK(la::all(la::lequal(lo, g.min() + 2.f * t)));
        CHECK(la::all(la::gequal(hi, g.max() - 2.f * t)));

        // on the locus grid's lattice, so a texel of one covers exactly a texel of the other
        for (int2 p : {int2{0, 0}, locus.size - 1, locus.size / 2})
            CHECK(g.texel_of(locus.center(p)) == p + locus.first - g.first);

        // the outermost ring of texels lies wholly outside the locus
        int inside = 0;
        for (int x = 0; x < g.size.x; ++x)
            for (int y : {0, g.size.y - 1}) inside += inside_spectral_locus(g.center(int2{x, y}));
        for (int y = 0; y < g.size.y; ++y)
            for (int x : {0, g.size.x - 1}) inside += inside_spectral_locus(g.center(int2{x, y}));
        CHECK(inside == 0);

        // texel_of() and center() are inverses, and anything far off the grid stays off it
        for (int2 p : {int2{0, 0}, g.size - 1, g.size / 2}) CHECK(g.texel_of(g.center(p)) == p);
        for (float2 xy : {float2{1e30f, 0.3f}, float2{-1e30f, 0.3f}, float2{0.3f, 1e30f}, float2{0.3f, -1e30f}})
            CHECK_FALSE(g.contains(g.texel_of(xy)));
    }

    // a grid reaching past the locus's knows which of its texels lie inside it
    const ChromaticityGrid g = chromaticity_grid(RGB_to_XYZ(gamut_chromaticities(ColorGamut_ACES_AP0), 1.f));
    REQUIRE(g.size.y > locus.size.y);
    int disagree = 0, in_locus = 0;
    for (int y = 0; y < g.size.y; ++y)
        for (int x = 0; x < g.size.x; ++x)
        {
            in_locus += g.in_locus(int2{x, y});
            disagree += g.in_locus(int2{x, y}) != inside_spectral_locus(g.center(int2{x, y}));
        }
    CHECK(in_locus > g.size.x * g.size.y / 4);
    CHECK(disagree == 0);
}

TEST_CASE("Chromaticity bins count each selected pixel once, by its color whatever its brightness and alpha")
{
    // Each row is one color at brightnesses spanning many stops. The last row is red less nearly as much blue as
    // cancels its X + Y + Z, which lands its chromaticity far off any grid; the three before it have none.
    const float   scales[] = {1e-4f, 0.01f, 0.18f, 1.f, 7.f, 1e4f};
    constexpr int n_colors = 8, n_scales = 6, n_without = 3;

    for (auto gamut : {ColorGamut_sRGB_BT709, ColorGamut_BT2020_2100, ColorGamut_ACES_AP0})
    {
        CAPTURE(color_gamut_name(gamut));
        const Chromaticities chr              = gamut_chromaticities(gamut);
        const float3x3       M                = RGB_to_XYZ(chr, 1.f);
        const float3         colors[n_colors] = {
            {1, 0, 0}, {0, 1, 0},    {0, 0, 1},   {1, 1, 1},
            {0, 0, 0}, {-1, -1, -1}, {NAN, 0, 0}, {1, 0, -0.99f * la::sum(M[0]) / la::sum(M[2])}};

        auto img =
            test_image(int2{n_scales, n_colors}, 3, [&](int c, int x, int y) { return colors[y][c] * scales[x]; });
        img->chromaticities = chr;
        img->compute_color_transform();
        const ChromaticityBins b = bin(*img);
        REQUIRE(b.computed);

        for (float2 xy : corners(chr))
        {
            CAPTURE(xy);
            CHECK(count_at(b, xy) == n_scales);
        }

        // every pixel is accounted for exactly once
        CHECK(b.skipped == n_without * n_scales);
        CHECK(b.outside == n_scales);
        CHECK(total(b) + b.outside + b.skipped == int64_t(n_colors) * n_scales);
        CHECK(b.max_count == n_scales);
    }

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

TEST_CASE("A box meets the spectral locus wherever it overlaps the region the locus encloses")
{
    const auto &locus     = spectral_locus();
    auto        vertex_in = [&](float2 lo, float2 hi)
    {
        return std::any_of(locus.begin(), locus.end(),
                           [&](float2 p) { return la::all(la::gequal(p, lo)) && la::all(la::lequal(p, hi)); });
    };

    // wholly inside, holding no vertex, and wholly around it
    const float2 white{0.3127f, 0.329f};
    REQUIRE_FALSE(vertex_in(white - 0.01f, white + 0.01f));
    CHECK(box_meets_spectral_locus(white - 0.01f, white + 0.01f));
    CHECK(box_meets_spectral_locus(float2{-1.f}, float2{2.f}));

    // across the line of purples, which only its two ends sample, so that only the edge itself crosses the box
    const float2 purple = 0.5f * (locus.front() + locus.back());
    REQUIRE_FALSE(vertex_in(purple - 0.005f, purple + 0.005f));
    CHECK(box_meets_spectral_locus(purple - 0.005f, purple + 0.005f));

    // clear of it: the corner the diagram's legend sits in, above the green tip, and beside the red end
    CHECK_FALSE(box_meets_spectral_locus(float2{0.55f, 0.6f}, float2{0.75f, 0.9f}));
    CHECK_FALSE(box_meets_spectral_locus(float2{0.0f, 0.86f}, float2{0.2f, 0.95f}));
    CHECK_FALSE(box_meets_spectral_locus(float2{0.74f, 0.2f}, float2{0.8f, 0.3f}));

    // across a sweep of boxes, every one holding a vertex of the locus or a point inside it meets it
    int disagree = 0, met = 0;
    for (float y = -0.1f; y < 0.95f; y += 0.04f)
        for (float x = -0.1f; x < 0.8f; x += 0.04f)
        {
            const float2 lo{x, y}, hi{x + 0.05f, y + 0.03f};
            bool         sampled = vertex_in(lo, hi);
            for (int j = 0; j <= 10 && !sampled; ++j)
                for (int i = 0; i <= 10 && !sampled; ++i)
                    sampled = inside_spectral_locus(lo + (hi - lo) * float2{i / 10.f, j / 10.f});
            const bool meets = box_meets_spectral_locus(lo, hi);
            met += meets;
            disagree += sampled && !meets;
        }
    CHECK(met > 50);
    CHECK(disagree == 0);
}
