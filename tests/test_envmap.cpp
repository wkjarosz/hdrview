//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/envmap.h"

#include <cmath>
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
        CAPTURE(name_of(m));
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
        CAPTURE(name_of(m));

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
        CAPTURE(name_of(m));
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
            CAPTURE(name_of(src));
            CAPTURE(name_of(dst));

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
        CAPTURE(name_of(m));
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
        CAPTURE(name_of(m));
        const float3 center = envmap_uv_to_xyz(EnvMapping(m), float2{0.5f, 0.5f});
        // Both are discs about the axis they look down, so the middle of the image is that axis.
        CHECK(center.z == doctest::Approx(1.f).epsilon(1e-4));
    }
}
