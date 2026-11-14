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

const ChromaticityGrid &chromaticity_grid()
{
    static const ChromaticityGrid grid = []
    {
        float2 lo{numeric_limits<float>::infinity()}, hi{-numeric_limits<float>::infinity()};
        for (auto p : spectral_locus())
        {
            lo = la::min(lo, p);
            hi = la::max(hi, p);
        }

        // every texel the locus touches, and one more beyond it on each side
        const int2 first = int2(la::floor(lo / ChromaticityGrid::texel)) - 1;
        const int2 last  = int2(la::floor(hi / ChromaticityGrid::texel)) + 1;

        ChromaticityGrid g;
        g.size = last - first + 1;
        g.min  = float2(first) * ChromaticityGrid::texel;
        g.max  = float2(last + 1) * ChromaticityGrid::texel;
        return g;
    }();
    return grid;
}
