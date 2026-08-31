//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/envmap.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{

// Directions spread over the sphere, deliberately off the axes and off the cube's face boundaries, where
// several of these mappings are discontinuous and a round trip is allowed to land on either side.
std::vector<float3> sample_directions()
{
    std::vector<float3> dirs;
    // A spiral, which covers the sphere without clustering at the poles the way a lat-long grid does.
    const int n = 64;
    for (int i = 0; i < n; ++i)
    {
        const float t   = (float(i) + 0.5f) / float(n);
        const float z   = 1.f - 2.f * t;
        const float r   = std::sqrt(std::max(0.f, 1.f - z * z));
        const float phi = float(i) * 2.399963f; // golden angle, so successive samples do not line up
        dirs.push_back(la::normalize(float3{r * std::cos(phi), z, r * std::sin(phi)}));
    }
    return dirs;
}

const char *name_of(int m) { return envmapping_name(m); }

} // namespace

TEST_CASE("Every mapping turns an image point into a unit direction")
{
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        for (float v = 0.05f; v < 1.f; v += 0.1f)
            for (float u = 0.05f; u < 1.f; u += 0.1f)
            {
                CAPTURE(u);
                CAPTURE(v);
                const float3 d = envmap_uv_to_xyz(EnvMapping(m), float2{u, v});
                CHECK(la::length(d) == doctest::Approx(1.f).epsilon(1e-4));
            }
    }
}

TEST_CASE("A direction survives the trip through every mapping and back")
{
    // The property that catches a transcription error: the two halves of a mapping must be inverses, and
    // nothing else about them has to be known to check it.
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        // Equal area goes through a polynomial approximation of atan, so it does not round-trip to the
        // last bit; the others are closed-form.
        const double tol = m == EnvMapping_EqualArea ? 1e-3 : 1e-4;

        for (const float3 &d : sample_directions())
        {
            CAPTURE(d.x);
            CAPTURE(d.y);
            CAPTURE(d.z);

            const float2 uv  = envmap_xyz_to_uv(EnvMapping(m), d);
            const float3 out = envmap_uv_to_xyz(EnvMapping(m), uv);

            CHECK(out.x == doctest::Approx(d.x).epsilon(tol));
            CHECK(out.y == doctest::Approx(d.y).epsilon(tol));
            CHECK(out.z == doctest::Approx(d.z).epsilon(tol));
        }
    }
}

TEST_CASE("An image point lands inside the image it came from")
{
    // A mapping that returned coordinates outside [0,1] would sample nothing when remapping.
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        for (const float3 &d : sample_directions())
        {
            const float2 uv = envmap_xyz_to_uv(EnvMapping(m), d);
            CHECK(uv.x >= -1e-4f);
            CHECK(uv.x <= 1.f + 1e-4f);
            CHECK(uv.y >= -1e-4f);
            CHECK(uv.y <= 1.f + 1e-4f);
        }
    }
}

TEST_CASE("Converting between two mappings is the same as going through a direction")
{
    // What convert_envmap_uv() is: unproject through one, project through the other. Stated as a test so
    // that it cannot quietly become something else.
    for (int src = 0; src < EnvMapping_COUNT; ++src)
        for (int dst = 0; dst < EnvMapping_COUNT; ++dst)
        {
            CAPTURE(std::string(name_of(src)));
            CAPTURE(std::string(name_of(dst)));

            const float2 uv      = float2{0.37f, 0.61f};
            const float2 direct  = convert_envmap_uv(EnvMapping(dst), EnvMapping(src), uv);
            const float2 by_hand = envmap_xyz_to_uv(EnvMapping(dst), envmap_uv_to_xyz(EnvMapping(src), uv));

            CHECK(direct.x == doctest::Approx(by_hand.x));
            CHECK(direct.y == doctest::Approx(by_hand.y));
        }
}

TEST_CASE("Converting a mapping to itself leaves the point where it was")
{
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        const double tol = m == EnvMapping_EqualArea ? 1e-3 : 1e-4;

        // Away from the edges, where the disc mappings run out of sphere and the cross has empty corners.
        for (float v = 0.35f; v < 0.7f; v += 0.1f)
            for (float u = 0.35f; u < 0.7f; u += 0.1f)
            {
                CAPTURE(u);
                CAPTURE(v);
                const float2 out = convert_envmap_uv(EnvMapping(m), EnvMapping(m), float2{u, v});
                CHECK(out.x == doctest::Approx(u).epsilon(tol));
                CHECK(out.y == doctest::Approx(v).epsilon(tol));
            }
    }
}

TEST_CASE("Longitude-latitude puts the poles on the top and bottom edges")
{
    // An anchor on the one mapping whose convention everything else is compared against: v is latitude, so
    // the top row is one pole and the bottom row the other.
    const float3 top    = envmap_uv_to_xyz(EnvMapping_LatLong, float2{0.5f, 0.f});
    const float3 bottom = envmap_uv_to_xyz(EnvMapping_LatLong, float2{0.5f, 1.f});

    CHECK(top.y == doctest::Approx(1.f).epsilon(1e-4));
    CHECK(bottom.y == doctest::Approx(-1.f).epsilon(1e-4));
}

TEST_CASE("The angular map and the mirror ball put the forward direction at the center")
{
    for (int m : {EnvMapping_Angular, EnvMapping_MirrorBall})
    {
        CAPTURE(std::string(name_of(m)));
        const float3 center = envmap_uv_to_xyz(EnvMapping(m), float2{0.5f, 0.5f});
        // Both are discs about the axis they look down, so the middle of the image is that axis.
        CHECK(center.z == doctest::Approx(1.f).epsilon(1e-4));
    }
}

namespace
{

//! An environment whose sample at each point is \p f of the direction that point stands for.
template <typename F>
Array2Df make_envmap(int2 size, EnvMapping mapping, F &&f)
{
    Array2Df a{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
            a(x, y) = f(envmap_uv_to_xyz(mapping,
                                         float2{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)}));
    return a;
}

} // namespace

TEST_CASE("Remapping carries a value with the direction it belongs to")
{
    // The property that says remap composes the mappings correctly, and the one thing resampling cannot
    // fake: if the source holds a smooth function of direction, every destination sample must hold that
    // same function of *its* direction, whatever the two parameterizations were.
    for (int src_m = 0; src_m < EnvMapping_COUNT; ++src_m)
        for (int dst_m = 0; dst_m < EnvMapping_COUNT; ++dst_m)
        {
            CAPTURE(std::string(name_of(src_m)));
            CAPTURE(std::string(name_of(dst_m)));

            // Smooth and low-frequency, so that resampling blur cannot account for a mismatch.
            auto f = [](float3 d) { return 0.5f + 0.25f * d.y + 0.15f * d.x; };

            const Array2Df src = make_envmap(int2{128, 128}, EnvMapping(src_m), f);
            const Array2Df out = remapped_envmap(src, int2{48, 48}, EnvMapping(dst_m), EnvMapping(src_m), 2);

            // Away from the edges: the disc mappings run out of sphere at their corners and the cube cross
            // has empty ones, where there is no direction to be right about.
            for (int y = 12; y < 36; ++y)
                for (int x = 12; x < 36; ++x)
                {
                    const float2 uv{(x + 0.5f) / 48.f, (y + 0.5f) / 48.f};
                    // Only where the destination has a direction at all: a disc's corners and a cube
                    // cross's are left empty rather than filled, so there is nothing to be right about.
                    if (!envmap_uv_is_valid(EnvMapping(dst_m), uv))
                        continue;

                    CAPTURE(x);
                    CAPTURE(y);
                    CHECK(out(x, y) == doctest::Approx(f(envmap_uv_to_xyz(EnvMapping(dst_m), uv))).epsilon(0.05));
                }
        }
}

TEST_CASE("Remapping a mapping to itself returns the image it was given")
{
    auto f = [](float3 d) { return 0.5f + 0.3f * d.z; };

    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        const Array2Df src = make_envmap(int2{64, 64}, EnvMapping(m), f);
        const Array2Df out = remapped_envmap(src, int2{64, 64}, EnvMapping(m), EnvMapping(m), 2);

        // Inside a single face of the cube cross, which is the tightest constraint of the six: across one
        // of its seams supersampling genuinely averages two faces, so identity is not what should happen
        // there. The window is interior for every other mapping too.
        for (int y = 18; y < 30; ++y)
            for (int x = 26; x < 38; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(out(x, y) == doctest::Approx(src(x, y)).epsilon(0.02));
            }
    }
}

TEST_CASE("A uniform environment reflects back exactly what it emits")
{
    // The sharpest check on the solid-angle weighting there is. For constant incident radiance L the
    // irradiance is pi*L, so dividing by pi gives L back -- but only if the weights over the whole image
    // sum to the 4*pi steradians of the sphere. Get the stretch of the mapping wrong and this misses.
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        const Array2Df src = make_envmap(int2{96, 96}, EnvMapping(m), [](float3) { return 0.6f; });
        const Array2Df out = irradiance_envmap(src, int2{16, 16}, EnvMapping(m));

        for (int y = 4; y < 12; ++y)
            for (int x = 4; x < 12; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(out(x, y) == doctest::Approx(0.6f).epsilon(0.05));
            }
    }
}

TEST_CASE("A lit upper hemisphere gives the cosine-weighted answer")
{
    // Analytic, and unlike the uniform case it depends on more than the total: a surface facing the lit
    // half sees all of it, one facing away sees none, and one edge-on sees exactly half.
    const Array2Df src =
        make_envmap(int2{128, 128}, EnvMapping_LatLong, [](float3 d) { return d.y > 0.f ? 1.f : 0.f; });
    const Array2Df out = irradiance_envmap(src, int2{64, 32}, EnvMapping_LatLong);

    auto at = [&out](float3 n)
    {
        const float2 uv = envmap_xyz_to_uv(EnvMapping_LatLong, n);
        return out(std::min(63, int(uv.x * 64.f)), std::min(31, int(uv.y * 32.f)));
    };

    // Nine coefficients cannot represent a hard edge exactly, so these carry the truncation error the
    // approximation is known for rather than being tight.
    CHECK(at(float3{0.f, 1.f, 0.f}) == doctest::Approx(1.f).epsilon(0.05));
    CHECK(at(float3{1.f, 0.f, 0.f}) == doctest::Approx(0.5f).epsilon(0.1));
    CHECK(at(float3{0.f, -1.f, 0.f}) == doctest::Approx(0.f).epsilon(0.05).scale(1.f));
}

TEST_CASE("Irradiance is smoother than what it was computed from")
{
    // What the convolution is for: a single bright direction spreads into a broad falloff rather than
    // staying a point.
    Array2Df src{int2{64, 64}};
    src(32, 16) = 100.f;

    const Array2Df out = irradiance_envmap(src, int2{32, 32}, EnvMapping_LatLong);

    // Nowhere near as peaked as the input, and non-negative over most of the sphere.
    float peak = 0.f;
    for (int i = 0; i < out.num_elements(); ++i) peak = std::max(peak, out(i));
    CHECK(peak < 100.f);
    CHECK(peak > 0.f);
}
