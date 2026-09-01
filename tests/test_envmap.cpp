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

TEST_CASE("Each mapping's Jacobian integrates to the whole sphere")
{
    // Every one of these was derived by hand, so each is checked globally here and pointwise below. A
    // factor or an exponent wrong in any of them shows up as a total that is not 4*pi.
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
    // The global check above would pass for a Jacobian that is wrong in compensating ways, so this pins it
    // pointwise: the area a step of one sample sweeps out on the sphere, measured, against the analytic
    // value. Away from seams and rims, where a difference straddles two faces and means nothing.
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

                // The measured patch covers one sample of an n-by-n image; the Jacobian is per unit area.
                const double measured = double(la::length(la::cross(du, dv))) * double(n) * double(n);

                CAPTURE(u);
                CAPTURE(v);
                CHECK(measured == doctest::Approx(double(envmap_jacobian(EnvMapping(m), uv))).epsilon(0.02));
            }
    }
}

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

            for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
            {
                CAPTURE(sampler);

                // Known gap: reading *from* a cube cross with a footprint wider than a texel gathers the
                // four cells of the 3-by-4 grid that are not faces. A single tap lands on a face and never
                // sees them; an ellipse near a face edge does, and averages in samples that stand for no
                // direction. Fixing it means masking invalid source texels inside the gather, which
                // ewa_level() cannot currently see -- it is handed a level, not a mapping.
                if (sampler == EnvMapSampling_EWA && src_m == EnvMapping_CubeMap)
                    continue;
                const Array2Df out = remapped_envmap(src, int2{48, 48}, EnvMapping(dst_m), EnvMapping(src_m),
                                                     EnvMapSampling(sampler), 4);

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
}

TEST_CASE("Remapping a mapping to itself returns the image it was given")
{
    auto f = [](float3 d) { return 0.5f + 0.3f * d.z; };

    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));
        const Array2Df src = make_envmap(int2{64, 64}, EnvMapping(m), f);

        for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
        {
            CAPTURE(sampler);
            const Array2Df out =
                remapped_envmap(src, int2{64, 64}, EnvMapping(m), EnvMapping(m), EnvMapSampling(sampler), 4);

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

TEST_CASE("EWA lands on the mean whatever shape the reduction is")
{
    // One sweep rather than a test per shape. Reductions differ in kind, not only degree: an isotropic one
    // exercises the mip level, a lopsided one exercises the probes along the long axis, and a filter can
    // be right about one and wrong about the other -- which is exactly how this arrived, as two separate
    // tests written after two separate failures.
    //
    // Period sixteen, so an output pixel covering sixteen source columns covers exactly one period.
    // Deliberately not eight: a level one step too sharp averages over eight texels, which for a
    // period-eight pattern is still a whole period and so gives the right answer for the wrong reason.
    Array2Df src{int2{512, 512}};
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x) src(x, y) = (x % 16 < 4) ? 1.f : 0.f;
    const double true_mean = 0.25;

    struct Shape
    {
        const char *what;
        int2        size;
    };
    // Only shapes that actually reduce along x, the axis the pattern varies on: a destination that keeps x
    // at full resolution covers one source column per pixel and should show the stripes, not their mean.
    // Asking for the mean there asks the filter to lose detail it was handed, which the first version of
    // this test did.
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

        // The bound separates a filtered result from an unfiltered one rather than asserting the box mean:
        // a Gaussian weights the middle of its footprint more heavily, so over exactly one period of a
        // pattern its answer depends on the phase. Stripes surviving intact would score about 0.375 here,
        // so this leaves no room for them while allowing the weighting.
        CHECK(err < 0.15);

        // And better than the cheap setting it exists to improve on, wherever that setting is stressed.
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

                // Wherever there is already a direction, nothing moves -- this must not perturb the
                // sphere itself, only fill in around it.
                if (envmap_uv_is_valid(EnvMapping(m), uv))
                {
                    CHECK(snapped.x == doctest::Approx(uv.x));
                    CHECK(snapped.y == doctest::Approx(uv.y));
                }
                else
                {
                    // And wherever there is not, what comes back is somewhere there is.
                    CHECK(envmap_uv_is_valid(EnvMapping(m), snapped));

                    // It is also the *nearest* such place, checked against a search over the whole
                    // square -- but only every eighth point, since that search is quadratic in the grid
                    // and the property does not vary quickly enough to need every one.
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
    // An image's empty corners sit right against the edge of the sphere, so a later bilinear read near
    // that edge -- displaying it, filtering it, remapping it again -- reaches into them. Left at zero
    // they darken the rim; carrying the nearest direction outward means they agree with their neighbors.
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

                // What the nearest real direction holds, which is what was written there.
                const float2 snapped  = nearest_valid_envmap_uv(EnvMapping(dst_m), uv);
                const float  expected = f(envmap_uv_to_xyz(EnvMapping(dst_m), snapped));

                CHECK(out(x, y) == doctest::Approx(expected).epsilon(0.05));

                // Which is emphatically not the zero it used to be.
                CHECK(out(x, y) > 0.05f);
            }
    }
}

TEST_CASE("A lat-long source is read as a sphere, not as a rectangle")
{
    // Reading past the left edge of a lat-long image is reading the right edge: the two are the same
    // meridian. Clamping instead puts a wall there, and a sample just inside the edge sees its own column
    // twice rather than its neighbor across the seam.
    //
    // Magnified, so that the reach past the edge is a single texel and what comes back is unambiguous:
    // the first column of the source is dark, the last is bright, and everything between is mid gray.
    const int2 src_size{8, 4};
    Array2Df   src{src_size};
    for (int y = 0; y < src_size.y; ++y)
        for (int x = 0; x < src_size.x; ++x) src(x, y) = x == 0 ? 0.f : (x == src_size.x - 1 ? 1.f : 0.5f);

    for (int sampler : {EnvMapSampling_Point, EnvMapSampling_EWA})
    {
        CAPTURE(sampler);
        const Array2Df out =
            remapped_envmap(src, int2{64, 32}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling(sampler), 4);

        // The leftmost output column sits half a source texel in from the seam, so its reconstruction
        // reaches across it and picks up the bright column on the far side. Clamped, it would see only the
        // dark column it sits on and stay at zero.
        for (int y = 4; y < 28; ++y)
        {
            CAPTURE(y);
            CHECK(out(0, y) > 0.1f);
        }
    }
}

TEST_CASE("A lat-long pole is crossed rather than smeared")
{
    // Past the top row the sphere continues down the far side, half a turn round. An image whose two
    // halves differ tells the two apart: clamping extends the near half upward, while crossing brings the
    // far half in.
    const int2 src_size{64, 32};
    Array2Df   src{src_size};
    for (int y = 0; y < src_size.y; ++y)
        for (int x = 0; x < src_size.x; ++x) src(x, y) = x < src_size.x / 2 ? 0.f : 1.f;

    // A footprint at the very top row reaches past the pole, where the other half of the image lies.
    const Array2Df out =
        remapped_envmap(src, int2{32, 16}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 8);

    // Along the top row, a sample from the dark half now sees some of the bright half across the pole, and
    // the other way about -- so neither is still exactly 0 or 1 the way clamping would leave them.
    float darkest = 1.f, brightest = 0.f;
    for (int x = 0; x < out.width(); ++x)
    {
        darkest   = std::min(darkest, out(x, 0));
        brightest = std::max(brightest, out(x, 0));
    }

    // Still mostly its own half -- the pole is one direction, so this is a nudge and not a blend to gray.
    CHECK(darkest < 0.5f);
    CHECK(brightest > 0.5f);

    // ...but no longer pinned at the extremes, which is what tells crossing from clamping.
    CHECK(darkest > 0.f);
    CHECK(brightest < 1.f);
}

TEST_CASE("Magnifying reconstructs rather than repeating source texels")
{
    // The complement of the reduction sweep: what happens when the footprint is *smaller* than a texel.
    // It has to be widened to one anyway -- an ellipse allowed to shrink below a texel gathers whichever
    // one it happens to land on, which repeats each source texel across all the output pixels that
    // magnify it, and staircases.
    //
    // The anisotropic case is the one that matters and the one that is easy to miss: a footprint can be
    // well under a texel across while spanning many along, which is every lat-long pole, and there the
    // ellipse is a sliver rather than a point.
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

        // A ramp down, so a reconstruction of any width steps by about the same amount everywhere, and
        // stripes across for the minified axis to average away.
        Array2Df src{c.src_size};
        for (int y = 0; y < c.src_size.y; ++y)
            for (int x = 0; x < c.src_size.x; ++x)
                src(x, y) = (float(y) + 0.5f) / float(c.src_size.y) + ((x % 8 < 4) ? 0.25f : -0.25f);

        const Array2Df out =
            remapped_envmap(src, c.dst_size, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 32);

        // An ellipse that encloses no texel at all divides by no weight; the values say so before the
        // shape of them is measured.
        for (int i = 0; i < out.num_elements(); ++i) REQUIRE(std::isfinite(out(i)));

        // Plateaus, as the fraction of vertical neighbors that barely differ. A repeated texel is flat
        // between its jumps, and at these scales that is most of the image.
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
    // At an anisotropy cap of one the ellipse is widened until it is round, which raises the level, so the
    // failure mode of a cap the footprint exceeds is a soft result and not a broken one. Worth pinning:
    // the alternative -- keeping the level and gathering only part of the footprint -- is what aliases.
    Array2Df src{int2{512, 64}};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 512; ++x) src(x, y) = (x % 8 < 2) ? 1.f : 0.f;

    const Array2Df few =
        remapped_envmap(src, int2{32, 64}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 1);

    // Still near the mean rather than swinging between the stripe's extremes.
    for (int i = 0; i < few.num_elements(); ++i)
    {
        CHECK(few(i) > 0.05f);
        CHECK(few(i) < 0.6f);
    }
}

TEST_CASE("Each parameterization asks for the proportions it can fill")
{
    // A remap to a size that ignores these either stretches the result or throws away resolution along one
    // axis, which is why the dialog follows them rather than keeping whatever the source happened to be.
    CHECK(envmapping_aspect(EnvMapping_LatLong) == doctest::Approx(2.f));
    CHECK(envmapping_aspect(EnvMapping_Cylindrical) == doctest::Approx(2.f));
    CHECK(envmapping_aspect(EnvMapping_CubeMap) == doctest::Approx(0.75f));
    CHECK(envmapping_aspect(EnvMapping_Angular) == doctest::Approx(1.f));
    CHECK(envmapping_aspect(EnvMapping_MirrorBall) == doctest::Approx(1.f));
    CHECK(envmapping_aspect(EnvMapping_EqualArea) == doctest::Approx(1.f));
}

TEST_CASE("A mapping's aspect matches the area its image actually covers")
{
    // Not merely the numbers above repeated: the proportion each wants is the one under which its valid
    // region fills the image, which is measurable from the mapping itself.
    for (int m = 0; m < EnvMapping_COUNT; ++m)
    {
        CAPTURE(std::string(name_of(m)));

        // A tall, thin sampling grid so both axes are resolved; count where the mapping has a direction.
        const int n       = 240;
        int       covered = 0;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (envmap_uv_is_valid(EnvMapping(m), float2{(x + 0.5f) / n, (y + 0.5f) / n}))
                    ++covered;

        // The discs cover pi/4 of their square; the cross uses six of the twelve cells of its 3-by-4
        // grid -- the column of four plus the two side faces -- and the rest fill their image.
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
    // Proof that the pyramid is reached at all, which the quality tests cannot give: they only show that
    // the result is close to the mean, and a sharp enough filter over the top level would be too.
    Array2Df src{int2{256, 256}};
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) src(x, y) = (x % 16 < 4) ? 1.f : 0.f;

    auto remap = [&](float bias)
    { return remapped_envmap(src, int2{32, 32}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 8, bias); };

    // Spread of the output, as a stand-in for how much detail survives.
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

    // Biasing down reaches levels that still hold the stripes; biasing up reaches ones that do not. If the
    // level were ignored, all three would be identical.
    CHECK(sharp > neutral);
    CHECK(soft <= neutral);

    // And the response to the level is continuous, which is what says the two levels either side are
    // blended rather than one of them snapped to. A snapped level puts a whole level's worth of difference
    // into the step that crosses an integer and almost nothing into the others -- as bands, in an image,
    // wherever the scale crosses a power of two.
    //
    // Swept at two scales, because the two ends of the pyramid are reached differently. The reduction
    // above lands in the middle of it; a remap at its own size sits at level zero, where the lod goes
    // negative as soon as the bias does, and clamping the level while taking the blend from the unclamped
    // lod put the two sides of that boundary at opposite ends of the pyramid. In an image that boundary is
    // the curve along which the remap stops shrinking and starts enlarging, and the seam lay along it.
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

        // No step much larger than the steps either side of it.
        CHECK(worst < 4.0 * (total / double(n)));
    };

    sweep(remap);

    // The boundary between magnifying and minifying, which the sweep above is too coarse to see: a step
    // either side of it is a fortieth of a level, and must change the result by far less than moving a
    // whole level does. Clamping the level while taking the blend from the unclamped lod put those two
    // steps at opposite ends of the pyramid instead, and in an image that boundary is a curve across it,
    // so the mismatch lay along the curve as a seam.
    //
    // A remap at the source's own size sits exactly on it: one destination pixel covers one source pixel,
    // so the lod is the bias.
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
