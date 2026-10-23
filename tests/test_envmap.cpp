//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h" // first, so that MSVC's <cmath> defines M_PI
#include "edit/envmap.h"

#include <cmath>
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

/// Whether every component of \p a is within \p tol of \p b's.
template <typename V>
bool near(const V &a, const V &b, float tol)
{ return la::maxelem(la::abs(a - b)) <= tol; }

} // namespace

TEST_CASE("Every mapping is a bijection between its image and the sphere, oriented as its anchors say")
{
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        // equal area goes through a polynomial approximation of atan, so it does not round-trip to the last bit; the
        // others are closed-form
        const float tol = m == EnvMapping_EqualArea ? 1e-3f : 1e-4f;

        // every direction lands inside the image, on a point that stands for it
        for (const float3 &d : sample_directions())
        {
            CAPTURE(d);
            const float2 uv = envmap_xyz_to_uv(EnvMapping(m), d);
            CAPTURE(uv);
            CHECK(la::all(la::gequal(uv, float2{-1e-4f})));
            CHECK(la::all(la::lequal(uv, float2{1.f + 1e-4f})));
            CHECK(envmap_uv_is_valid(EnvMapping(m), uv));
            CHECK(near(envmap_uv_to_xyz(EnvMapping(m), uv), d, tol));
        }

        // and every point standing for a direction stands for a unit one, which leads back to that point, so no two
        // points share one; on a grid off the cube layouts' cell boundaries and the equal-area square's folds
        const int n = 12;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                const float2 uv{(float(i) + 0.37f) / float(n), (float(j) + 0.37f) / float(n)};
                if (!envmap_uv_is_valid(EnvMapping(m), uv))
                    continue;
                CAPTURE(uv);
                const float3 d = envmap_uv_to_xyz(EnvMapping(m), uv);
                CHECK(la::length(d) == doctest::Approx(1.f).epsilon(1e-4));
                CHECK(near(envmap_xyz_to_uv(EnvMapping(m), d), uv, tol));
            }
    }

    // A bijection can still be mirrored or turned, which a remap between two mappings would show as the image
    // flipped or rotated, so each mapping is pinned at points that fix its orientation: the discs look down +z with
    // +y up and +x to the right, the cylinders put +z in the middle and +y at the top, the cube layouts each face in
    // its cell (the cross's, or OpenEXR's +x, -x, +y, -y, +z, -z down the column) turned as the point a quarter of the
    // way into its top-left corner says, and the equal-area square has +y at its center and the equator on its edges.
    struct Anchor
    {
        int    mapping;
        float2 uv;
        float3 d;
        bool   unique = true; ///< whether no other point of the image stands for d, so d also leads back to uv
    };
    const float  mb        = 0.5f * 0.70710678f; //< the mirror ball's radius for a quarter turn from +z: sin(pi/4) / 2
    const Anchor anchors[] = {
        {EnvMapping_Angular, {0.5f, 0.5f}, {0, 0, 1}},
        {EnvMapping_Angular, {0.75f, 0.5f}, {1, 0, 0}},
        {EnvMapping_Angular, {0.25f, 0.5f}, {-1, 0, 0}},
        {EnvMapping_Angular, {0.5f, 0.25f}, {0, 1, 0}},
        {EnvMapping_Angular, {0.5f, 0.75f}, {0, -1, 0}},
        {EnvMapping_Angular, {1.f, 0.5f}, {0, 0, -1}, false},
        {EnvMapping_MirrorBall, {0.5f, 0.5f}, {0, 0, 1}},
        {EnvMapping_MirrorBall, {0.5f + mb, 0.5f}, {1, 0, 0}},
        {EnvMapping_MirrorBall, {0.5f - mb, 0.5f}, {-1, 0, 0}},
        {EnvMapping_MirrorBall, {0.5f, 0.5f - mb}, {0, 1, 0}},
        {EnvMapping_MirrorBall, {0.5f, 0.5f + mb}, {0, -1, 0}},
        {EnvMapping_MirrorBall, {1.f, 0.5f}, {0, 0, -1}, false},
        {EnvMapping_LatLong, {0.5f, 0.5f}, {0, 0, 1}},
        {EnvMapping_LatLong, {0.75f, 0.5f}, {1, 0, 0}},
        {EnvMapping_LatLong, {0.25f, 0.5f}, {-1, 0, 0}},
        {EnvMapping_LatLong, {0.f, 0.5f}, {0, 0, -1}, false},
        {EnvMapping_LatLong, {0.5f, 0.25f}, {0, 1, 1}}, // latitude linear in v
        {EnvMapping_LatLong, {0.3f, 0.f}, {0, 1, 0}, false},
        {EnvMapping_LatLong, {0.3f, 1.f}, {0, -1, 0}, false},
        {EnvMapping_Cylindrical, {0.5f, 0.5f}, {0, 0, 1}},
        {EnvMapping_Cylindrical, {0.75f, 0.5f}, {1, 0, 0}},
        {EnvMapping_Cylindrical, {0.25f, 0.5f}, {-1, 0, 0}},
        {EnvMapping_Cylindrical, {0.f, 0.5f}, {0, 0, -1}, false},
        {EnvMapping_Cylindrical, {0.5f, 0.25f}, {0, 0.5f, 0.8660254f}}, // height linear in v
        {EnvMapping_Cylindrical, {0.3f, 0.f}, {0, 1, 0}, false},
        {EnvMapping_Cylindrical, {0.3f, 1.f}, {0, -1, 0}, false},
        {EnvMapping_CubeMap, {2.25f / 3.f, 1.25f / 4.f}, {1, 0.5f, 0.5f}},
        {EnvMapping_CubeMap, {0.25f / 3.f, 1.25f / 4.f}, {-1, 0.5f, -0.5f}},
        {EnvMapping_CubeMap, {1.25f / 3.f, 0.25f / 4.f}, {-0.5f, 1, -0.5f}},
        {EnvMapping_CubeMap, {1.25f / 3.f, 2.25f / 4.f}, {-0.5f, -1, 0.5f}},
        {EnvMapping_CubeMap, {1.25f / 3.f, 1.25f / 4.f}, {-0.5f, 0.5f, 1}},
        {EnvMapping_CubeMap, {1.25f / 3.f, 3.25f / 4.f}, {-0.5f, -0.5f, -1}},
        {EnvMapping_CubeMapColumn, {0.25f, 0.25f / 6.f}, {1, 0.5f, -0.5f}},
        {EnvMapping_CubeMapColumn, {0.25f, 1.25f / 6.f}, {-1, 0.5f, 0.5f}},
        {EnvMapping_CubeMapColumn, {0.25f, 2.25f / 6.f}, {-0.5f, 1, 0.5f}},
        {EnvMapping_CubeMapColumn, {0.25f, 3.25f / 6.f}, {-0.5f, -1, -0.5f}},
        {EnvMapping_CubeMapColumn, {0.25f, 4.25f / 6.f}, {0.5f, 0.5f, 1}},
        {EnvMapping_CubeMapColumn, {0.25f, 5.25f / 6.f}, {-0.5f, 0.5f, -1}},
        {EnvMapping_EqualArea, {0.5f, 0.5f}, {0, 1, 0}},
        {EnvMapping_EqualArea, {1.f, 0.5f}, {1, 0, 0}},
        {EnvMapping_EqualArea, {0.f, 0.5f}, {-1, 0, 0}},
        {EnvMapping_EqualArea, {0.5f, 0.f}, {0, 0, 1}},
        {EnvMapping_EqualArea, {0.5f, 1.f}, {0, 0, -1}},
        {EnvMapping_EqualArea, {0.75f, 0.5f}, {0.6614378f, 0.75f, 0}}, // equal area: half the radius, a quarter of it
        {EnvMapping_EqualArea, {0.f, 0.f}, {0, -1, 0}, false},
    };
    int anchored[EnvMapping_COUNT] = {};
    for (const Anchor &a : anchors)
    {
        CAPTURE(std::string(name_of(a.mapping)));
        CAPTURE(a.uv);
        const float3 d = la::normalize(a.d);
        CAPTURE(d);
        CHECK(near(envmap_uv_to_xyz(EnvMapping(a.mapping), a.uv), d, 1e-3f));
        if (a.unique)
            CHECK(near(envmap_xyz_to_uv(EnvMapping(a.mapping), d), a.uv, 1e-3f));
        ++anchored[a.mapping];
    }
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        CHECK(anchored[m] >= 6);
    }
}

namespace
{

/// A size in \p mapping's own proportions, so whatever it packs divides the image evenly.
inline int2 size_for(int mapping, int height = 96)
{
    return int2{std::max(1, int(std::lround(float(height) * envmapping_aspect(mapping)))), height};
}

/// An environment whose sample at each point is \p f of the direction that point stands for.
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
    // If the source holds a smooth function of direction, every destination sample holds that same function of its
    // own direction turned by the remap's rotation, whatever the two parameterizations were. Turned, so that no read
    // lands on a source texel's center by coincidence, as one of a mapping onto itself unturned would.
    const float3x3 R = envmap_rotation(0.7f, -0.4f, 1.1f);
    // smooth and low-frequency, so resampling blur cannot account for a mismatch
    auto f = [](float3 d) { return 0.5f + 0.25f * d.y + 0.15f * d.x; };

    for (int src_m = 0; src_m < EnvMapping_COUNT; ++src_m)
    {
        const Array2Df src = make_envmap(size_for(src_m), EnvMapping(src_m), f);
        for (int dst_m = 0; dst_m < EnvMapping_COUNT; ++dst_m)
            for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
            {
                CAPTURE(std::string(name_of(src_m)));
                CAPTURE(std::string(name_of(dst_m)));
                CAPTURE(sampler);

                const Array2Df out = remapped_envmap(src, int2{48, 48}, EnvMapping(dst_m), EnvMapping(src_m),
                                                     EnvMapSampling(sampler), 4, R);

                // away from the edges, where the disc mappings run out of sphere and the cube cross is empty, and
                // only where the destination has a direction at all
                float worst = 0.f;
                for (int y = 12; y < 36; ++y)
                    for (int x = 12; x < 36; ++x)
                    {
                        const float2 uv{(x + 0.5f) / 48.f, (y + 0.5f) / 48.f};
                        if (envmap_uv_is_valid(EnvMapping(dst_m), uv))
                            worst = std::max(worst,
                                             std::abs(out(x, y) - f(mul(R, envmap_uv_to_xyz(EnvMapping(dst_m), uv)))));
                    }
                // EWA blurs by half an output pixel, which a footprint stretched by the remap carries a long way
                CAPTURE(worst);
                CHECK(worst < (sampler == EnvMapSampling_Point ? 0.01f : 0.04f));
            }
    }
}

TEST_CASE("A remap's rotation is the yaw, pitch and roll it is given, and a drag's turns one direction onto another")
{
    // yaw turns about the up axis, carrying +x toward -z, as it carries a lat-long's horizon sideways
    const float3 turned = mul(envmap_rotation(0.3f, 0.f, 0.f), float3{1.f, 0.f, 0.f});
    CHECK(la::length(turned - float3{std::cos(0.3f), 0.f, -std::sin(0.3f)}) < 1e-6f);

    // the angles come back from the rotation they make, which is a rotation
    for (float yaw : {-2.5f, -0.3f, 0.f, 1.2f, 3.f})
        for (float pitch : {-1.4f, -0.5f, 0.f, 0.9f})
            for (float roll : {-3.f, 0.f, 0.4f, 2.2f})
            {
                CAPTURE(yaw);
                CAPTURE(pitch);
                CAPTURE(roll);
                const float3x3 R = envmap_rotation(yaw, pitch, roll);
                CHECK(la::length(envmap_euler(R) - float3{yaw, pitch, roll}) < 1e-4f);
                CHECK(la::determinant(R) == doctest::Approx(1.f));
                for (int c = 0; c < 3; ++c)
                    CHECK(la::length(mul(transpose(R), R)[c] - float3x3{la::identity}[c]) < 1e-5f);
            }

    // and at a quarter-turn pitch, where yaw and roll turn about the same axis, the rotation still comes back, with
    // the roll it is asked to keep, since only the yaw and roll together are set there
    for (float pitch : {-1.5707963f, 1.5707963f})
        for (float roll : {-2.5f, 0.f, 0.6f})
            for (float keep : {-1.f, 0.f, 2.f})
            {
                CAPTURE(pitch);
                CAPTURE(roll);
                CAPTURE(keep);
                const float3x3 locked = envmap_rotation(0.8f, pitch, roll);
                const float3   e      = envmap_euler(locked, keep);
                CHECK(e.z == keep);
                CHECK(std::abs(e.x) <= 3.1415927f);
                const float3x3 back = envmap_rotation(e.x, e.y, e.z);
                for (int c = 0; c < 3; ++c) CHECK(la::length(back[c] - locked[c]) < 1e-3f);
            }

    // the rotation between two directions takes the one onto the other, opposite ones included
    for (const float3 from : sample_directions())
        for (const float3 to : {float3{0.f, 1.f, 0.f}, la::normalize(float3{1.f, -2.f, 0.5f}), -from})
        {
            const float4 q = rotation_between(from, to);
            CHECK(la::length(la::qrot(q, from) - to) < 1e-4f);
        }
}

TEST_CASE("A cube map's faces join up, so a read at a face edge continues onto the next face")
{
    // Two texels either side of a face join are neighboring directions but nowhere near each other in the
    // cross, and for eight of the twelve joins what lies beside the edge in the image is an empty cell. So a
    // read half a texel past an edge has to find the next face.
    auto face_of = [](float3 d)
    {
        const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
        const int   axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
        return 2 * axis + ((axis == 0 ? d.x : axis == 1 ? d.y : d.z) >= 0.f ? 0 : 1);
    };

    // A vertical cross is three quarters faces and one quarter nothing: the four cells outside its arms stand for no
    // direction. Read as one image they are texels like any other, and a mip pyramid built over the whole cross has
    // folded them deep into every face by its third level; read as six faces they are never addressed. So both
    // sources give them a value nothing else could be confused with.
    const int2 src_size{96, 128};
    auto       with_empty_cells_marked = [&](Array2Df a)
    {
        for (int y = 0; y < src_size.y; ++y)
            for (int x = 0; x < src_size.x; ++x)
                if (!envmap_uv_is_valid(EnvMapping_CubeMap, float2{(float(x) + 0.5f) / float(src_size.x),
                                                                   (float(y) + 0.5f) / float(src_size.y)}))
                    a(x, y) = 1000.f;
        return a;
    };

    // a smooth function of direction, which the result has to reproduce wherever a sample lands, joins included
    auto           f      = [](float3 d) { return 0.5f + 0.2f * d.x + 0.15f * d.y - 0.1f * d.z; };
    const Array2Df smooth = with_empty_cells_marked(make_envmap(src_size, EnvMapping_CubeMap, f));

    // and a second source, constant on each face and jumping between them, so a disagreement about where a
    // face ends shows at full contrast instead of as a fraction of a gradient
    auto           face_value = [](int face) { return 0.1f + 0.16f * float(face); };
    const Array2Df stepped    = with_empty_cells_marked(
        make_envmap(src_size, EnvMapping_CubeMap, [&](float3 d) { return face_value(face_of(d)); }));

    for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
    {
        CAPTURE(sampler);

        // magnified, so each destination sample reads a texel or two instead of averaging a face's worth
        const int2     size{192, 96};
        const Array2Df out =
            remapped_envmap(smooth, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling(sampler), 2);

        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                const float2 uv{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)};
                CHECK(out(x, y) == doctest::Approx(f(envmap_uv_to_xyz(EnvMapping_LatLong, uv))).epsilon(0.03));
            }

        // A read past a face's edge takes the texel the sphere continues into on the next face, so a join is
        // reconstructed over about a texel, with samples in between on neither face, where clamping at the edge
        // leaves every sample holding one face's value.
        // Four destination samples per source texel around the equator, so a step spread over one texel is
        // several samples wide, and one sample per pixel, since averaging several within it would soften
        // the step by itself.
        const int2     ring_size{512, 256};
        const Array2Df ring =
            remapped_envmap(stepped, ring_size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling(sampler), 1);

        const int y = ring_size.y / 2; // the equator, which crosses four faces and so four joins

        auto face_at = [&](int x)
        {
            return face_of(envmap_uv_to_xyz(EnvMapping_LatLong, float2{(float(x) + 0.5f) / float(ring_size.x),
                                                                       (float(y) + 0.5f) / float(ring_size.y)}));
        };

        int   joins = 0, blended = 0;
        float largest = 0.f;
        for (int x = 1; x < ring_size.x; ++x)
        {
            largest = std::max(largest, std::abs(ring(x, y) - ring(x - 1, y)));

            if (face_at(x) == face_at(x - 1))
                continue;

            ++joins;

            // within a texel of the join the reconstruction is partway between the two faces
            const float a = face_value(face_at(x - 1)), b = face_value(face_at(x));
            const float lo = std::min(a, b) + 0.15f * std::abs(a - b);
            const float hi = std::max(a, b) - 0.15f * std::abs(a - b);

            for (int i = std::max(0, x - 3); i < std::min(ring_size.x, x + 3); ++i)
                if (ring(i, y) > lo && ring(i, y) < hi)
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

    // The two samplers reach the faces by different routes: a bilinear tap per sub-sample, or an ellipse
    // over a level of the pyramid. With enough sub-samples point sampling is the average over the
    // destination pixel, which is what EWA approximates, so it stands as a reference. Nearly isotropic and
    // lopsided each way: the anisotropic shapes send the ellipse along a face and off the end of it. Minified
    // hard, which puts the coarse levels of the pyramid in play: at 8 or 16 across, one destination pixel covers
    // dozens of source texels.
    for (int2 size : {int2{16, 8}, int2{64, 32}, int2{128, 8}, int2{8, 64}})
    {
        CAPTURE(size.x);
        CAPTURE(size.y);

        const Array2Df brute =
            remapped_envmap(stepped, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling_Point, 24);
        const Array2Df ewa =
            remapped_envmap(stepped, size, EnvMapping_LatLong, EnvMapping_CubeMap, EnvMapSampling_EWA, 16);

        // every output is an average of face values, which span [0.1, 0.9], so an empty cell shows
        double mean = 0.0, worst = 0.0;
        float  lowest = 1.f, highest = 0.f;
        for (int i = 0; i < ewa.num_elements(); ++i)
        {
            const double e = std::abs(double(ewa(i)) - double(brute(i)));
            mean += e;
            worst   = std::max(worst, e);
            lowest  = std::min({lowest, ewa(i), brute(i)});
            highest = std::max({highest, ewa(i), brute(i)});
        }
        CHECK(lowest >= 0.1f - 1e-4f);
        CHECK(highest <= 0.9f + 1e-4f);
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

TEST_CASE("Every edge of a mapping's image is read as the sphere continues past it")
{
    // Remapping a mapping onto itself at its own size gives back the image, up to what reconstructing it costs, when a
    // read past an edge takes the texel the sphere continues into: the other end of a row past a lat-long or
    // cylindrical seam, the far side of a lat-long pole half a turn round, the same edge mirrored about its midpoint in
    // the equal-area square, and for the cube layouts the neighboring face, wherever in the image the layout puts it. A
    // lobe a few texels inside an edge, off its midpoint, differs from one side of the edge to the other, so a read of
    // the wrong texel there (clamping, or whatever the layout put beside the edge) makes the texels by the image's
    // edges, or its faces', reconstruct worse than those in the middle, where the lobe peaks. The discs are absent, as
    // their rim is the one direction -z, which nothing reads across, and so are the cylinder's poles, whose rows span
    // so much latitude that a bilinear read across the pole reconstructs poorly whichever texel it takes.
    struct Edge
    {
        int         mapping;
        const char *what;
        float2      at, inward; ///< a point on the edge, in texels of an image of size_for(mapping, 72), and the way in
        float       allowance = 1.f; ///< how much worse than the middle the edges may reconstruct
    };
    const Edge edges[] = {
        {EnvMapping_LatLong, "seam", {0.f, 28.8f}, {1.f, 0.f}},
        {EnvMapping_LatLong, "north pole", {43.2f, 0.f}, {0.f, 1.f}},
        {EnvMapping_LatLong, "south pole", {100.8f, 72.f}, {0.f, -1.f}},
        {EnvMapping_Cylindrical, "seam", {144.f, 43.2f}, {-1.f, 0.f}},
        {EnvMapping_CubeMap, "+y beside an empty cell", {18.f, 10.8f}, {1.f, 0.f}},
        {EnvMapping_CubeMap, "-x below an empty cell", {7.2f, 18.f}, {0.f, 1.f}},
        {EnvMapping_CubeMap, "-z beside an empty cell", {36.f, 61.2f}, {-1.f, 0.f}},
        {EnvMapping_CubeMap, "-z at the bottom", {24.3f, 72.f}, {0.f, -1.f}},
        {EnvMapping_CubeMapColumn, "+x above -x", {4.8f, 12.f}, {0.f, -1.f}},
        {EnvMapping_CubeMapColumn, "+y at the side", {0.f, 32.4f}, {1.f, 0.f}},
        {EnvMapping_CubeMapColumn, "+z above -z", {7.2f, 60.f}, {0.f, -1.f}},
        // the square's edges are creases, where the map to the sphere turns back on itself, so a read across one is a
        // coarser reconstruction than one in the middle even of the right texels
        {EnvMapping_EqualArea, "left fold", {0.f, 14.4f}, {1.f, 0.f}, 1.75f},
        {EnvMapping_EqualArea, "top fold", {50.4f, 0.f}, {0.f, 1.f}, 1.75f},
    };

    for (const Edge &e : edges)
    {
        CAPTURE(std::string(name_of(e.mapping)));
        CAPTURE(std::string(e.what));
        const EnvMapping mapping = EnvMapping(e.mapping);
        const int2       size    = size_for(e.mapping, 72);
        auto             valid   = [&](int x, int y)
        { return envmap_uv_is_valid(mapping, float2{(x + 0.5f) / float(size.x), (y + 0.5f) / float(size.y)}); };

        // four texels wide, centered four texels in, which puts the edge where the lobe is steepest; a texel is the
        // angle the image's texels cover on average
        int texels = 0;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) texels += valid(x, y);
        const float    width = 4.f * std::sqrt(4.f * 3.14159265f / float(texels));
        const float3   c     = envmap_uv_to_xyz(mapping, (e.at + 4.f * e.inward) / float2(size));
        const Array2Df lobe  = make_envmap(size, mapping, [&](float3 d)
                                           { return 0.2f + std::exp(-(1.f - la::dot(d, c)) / (width * width)); });

        // a cube layout's edges are those of its faces
        const int    face   = shader::envmap_is_cube(e.mapping) ? shader::envmap_face_size(e.mapping, size) : 0;
        const float2 extent = face ? float2(float(face)) : float2(size);

        for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
        {
            CAPTURE(sampler);
            const Array2Df out = remapped_envmap(lobe, size, mapping, mapping, EnvMapSampling(sampler), 4);

            // the worst relative error within two texels of an edge, and elsewhere
            float by_edge = 0.f, inside = 0.f;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    if (!valid(x, y))
                        continue;
                    const float2 q = face ? float2{std::fmod(x + 0.5f, float(face)), std::fmod(y + 0.5f, float(face))}
                                          : float2{x + 0.5f, y + 0.5f};
                    float       &worst = la::minelem(la::min(q, extent - q)) < 2.f ? by_edge : inside;
                    worst              = std::max(worst, std::abs(out(x, y) - lobe(x, y)) / lobe(x, y));
                }
            CAPTURE(by_edge);
            CAPTURE(inside);
            REQUIRE(inside > 0.f);
            CHECK(by_edge < e.allowance * inside);
        }
    }
}

TEST_CASE("Irradiance is the cosine-weighted average of what each mapping holds, on every band")
{
    // Nine spherical harmonics hold any radiance of degree two or less exactly, and the clamped cosine scales each band
    // by its own factor, so the irradiance over pi of a polynomial radiance is known in closed form: band 0 passes
    // whole, band 1 at 2/3 and band 2 at 1/4. A uniform environment comes back as itself, but only if the weights
    // over the image sum to the sphere's 4*pi steradians; the band-1 ones each light a hemisphere, whose step has no
    // band-2 part, so a surface facing the lit half sees all of it, one facing away none, and one edge-on half; and the
    // band-2 ones are the five functions of that band, so every coefficient is set in turn and one read in place of
    // another shows.
    struct Light
    {
        const char *what;
        float (*radiance)(float3);
        float (*irradiance)(float3); ///< over pi
        float tolerance = 0.01f;     ///< what integrating over the source's samples costs, more for a hard edge
    };
    const Light lights[] = {
        {"uniform", [](float3) { return 0.6f; }, [](float3) { return 0.6f; }},
        {"+x half", [](float3 d) { return d.x > 0.f ? 1.f : 0.f; }, [](float3 n) { return 0.5f + 0.5f * n.x; }, 0.02f},
        {"+y half", [](float3 d) { return d.y > 0.f ? 1.f : 0.f; }, [](float3 n) { return 0.5f + 0.5f * n.y; }, 0.02f},
        {"+z half", [](float3 d) { return d.z > 0.f ? 1.f : 0.f; }, [](float3 n) { return 0.5f + 0.5f * n.z; }, 0.02f},
        {"xy", [](float3 d) { return 0.5f + d.x * d.y; }, [](float3 n) { return 0.5f + 0.25f * n.x * n.y; }},
        {"yz", [](float3 d) { return 0.5f + d.y * d.z; }, [](float3 n) { return 0.5f + 0.25f * n.y * n.z; }},
        {"xz", [](float3 d) { return 0.5f + d.x * d.z; }, [](float3 n) { return 0.5f + 0.25f * n.x * n.z; }},
        {"x^2 - y^2", [](float3 d) { return 0.5f + d.x * d.x - d.y * d.y; },
         [](float3 n) { return 0.5f + 0.25f * (n.x * n.x - n.y * n.y); }},
        {"3z^2 - 1", [](float3 d) { return 0.5f + 0.5f * (3.f * d.z * d.z - 1.f); },
         [](float3 n) { return 0.5f + 0.125f * (3.f * n.z * n.z - 1.f); }},
    };

    for (int m = 0; m < EnvMapping_COUNT; ++m)
        for (const Light &light : lights)
        {
            CAPTURE(std::string(name_of(m)));
            CAPTURE(std::string(light.what));
            const Array2Df src = make_envmap(size_for(m, 96), EnvMapping(m), light.radiance);
            const int2     size{16, 16};
            const Array2Df out = irradiance_envmap(src, size, EnvMapping(m));

            float worst = 0.f;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    const float2 uv{(x + 0.5f) / float(size.x), (y + 0.5f) / float(size.y)};
                    if (envmap_uv_is_valid(EnvMapping(m), uv))
                        worst = std::max(worst,
                                         std::abs(out(x, y) - light.irradiance(envmap_uv_to_xyz(EnvMapping(m), uv))));
                }
            CAPTURE(worst);
            CHECK(worst < light.tolerance);
        }
}

TEST_CASE("EWA blurs by half an output pixel whatever shape the reduction is, and keeps out what is past Nyquist")
{
    // A lat-long remapped onto itself at another size is a pure scaling, the same at every pixel, so a sinusoid in the
    // source comes out a sinusoid, attenuated by the filter's response at its frequency. A Gaussian of sigma output
    // pixels passes exp(-2 pi^2 sigma^2 nu^2) of a frequency of nu cycles per pixel, which gives the blur's sigma from
    // what comes through at a quarter cycle per pixel, and a frequency past Nyquist folds back onto one below it,
    // three quarters onto one quarter, which says how much aliases.
    //
    // Reductions differ in kind, not only degree: an isotropic one exercises the mip levels, a lopsided one the reads
    // along the long axis, and a slightly lopsided one the few reads that make up a little more along it than across,
    // and a filter can be right about one and wrong about another; scales of 2 and 4 put the output's pixels on the
    // texels of the half- and quarter-size levels, where 1.5, 3 and 6 do not.
    const int W = 480, H = 240;

    struct Shape
    {
        const char *what;
        float       kx, ky; ///< Source pixels per output pixel across and down
        int         max_aniso;
    };
    const Shape shapes[] = {{"isotropic", 1.5f, 1.5f, 8},
                            {"isotropic", 2.f, 2.f, 8},
                            {"isotropic", 3.f, 3.f, 8},
                            {"isotropic", 4.f, 4.f, 8},
                            {"isotropic", 6.f, 6.f, 8},
                            {"slightly lopsided", 4.f, 3.f, 8},
                            {"slightly lopsided", 3.f, 2.5f, 8},
                            {"reduced in x only", 4.f, 1.f, 8},
                            {"reduced further in x than in y", 6.f, 2.f, 8},
                            {"too eccentric for its anisotropy cap", 8.f, 1.f, 1}};

    // EWA is linear, and a sinusoid along one axis is orthogonal to any along the other, so one remap measures a probe
    // along each axis at once
    for (const Shape &shape : shapes)
    {
        CAPTURE(std::string(shape.what));
        CAPTURE(shape.kx);
        CAPTURE(shape.ky);

        const int2   size{int(std::lround(W / shape.kx)), int(std::lround(H / shape.ky))};
        const int2   src_size{W, H};
        const float2 k{shape.kx, shape.ky};

        // at several phases, since where the output's pixels fall on the source's texels changes what a read blurs by,
        // and can put an aliased frequency's samples on its zeros
        for (double offset : {0.0, 0.25 * M_PI, 0.5 * M_PI, 0.75 * M_PI})
            // the blur probe, a quarter cycle per output pixel, and the aliasing one, three quarters
            for (bool aliasing : {false, true})
            {
                CAPTURE(offset);
                CAPTURE(aliasing);
                // An axis the shape does not reduce is not probed: at the source's own scale reads land on its texels,
                // and the capped shape's short axis is widened on purpose. The aliasing probe, 3/(4k) cycles per source
                // pixel, is below the source's own Nyquist only for k > 1.5.
                bool probed[2];
                int  cycles[2];
                for (int a = 0; a < 2; ++a)
                {
                    probed[a] = k[a] > 1.f && (!aliasing || k[a] > 1.5f);
                    cycles[a] = aliasing ? size[a] - size[a] / 4 : size[a] / 4;
                }
                if (!probed[0] && !probed[1])
                    continue;

                std::vector<float> probe[2];
                for (int a = 0; a < 2; ++a)
                    for (int i = 0; i < src_size[a]; ++i)
                        probe[a].push_back(
                            probed[a]
                                ? 0.25f * float(std::cos(2.0 * M_PI * cycles[a] * (i + 0.5) / src_size[a] + offset))
                                : 0.f);
                Array2Df src{src_size};
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x) src(x, y) = 0.5f + probe[0][size_t(x)] + probe[1][size_t(y)];
                const Array2Df out = remapped_envmap(src, size, EnvMapping_LatLong, EnvMapping_LatLong,
                                                     EnvMapSampling_EWA, shape.max_aniso);

                for (int a = 0; a < 2; ++a)
                {
                    if (!probed[a])
                        continue;
                    CAPTURE(a);
                    // how much of the probe's amplitude comes out at a quarter cycle per output pixel along its axis
                    double c = 0.0, s = 0.0;
                    for (int y = 0; y < size.y; ++y)
                        for (int x = 0; x < size.x; ++x)
                        {
                            const double phase = 2.0 * M_PI * (size[a] / 4) * ((a == 0 ? x : y) + 0.5) / size[a];
                            c += out(x, y) * std::cos(phase);
                            s += out(x, y) * std::sin(phase);
                        }
                    const double response = std::sqrt(c * c + s * s) * 2.0 / double(size.x * size.y) / 0.25;
                    if (!aliasing)
                    {
                        const double sigma = std::sqrt(-std::log(response) / (2.0 * M_PI * M_PI / 16.0));
                        CHECK(sigma > 0.45);
                        CHECK(sigma < 0.55);
                    }
                    else
                        // a Gaussian of half a pixel passes 0.06 here; one read per level, or levels an octave apart,
                        // two or three times that
                        CHECK(response < 0.15);
                }
            }
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

TEST_CASE("A mapping's aspect matches the area its image actually covers")
{
    // A remap to a size that ignores the proportions a parameterization asks for stretches the result or
    // throws away resolution along one axis. Those proportions are what its layout packs into the image, so
    // the fraction of the image that stands for a direction is measured from the mapping itself and checked
    // against the same layout.
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        // a full turn across by half a turn down, three faces across by four down, one across by six down,
        // or a square
        const bool   is_disc = m == EnvMapping_Angular || m == EnvMapping_MirrorBall;
        const double aspect  = m == EnvMapping_LatLong || m == EnvMapping_Cylindrical ? 2.0
                               : m == EnvMapping_CubeMap                              ? 3.0 / 4.0
                               : m == EnvMapping_CubeMapColumn                        ? 1.0 / 6.0
                                                                                      : 1.0;
        CHECK(double(envmapping_aspect(m)) == doctest::Approx(aspect));

        // a fine sampling grid so both axes are resolved; count where the mapping has a direction
        const int n       = 240;
        int       covered = 0;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (envmap_uv_is_valid(EnvMapping(m), float2{(x + 0.5f) / n, (y + 0.5f) / n}))
                    ++covered;

        // the discs cover pi/4 of their square; the cross uses six of the twelve cells of its 3-by-4 grid,
        // the column of four plus the two side faces, and the rest fill their image
        const double fraction = double(covered) / double(n) * (1.0 / double(n));
        if (is_disc)
            CHECK(fraction == doctest::Approx(3.14159265358979 / 4.0).epsilon(0.01));
        else if (m == EnvMapping_CubeMap)
            CHECK(fraction == doctest::Approx(6.0 / 12.0).epsilon(0.01));
        else
            CHECK(fraction == doctest::Approx(1.0));
    }
}

TEST_CASE("The mip level is blended continuously across the finest one")
{
    // A remap at the source's own size sits where magnifying turns into minifying, along a curve across the image.
    // The blend between levels must be continuous there, or that curve shows as a seam.
    //
    // Alternating columns, which the finest level holds and an octave down averages flat, so how much of the
    // output's variation survives says which level was read.
    Array2Df src{int2{256, 256}};
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) src(x, y) = float(x % 2);

    // spread about the mean, which compares across destinations of different sizes
    auto variation = [&](int n)
    {
        const Array2Df out =
            remapped_envmap(src, int2{n, n}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 8);

        double mean = 0.0;
        for (int i = 0; i < out.num_elements(); ++i) mean += double(out(i));
        mean /= double(out.num_elements());

        double var = 0.0;
        for (int i = 0; i < out.num_elements(); ++i) var += (double(out(i)) - mean) * (double(out(i)) - mean);
        return std::sqrt(var / double(out.num_elements()));
    };

    // a hundredth of a level either side of the boundary, and an octave (two levels) past it
    const double just_over = variation(255), at = variation(256), just_under = variation(257);
    const double one_octave = variation(128);

    CAPTURE(just_over);
    CAPTURE(at);
    CAPTURE(just_under);
    CAPTURE(one_octave);

    // the level is reached at all: the finest holds the columns and an octave down they are averaged away, or
    // the comparison below says nothing
    REQUIRE(at > 0.05);
    REQUIRE(one_octave < 0.25 * at);

    // and the two sides of the boundary agree far more closely than an octave's worth of difference. Only
    // these two are compared: at the source's own size the samples land on the columns instead of drifting
    // across them, which changes the spread by itself.
    CHECK(std::abs(just_under - just_over) < 0.2 * (just_over - one_octave));
}
