//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "chromaticity.h"
#include "colorspace.h"
#include "common.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace std;

const vector<float2> &spectral_locus()
{
    static const vector<float2> locus = []
    {
        constexpr int   sample_count   = 200;
        constexpr float wavelength_min = 380.f;
        constexpr float wavelength_max = 700.f;

        vector<float2> poly(sample_count);
        for (int i = 0; i < sample_count; ++i)
            poly[i] = wavelength_to_xy(lerp(wavelength_min, wavelength_max, float(i) / float(sample_count - 1)));
        return poly;
    }();
    return locus;
}

bool inside_spectral_locus(float2 xy)
{
    // ray casting: count the polygon's edges a ray from xy toward +x crosses
    const auto  &locus = spectral_locus();
    const size_t n     = locus.size();
    bool         in    = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const float2 a = locus[i], b = locus[j];
        if ((a.y > xy.y) != (b.y > xy.y) && xy.x < (b.x - a.x) * (xy.y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

json ChromaticityOverlays::to_json() const
{
    json j = json::object();
    for (const auto &o : k_chromaticity_overlays) j[o.key] = this->*o.flag;
    j["pixel density opacity"] = density_opacity;
    return j;
}

void ChromaticityOverlays::from_json(const json &j)
{
    if (!j.is_object())
        return;
    for (const auto &o : k_chromaticity_overlays)
        if (j.contains(o.key) && j[o.key].is_boolean())
            this->*o.flag = j[o.key].get<bool>();
    if (j.contains("pixel density opacity") && j["pixel density opacity"].is_number())
        density_opacity = std::clamp(j["pixel density opacity"].get<float>(), 0.f, 1.f);
}

ChromaticityOverlays &chromaticity_overlays()
{
    static ChromaticityOverlays overlays;
    return overlays;
}

/// The bounding box of spectral_locus().
static std::pair<float2, float2> locus_bounds()
{
    float2 lo{numeric_limits<float>::infinity()}, hi{-numeric_limits<float>::infinity()};
    for (auto p : spectral_locus())
    {
        lo = la::min(lo, p);
        hi = la::max(hi, p);
    }
    return {lo, hi};
}

ChromaticityGrid ChromaticityGrid::covering(float2 lo, float2 hi)
{
    ChromaticityGrid g;
    g.first         = int2(la::floor(lo / texel)) - 1;
    const int2 last = int2(la::floor(hi / texel)) + 1;
    g.size          = last - g.first + 1;
    return g;
}

const ChromaticityGrid &chromaticity_grid()
{
    static const ChromaticityGrid grid = []
    {
        auto [lo, hi] = locus_bounds();
        return ChromaticityGrid::covering(lo, hi);
    }();
    return grid;
}

bool ChromaticityGrid::in_locus(int2 t) const
{
    // the locus lies within chromaticity_grid(), so only its texels need testing, once
    const ChromaticityGrid      &locus = chromaticity_grid();
    static const vector<uint8_t> mask  = [&locus]
    {
        vector<uint8_t> m(size_t(locus.size.x) * locus.size.y);
        for (int y = 0; y < locus.size.y; ++y)
            for (int x = 0; x < locus.size.x; ++x)
                m[size_t(y) * locus.size.x + x] = inside_spectral_locus(locus.center(int2{x, y}));
        return m;
    }();

    const int2 q = t + first - locus.first;
    return locus.contains(q) && mask[size_t(q.y) * locus.size.x + q.x];
}

ChromaticityGrid chromaticity_grid(const float3x3 &M_RGB_to_XYZ)
{
    static constexpr float k_reach_lo = -0.5f, k_reach_hi = 1.5f;

    auto [lo, hi] = locus_bounds();
    // the primaries, M's columns, and the white point, where RGB (1, 1, 1) lands
    for (float3 XYZ : {M_RGB_to_XYZ[0], M_RGB_to_XYZ[1], M_RGB_to_XYZ[2], mul(M_RGB_to_XYZ, float3{1.f})})
    {
        const float2 xy = XYZ.xy() / (XYZ.x + XYZ.y + XYZ.z);
        if (!std::isfinite(xy.x) || !std::isfinite(xy.y))
            continue;
        lo = la::min(lo, la::max(xy, float2{k_reach_lo}));
        hi = la::max(hi, la::min(xy, float2{k_reach_hi}));
    }
    return ChromaticityGrid::covering(lo, hi);
}
