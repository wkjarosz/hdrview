//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/envmap.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace
{

// Directions spread over the sphere, off the axes and off the cube's face boundaries, where several of these
// mappings are discontinuous and a round trip may land on either side.
std::vector<float3> sample_directions()
{
    std::vector<float3> dirs;
    // a spiral, which covers the sphere without clustering at the poles as a lat-long grid does
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
    // the two halves of a mapping have to be inverses, whatever else they are
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        // equal area goes through a polynomial approximation of atan, so it does not round-trip to the last
        // bit; the others are closed-form
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
    // a mapping returning coordinates outside [0,1] would sample nothing when remapping
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
    // convert_envmap_uv() is an unprojection through one followed by a projection through the other
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

        // away from the edges, where the disc mappings run out of sphere and the cross has empty corners
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
    // an anchor on the mapping everything else is compared against: v is latitude, so the top row is one pole
    // and the bottom row the other
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
        // both are discs about the axis they look down, so the middle of the image is that axis
        CHECK(center.z == doctest::Approx(1.f).epsilon(1e-4));
    }
}

namespace
{

//! A size in \p mapping's own proportions, so whatever it packs divides the image evenly. A square image
//! gives a cube layout faces of a fractional number of texels, which no resampling recovers.
inline int2 size_for(int mapping, int height = 96)
{
    return int2{std::max(1, int(std::lround(float(height) * envmapping_aspect(mapping)))), height};
}

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

TEST_CASE("Each mapping's Jacobian integrates to the whole sphere")
{
    // each Jacobian was derived by hand, so a wrong factor or exponent shows up as a total that is not 4*pi
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        const int n     = 512;
        double    total = 0.0;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                total += double(
                    envmap_jacobian(EnvMapping(m), float2{(float(x) + 0.5f) / float(n), (float(y) + 0.5f) / float(n)}));
        total /= double(n) * double(n);

        CHECK(total == doctest::Approx(4.0 * 3.14159265358979323846).epsilon(0.01));
    }
}

TEST_CASE("The Jacobian agrees with how far the direction actually moves")
{
    // The global check above would pass for a Jacobian wrong in compensating ways, so this measures the area
    // a step of one sample sweeps out on the sphere against the analytic value. Away from seams and rims,
    // where a difference straddles two faces and means nothing.
    const int n = 256;

    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        for (float v = 0.3f; v < 0.46f; v += 0.05f)
            for (float u = 0.4f; u < 0.6f; u += 0.05f)
            {
                const float2 uv{u, v};
                if (!envmap_uv_is_valid(EnvMapping(m), uv))
                    continue;

                const float2 hx{0.5f / float(n), 0.f}, hy{0.f, 0.5f / float(n)};
                const float3 du = envmap_uv_to_xyz(EnvMapping(m), uv + hx) - envmap_uv_to_xyz(EnvMapping(m), uv - hx);
                const float3 dv = envmap_uv_to_xyz(EnvMapping(m), uv + hy) - envmap_uv_to_xyz(EnvMapping(m), uv - hy);

                // the measured patch covers one sample of an n-by-n image; the Jacobian is per unit area
                const double measured = double(la::length(la::cross(du, dv))) * double(n) * double(n);

                CAPTURE(u);
                CAPTURE(v);
                CHECK(measured == doctest::Approx(double(envmap_jacobian(EnvMapping(m), uv))).epsilon(0.02));
            }
    }
}

TEST_CASE("Remapping carries a value with the direction it belongs to")
{
    // if the source holds a smooth function of direction, every destination sample holds that same function
    // of its own direction, whatever the two parameterizations were
    for (int src_m = 0; src_m < EnvMapping_COUNT; ++src_m)
        for (int dst_m = 0; dst_m < EnvMapping_COUNT; ++dst_m)
        {
            CAPTURE(std::string(name_of(src_m)));
            CAPTURE(std::string(name_of(dst_m)));

            // smooth and low-frequency, so resampling blur cannot account for a mismatch
            auto f = [](float3 d) { return 0.5f + 0.25f * d.y + 0.15f * d.x; };

            const Array2Df src = make_envmap(size_for(src_m), EnvMapping(src_m), f);

            for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
            {
                CAPTURE(sampler);

                const Array2Df out = remapped_envmap(src, int2{48, 48}, EnvMapping(dst_m), EnvMapping(src_m),
                                                     EnvMapSampling(sampler), 4);

                // away from the edges, where the disc mappings run out of sphere and the cube cross is empty
                for (int y = 12; y < 36; ++y)
                    for (int x = 12; x < 36; ++x)
                    {
                        const float2 uv{(x + 0.5f) / 48.f, (y + 0.5f) / 48.f};
                        // only where the destination has a direction at all
                        if (!envmap_uv_is_valid(EnvMapping(dst_m), uv))
                            continue;

                        CAPTURE(x);
                        CAPTURE(y);
                        CHECK(out(x, y) == doctest::Approx(f(envmap_uv_to_xyz(EnvMapping(dst_m), uv))).epsilon(0.05));
                    }
            }
        }
}

TEST_CASE("A cube map is read as six faces, so what is not a face never reaches the result")
{
    // A vertical cross is three quarters faces and one quarter nothing: the four cells outside its arms stand
    // for no direction. Read as one image they are texels like any other, and a mip pyramid built over the
    // whole cross has folded them deep into every face by its third level; read as six faces they are never
    // addressed. So they are given a value nothing else could be confused with.
    const int2 src_size{96, 128};
    Array2Df   src = make_envmap(src_size, EnvMapping_CubeMap, [](float3 d) { return 0.5f + 0.25f * d.y; });

    for (int y = 0; y < src_size.y; ++y)
        for (int x = 0; x < src_size.x; ++x)
        {
            const float2 uv{(float(x) + 0.5f) / float(src_size.x), (float(y) + 0.5f) / float(src_size.y)};
            if (!envmap_uv_is_valid(EnvMapping_CubeMap, uv))
                src(x, y) = 1000.f;
        }

    for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
    {
        CAPTURE(sampler);

        // minified hard, which puts the coarse levels of the pyramid in play: at 16 across, one destination
        // pixel covers dozens of source texels
        for (int dst_size : {16, 64})
        {
            CAPTURE(dst_size);
            const Array2Df out = remapped_envmap(src, int2{2 * dst_size, dst_size}, EnvMapping_LatLong,
                                                 EnvMapping_CubeMap, EnvMapSampling(sampler), 4);

            for (int y = 0; y < dst_size; ++y)
                for (int x = 0; x < 2 * dst_size; ++x)
                {
                    CAPTURE(x);
                    CAPTURE(y);
                    // the faces span [0.25, 0.75], so anything from an empty cell shows here
                    CHECK(out(x, y) > 0.2f);
                    CHECK(out(x, y) < 0.8f);
                }
        }
    }
}

TEST_CASE("A cube map's faces join up, so a read at a face edge continues onto the next face")
{
    // Two texels either side of a face join are neighboring directions but nowhere near each other in the
    // cross, and for eight of the twelve joins what lies beside the edge in the image is an empty cell. So a
    // read half a texel past an edge has to find the next face, checked against a function of direction alone.
    auto f = [](float3 d) { return 0.5f + 0.2f * d.x + 0.15f * d.y - 0.1f * d.z; };

    const int2 src_size{96, 128};
    Array2Df   src = make_envmap(src_size, EnvMapping_CubeMap, f);

    for (int y = 0; y < src_size.y; ++y)
        for (int x = 0; x < src_size.x; ++x)
        {
            const float2 uv{(float(x) + 0.5f) / float(src_size.x), (float(y) + 0.5f) / float(src_size.y)};
            if (!envmap_uv_is_valid(EnvMapping_CubeMap, uv))
                src(x, y) = 1000.f;
        }

    for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
    {
        CAPTURE(sampler);

        // magnified, so each destination sample reads a texel or two instead of averaging a face's worth
        const int2     size{192, 96};
        const Array2Df out =
            remapped_envmap(src, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling(sampler), 2);

        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                const float2 uv{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)};
                CHECK(out(x, y) == doctest::Approx(f(envmap_uv_to_xyz(EnvMapping_LatLong, uv))).epsilon(0.03));
            }
    }
}

TEST_CASE("Crossing a cube map's face join is as smooth as staying on one face")
{
    // The visible failure is a discontinuity, not an error against the truth: a bright or dark line along a
    // face's edge. The source is a smooth function of direction, so the largest step between samples on one
    // face and the largest between samples on two are measured separately and compared, which depends on
    // neither the resolution nor how steep the function is.
    auto f = [](float3 d) { return 0.5f + 0.2f * d.x + 0.15f * d.y - 0.1f * d.z; };

    const Array2Df src = make_envmap(int2{96, 128}, EnvMapping_CubeMap, f);

    // which face a direction is on: the axis it points most steeply along
    auto face_of = [](float3 d)
    {
        const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
        const int   axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
        return 2 * axis + ((axis == 0 ? d.x : axis == 1 ? d.y : d.z) >= 0.f ? 0 : 1);
    };

    for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
    {
        CAPTURE(sampler);

        const int2     size{256, 128};
        const Array2Df out =
            remapped_envmap(src, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling(sampler), 2);

        // every row, so all twelve joins are covered and not only the four a ring around the equator meets
        float within = 0.f, across = 0.f;
        int   crossings = 0;
        for (int y = 0; y < size.y; ++y)
            for (int x = 1; x < size.x; ++x)
            {
                auto dir = [&](int i)
                {
                    return envmap_uv_to_xyz(EnvMapping_LatLong, float2{(float(i) + 0.5f) / float(size.x),
                                                                       (float(y) + 0.5f) / float(size.y)});
                };

                const float step = std::abs(out(x, y) - out(x - 1, y));
                if (face_of(dir(x)) == face_of(dir(x - 1)))
                    within = std::max(within, step);
                else
                {
                    across = std::max(across, step);
                    ++crossings;
                }
            }

        // the comparison is worth nothing unless the sweep met some joins
        CHECK(crossings > 100);

        CAPTURE(within);
        CAPTURE(across);
        CHECK(across < 2.f * within);
    }
}

TEST_CASE("A face's ring carries its neighbors, so a read at an edge blends across the join")
{
    // Each face is stored with a one-texel ring of what lies past its edges, taken from whichever face that
    // is. Given every face a value of its own, a join reconstructed with the ring spreads the step over about
    // a texel, with samples in between on neither face; clamped at the edge, every sample holds one face's
    // value.
    auto face_of = [](float3 d)
    {
        const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
        const int   axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
        return 2 * axis + ((axis == 0 ? d.x : axis == 1 ? d.y : d.z) >= 0.f ? 0 : 1);
    };
    auto face_value = [](int face) { return 0.1f + 0.16f * float(face); };

    const Array2Df src =
        make_envmap(int2{96, 128}, EnvMapping_CubeMap, [&](float3 d) { return face_value(face_of(d)); });

    for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
    {
        CAPTURE(sampler);

        // Four destination samples per source texel around the equator, so a step spread over one texel is
        // several samples wide. One sample per pixel, since averaging several within it would soften the
        // step by itself.
        const int2     size{512, 256};
        const Array2Df out =
            remapped_envmap(src, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling(sampler), 1);

        const int y = size.y / 2; // the equator, which crosses four faces and so four joins

        auto face_at = [&](int x)
        {
            return face_of(envmap_uv_to_xyz(
                EnvMapping_LatLong, float2{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)}));
        };

        int   joins = 0, blended = 0;
        float largest = 0.f;
        for (int x = 1; x < size.x; ++x)
        {
            largest = std::max(largest, std::abs(out(x, y) - out(x - 1, y)));

            if (face_at(x) == face_at(x - 1))
                continue;

            ++joins;

            // within a texel of the join the reconstruction is partway between the two faces
            const float a = face_value(face_at(x - 1)), b = face_value(face_at(x));
            const float lo = std::min(a, b) + 0.15f * std::abs(a - b);
            const float hi = std::max(a, b) - 0.15f * std::abs(a - b);

            for (int i = std::max(0, x - 3); i < std::min(size.x, x + 3); ++i)
                if (out(i, y) > lo && out(i, y) < hi)
                {
                    ++blended;
                    break;
                }
        }

        CHECK(joins == 4);
        CHECK(blended == joins);

        // and no step is the whole contrast at once, which an unblended join would be; the largest
        // difference between two faces here is 0.8
        CAPTURE(largest);
        CHECK(largest < 0.5f);
    }
}

TEST_CASE("Filtering a cube map elliptically agrees with averaging it by brute force")
{
    // The two samplers reach a cube map's faces by different routes: a bilinear tap per sub-sample, or an
    // ellipse over a level of the pyramid. With enough sub-samples, point sampling is the average over the
    // destination pixel, which is what EWA approximates, so it stands as a reference. The source is constant
    // on each face and jumps between them, so every disagreement about where a face ends shows at full contrast.
    auto face_of = [](float3 d)
    {
        const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
        const int   axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
        return 2 * axis + ((axis == 0 ? d.x : axis == 1 ? d.y : d.z) >= 0.f ? 0 : 1);
    };
    const Array2Df src =
        make_envmap(int2{96, 128}, EnvMapping_CubeMap, [&](float3 d) { return 0.1f + 0.16f * float(face_of(d)); });

    // nearly isotropic, and lopsided each way: the anisotropic ones send the ellipse along a face and off
    // the end of it
    for (int2 size : {int2{16, 8}, int2{64, 32}, int2{128, 8}, int2{8, 64}})
    {
        CAPTURE(size.x);
        CAPTURE(size.y);

        const Array2Df brute =
            remapped_envmap(src, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling_Point, 24);
        const Array2Df ewa = remapped_envmap(src, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling_EWA, 16);

        double mean = 0.0, worst = 0.0;
        for (int i = 0; i < ewa.num_elements(); ++i)
        {
            const double e = std::abs(double(ewa(i)) - double(brute(i)));
            mean += e;
            worst = std::max(worst, e);
        }
        mean /= double(ewa.num_elements());

        CAPTURE(mean);
        CAPTURE(worst);

        // Room for the two filters differing in shape (a Gaussian over the footprint against a box over the
        // pixel) but not for either reading the wrong face: the values span 0.8, and a tap on the wrong side
        // of a join is worth much of that.
        CHECK(mean < 0.05);
        CHECK(worst < 0.15);
    }
}

TEST_CASE("Remapping a mapping to itself returns the image it was given")
{
    auto f = [](float3 d) { return 0.5f + 0.3f * d.z; };

    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        const int2     size = size_for(m, 72);
        const Array2Df src  = make_envmap(size, EnvMapping(m), f);

        for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
        {
            CAPTURE(sampler);
            const Array2Df out = remapped_envmap(src, size, EnvMapping(m), EnvMapping(m), EnvMapSampling(sampler), 4);

            // inside a single face of the cube cross, the tightest of the six: across one of its seams
            // supersampling averages two faces, so identity is not what should happen there
            for (int y = 18; y < 30; ++y)
                for (int x = 26; x < 38; ++x)
                {
                    CAPTURE(x);
                    CAPTURE(y);
                    CHECK(out(x, y) == doctest::Approx(src(x, y)).epsilon(0.02));
                }
        }
    }
}

TEST_CASE("A uniform environment reflects back exactly what it emits")
{
    // For constant incident radiance L the irradiance is pi*L, so dividing by pi gives L back, but only if
    // the weights over the whole image sum to the sphere's 4*pi steradians.
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
    // analytic, and unlike the uniform case it depends on more than the total: a surface facing the lit half
    // sees all of it, one facing away sees none, and one edge-on sees half
    const Array2Df src =
        make_envmap(int2{128, 128}, EnvMapping_LatLong, [](float3 d) { return d.y > 0.f ? 1.f : 0.f; });
    const Array2Df out = irradiance_envmap(src, int2{64, 32}, EnvMapping_LatLong);

    auto at = [&out](float3 n)
    {
        const float2 uv = envmap_xyz_to_uv(EnvMapping_LatLong, n);
        return out(std::min(63, int(uv.x * 64.f)), std::min(31, int(uv.y * 32.f)));
    };

    // nine coefficients cannot represent a hard edge, so these carry the approximation's truncation error
    CHECK(at(float3{0.f, 1.f, 0.f}) == doctest::Approx(1.f).epsilon(0.05));
    CHECK(at(float3{1.f, 0.f, 0.f}) == doctest::Approx(0.5f).epsilon(0.1));
    CHECK(at(float3{0.f, -1.f, 0.f}) == doctest::Approx(0.f).epsilon(0.05).scale(1.f));
}

TEST_CASE("Irradiance is smoother than what it was computed from")
{
    // a single bright direction spreads into a broad falloff instead of staying a point
    Array2Df src{int2{64, 64}};
    src(32, 16) = 100.f;

    const Array2Df out = irradiance_envmap(src, int2{32, 32}, EnvMapping_LatLong);

    // nowhere near as peaked as the input, and non-negative over most of the sphere
    float peak = 0.f;
    for (int i = 0; i < out.num_elements(); ++i) peak = std::max(peak, out(i));
    CHECK(peak < 100.f);
    CHECK(peak > 0.f);
}

TEST_CASE("EWA lands on the mean whatever shape the reduction is")
{
    // Reductions differ in kind, not only degree: an isotropic one exercises the mip level and a lopsided one
    // the probes along the long axis, and a filter can be right about one and wrong about the other.
    //
    // Period sixteen, so an output pixel covering sixteen source columns covers one period. Not eight: a level
    // one step too sharp averages over eight texels, still a whole period, and is right for the wrong reason.
    Array2Df src{int2{512, 512}};
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x) src(x, y) = (x % 16 < 4) ? 1.f : 0.f;
    const double true_mean = 0.25;

    struct Shape
    {
        const char *what;
        int2        size;
    };
    // only shapes that reduce along x, the axis the pattern varies on: a destination keeping x at full
    // resolution covers one source column per pixel and shows the stripes, not their mean
    const Shape shapes[] = {{"isotropic", int2{32, 32}},
                            {"reduced in x only", int2{32, 512}},
                            {"reduced further in x than in y", int2{16, 128}}};

    for (const Shape &shape : shapes)
    {
        CAPTURE(std::string(shape.what));

        const Array2Df ewa =
            remapped_envmap(src, shape.size, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 16);

        double err = 0.0;
        for (int i = 0; i < ewa.num_elements(); ++i) err += std::abs(double(ewa(i)) - true_mean);
        err /= double(ewa.num_elements());

        // The bound separates a filtered result from an unfiltered one instead of asserting the box mean: a
        // Gaussian weights the middle of its footprint more heavily, so over one period its answer depends on
        // the phase. Stripes surviving intact would score about 0.375.
        CHECK(err < 0.15);

        // and better than the cheap setting it exists to improve on, wherever that setting is stressed
        const Array2Df point =
            remapped_envmap(src, shape.size, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_Point, 1);
        double point_err = 0.0;
        for (int i = 0; i < point.num_elements(); ++i) point_err += std::abs(double(point(i)) - true_mean);
        point_err /= double(point.num_elements());

        CHECK(err <= point_err);
    }
}

TEST_CASE("Snapping to the sphere lands on it, and leaves alone what is already on it")
{
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        const int n = 64;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float2 uv{(x + 0.5f) / n, (y + 0.5f) / n};
                CAPTURE(uv.x);
                CAPTURE(uv.y);

                const float2 snapped = nearest_valid_envmap_uv(EnvMapping(m), uv);

                // wherever there is already a direction nothing moves; this fills in around the sphere
                if (envmap_uv_is_valid(EnvMapping(m), uv))
                {
                    CHECK(snapped.x == doctest::Approx(uv.x));
                    CHECK(snapped.y == doctest::Approx(uv.y));
                }
                else
                {
                    // and wherever there is not, what comes back is somewhere there is
                    CHECK(envmap_uv_is_valid(EnvMapping(m), snapped));

                    // and the nearest such place, checked against a search over the whole square; every
                    // eighth point only, since that search is quadratic in the grid
                    if ((x % 8) == 0 && (y % 8) == 0)
                    {
                        const float taken = la::length(snapped - uv);
                        for (int j = 0; j < n; ++j)
                            for (int i = 0; i < n; ++i)
                            {
                                const float2 q{(i + 0.5f) / n, (j + 0.5f) / n};
                                if (envmap_uv_is_valid(EnvMapping(m), q) && la::length(q - uv) <= taken - 1.5f / n)
                                    FAIL_CHECK("a nearer valid point exists");
                            }
                    }
                }
            }
    }
}

TEST_CASE("What is not sphere is filled from the sphere's edge rather than left empty")
{
    // An image's empty corners sit against the edge of the sphere, so a later bilinear read near that edge
    // reaches into them. Left at zero they darken the rim; carrying the nearest direction outward makes them
    // agree with their neighbors.
    auto f = [](float3 d) { return 0.5f + 0.4f * d.y; }; // always well away from zero

    const Array2Df src = make_envmap(int2{128, 128}, EnvMapping_LatLong, f);

    for (int dst_m : {EnvMapping_Angular, EnvMapping_MirrorBall, EnvMapping_CubeMap})
    {
        CAPTURE(std::string(name_of(dst_m)));

        const Array2Df out =
            remapped_envmap(src, int2{64, 64}, EnvMapping(dst_m), EnvMapping_LatLong, EnvMapSampling_Point, 2);

        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const float2 uv{(x + 0.5f) / 64.f, (y + 0.5f) / 64.f};
                if (envmap_uv_is_valid(EnvMapping(dst_m), uv))
                    continue;

                CAPTURE(x);
                CAPTURE(y);

                // what the nearest real direction holds, which is what was written there
                const float2 snapped  = nearest_valid_envmap_uv(EnvMapping(dst_m), uv);
                const float  expected = f(envmap_uv_to_xyz(EnvMapping(dst_m), snapped));

                CHECK(out(x, y) == doctest::Approx(expected).epsilon(0.05));

                // and not zero
                CHECK(out(x, y) > 0.05f);
            }
    }
}

TEST_CASE("A lat-long source is read as a sphere, not as a rectangle")
{
    // Reading past the left edge of a lat-long image is reading the right edge: the two are the same
    // meridian. Magnified, so the reach past the edge is a single texel: the first column of the source is
    // dark, the last bright, and everything between mid gray.
    const int2 src_size{8, 4};
    Array2Df   src{src_size};
    for (int y = 0; y < src_size.y; ++y)
        for (int x = 0; x < src_size.x; ++x) src(x, y) = x == 0 ? 0.f : (x == src_size.x - 1 ? 1.f : 0.5f);

    for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
    {
        CAPTURE(sampler);
        const Array2Df out =
            remapped_envmap(src, int2{64, 32}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling(sampler), 4);

        // the leftmost output column sits half a source texel in from the seam, so its reconstruction reaches
        // across and picks up the bright column; clamped, it would see only the dark column and stay at zero
        for (int y = 4; y < 28; ++y)
        {
            CAPTURE(y);
            CHECK(out(0, y) > 0.1f);
        }
    }
}

TEST_CASE("A lat-long pole is crossed rather than smeared")
{
    // Past the top row the sphere continues down the far side, half a turn round. An image whose two halves
    // differ tells the two apart: clamping extends the near half upward, crossing brings the far half in.
    const int2 src_size{64, 32};
    Array2Df   src{src_size};
    for (int y = 0; y < src_size.y; ++y)
        for (int x = 0; x < src_size.x; ++x) src(x, y) = x < src_size.x / 2 ? 0.f : 1.f;

    // a footprint at the top row reaches past the pole, where the other half of the image lies
    const Array2Df out =
        remapped_envmap(src, int2{32, 16}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 8);

    // along the top row a sample from the dark half sees some of the bright half across the pole, and the
    // other way about, so neither is still 0 or 1 as clamping would leave them
    float darkest = 1.f, brightest = 0.f;
    for (int x = 0; x < out.width(); ++x)
    {
        darkest   = std::min(darkest, out(x, 0));
        brightest = std::max(brightest, out(x, 0));
    }

    // still mostly its own half: the pole is one direction, so this is a nudge and not a blend to gray
    CHECK(darkest < 0.5f);
    CHECK(brightest > 0.5f);

    // ...but not pinned at the extremes, which is what tells crossing from clamping
    CHECK(darkest > 0.f);
    CHECK(brightest < 1.f);
}

TEST_CASE("Magnifying reconstructs rather than repeating source texels")
{
    // A footprint smaller than a texel has to be widened to one: an ellipse allowed to shrink below a texel
    // gathers whichever one it lands on, repeating each source texel across the output pixels that magnify it.
    // The anisotropic case is easy to miss: a footprint can be well under a texel across while spanning many
    // along, which is every lat-long pole, and there the ellipse is a sliver.
    struct Case
    {
        const char *what;
        int2        src_size, dst_size;
    };
    const Case cases[] = {{"magnified both ways", int2{32, 32}, int2{256, 256}},
                          {"magnified down, minified across", int2{256, 16}, int2{16, 128}}};

    for (const Case &c : cases)
    {
        CAPTURE(c.what);

        // a ramp down, so a reconstruction of any width steps by about the same amount everywhere, and
        // stripes across for the minified axis to average away
        Array2Df src{c.src_size};
        for (int y = 0; y < c.src_size.y; ++y)
            for (int x = 0; x < c.src_size.x; ++x)
                src(x, y) = (float(y) + 0.5f) / float(c.src_size.y) + ((x % 8 < 4) ? 0.25f : -0.25f);

        const Array2Df out =
            remapped_envmap(src, c.dst_size, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 32);

        // an ellipse enclosing no texel at all divides by no weight
        for (int i = 0; i < out.num_elements(); ++i) REQUIRE(std::isfinite(out(i)));

        // plateaus, as the fraction of vertical neighbors that barely differ: a repeated texel is flat
        // between its jumps, and at these scales that is most of the image
        const float step = 1.f / float(c.dst_size.y);
        int         flat = 0, n = 0;
        for (int y = 2; y + 3 < c.dst_size.y; ++y)
            for (int x = 0; x < c.dst_size.x; ++x)
            {
                if (std::abs(out(x, y + 1) - out(x, y)) < 0.25f * step)
                    ++flat;
                ++n;
            }

        CHECK(double(flat) / double(n) < 0.25);
    }
}

TEST_CASE("A footprint too eccentric to afford blurs rather than aliases")
{
    // At an anisotropy cap of one the ellipse is widened until round, which raises the level, so exceeding
    // the cap gives a soft result and not a broken one. Keeping the level and gathering only part of the
    // footprint is what aliases.
    Array2Df src{int2{512, 64}};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 512; ++x) src(x, y) = (x % 8 < 2) ? 1.f : 0.f;

    const Array2Df few =
        remapped_envmap(src, int2{32, 64}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 1);

    // still near the mean, not swinging between the stripe's extremes
    for (int i = 0; i < few.num_elements(); ++i)
    {
        CHECK(few(i) > 0.05f);
        CHECK(few(i) < 0.6f);
    }
}

TEST_CASE("Each parameterization asks for the proportions it can fill")
{
    // a remap to a size that ignores these stretches the result or throws away resolution along one axis
    CHECK(envmapping_aspect(EnvMapping_LatLong) == doctest::Approx(2.f));
    CHECK(envmapping_aspect(EnvMapping_Cylindrical) == doctest::Approx(2.f));
    CHECK(envmapping_aspect(EnvMapping_CubeMap) == doctest::Approx(0.75f));
    CHECK(envmapping_aspect(EnvMapping_Angular) == doctest::Approx(1.f));
    CHECK(envmapping_aspect(EnvMapping_MirrorBall) == doctest::Approx(1.f));
    CHECK(envmapping_aspect(EnvMapping_EqualArea) == doctest::Approx(1.f));
}

TEST_CASE("A mapping's aspect matches the area its image actually covers")
{
    // the proportion each wants is the one under which its valid region fills the image, measured here from
    // the mapping itself rather than repeating the numbers above
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        // a tall, thin sampling grid so both axes are resolved; count where the mapping has a direction
        const int n       = 240;
        int       covered = 0;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (envmap_uv_is_valid(EnvMapping(m), float2{(x + 0.5f) / n, (y + 0.5f) / n}))
                    ++covered;

        // the discs cover pi/4 of their square; the cross uses six of the twelve cells of its 3-by-4 grid,
        // the column of four plus the two side faces, and the rest fill their image
        const double fraction = double(covered) / double(n) * (1.0 / double(n));
        const bool   is_disc  = m == EnvMapping_Angular || m == EnvMapping_MirrorBall;
        if (is_disc)
            CHECK(fraction == doctest::Approx(3.14159265358979 / 4.0).epsilon(0.01));
        else if (m == EnvMapping_CubeMap)
            CHECK(fraction == doctest::Approx(6.0 / 12.0).epsilon(0.01));
        else
            CHECK(fraction == doctest::Approx(1.0));
    }
}

TEST_CASE("The mip level is reached, moves with the bias, and is blended across")
{
    // the quality tests only show the result is close to the mean, which a sharp enough filter over the top
    // level would also be
    Array2Df src{int2{256, 256}};
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) src(x, y) = (x % 16 < 4) ? 1.f : 0.f;

    auto remap = [&](float bias)
    { return remapped_envmap(src, int2{32, 32}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 8, bias); };

    // spread of the output, as a stand-in for how much detail survives
    auto spread = [](const Array2Df &a)
    {
        float lo = a(0), hi = a(0);
        for (int i = 1; i < a.num_elements(); ++i)
        {
            lo = std::min(lo, a(i));
            hi = std::max(hi, a(i));
        }
        return hi - lo;
    };

    const float sharp   = spread(remap(-4.f));
    const float neutral = spread(remap(0.f));
    const float soft    = spread(remap(+4.f));

    // biasing down reaches levels that still hold the stripes and biasing up ones that do not; if the level
    // were ignored, all three would be identical
    CHECK(sharp > neutral);
    CHECK(soft <= neutral);

    // A continuous response to the level says the two levels either side are blended rather than one snapped
    // to: a snapped level puts a whole level's worth of difference into the step crossing an integer and
    // almost nothing into the others, which shows as bands wherever the scale crosses a power of two.
    //
    // Swept at two scales, since the two ends of the pyramid are reached differently: the reduction above
    // lands in the middle, and a remap at its own size sits at level zero, where the lod goes negative as
    // soon as the bias does.
    auto sweep = [](const std::function<Array2Df(float)> &at_bias)
    {
        Array2Df prev  = at_bias(-1.f);
        double   worst = 0.0, total = 0.0;
        int      n = 0;
        for (float bias = -0.9f; bias <= 1.01f; bias += 0.1f)
        {
            const Array2Df cur = at_bias(bias);

            double d = 0.0;
            for (int i = 0; i < cur.num_elements(); ++i) d += std::abs(double(cur(i)) - double(prev(i)));
            d /= double(cur.num_elements());

            worst = std::max(worst, d);
            total += d;
            ++n;
            prev = cur;
        }

        // no step much larger than the steps either side of it
        CHECK(worst < 4.0 * (total / double(n)));
    };

    sweep(remap);

    // The boundary between magnifying and minifying, too fine for the sweep above to see: a step either side
    // is a fortieth of a level and must change the result by far less than a whole level does. A remap at the
    // source's own size sits on it, one destination pixel per source pixel, so the lod is the bias.
    auto at_own_size = [&](float bias) {
        return remapped_envmap(src, int2{256, 256}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 8,
                               bias);
    };

    auto mean_difference = [](const Array2Df &a, const Array2Df &b)
    {
        double d = 0.0;
        for (int i = 0; i < a.num_elements(); ++i) d += std::abs(double(a(i)) - double(b(i)));
        return d / double(a.num_elements());
    };

    const double across = mean_difference(at_own_size(-0.02f), at_own_size(0.02f));
    const double level  = mean_difference(at_own_size(0.f), at_own_size(1.f));

    REQUIRE(level > 0.0); // the two levels differ at all, or the comparison says nothing
    CHECK(across < 0.25 * level);
}
