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
            const Array2Df out =
                remapped_envmap(src, int2{48, 48}, EnvMapping(dst_m), EnvMapping(src_m), EnvMapSampling_Point, 2);

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
        const Array2Df out = remapped_envmap(src, int2{64, 64}, EnvMapping(m), EnvMapping(m), EnvMapSampling_Point, 2);

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

TEST_CASE("EWA carries a value to the direction it belongs to, as point sampling does")
{
    // Whatever the filter, a smooth function of direction has to come back as that same function of the
    // destination's directions. This is the property that says the footprint is being taken in the right
    // place, rather than merely that something was averaged.
    auto f = [](float3 d) { return 0.5f + 0.25f * d.y + 0.15f * d.x; };

    const Array2Df src = make_envmap(int2{128, 128}, EnvMapping_LatLong, f);
    const Array2Df out = remapped_envmap(src, int2{48, 48}, EnvMapping_Angular, EnvMapping_LatLong, EnvMapSampling_EWA);

    for (int y = 16; y < 32; ++y)
        for (int x = 16; x < 32; ++x)
        {
            const float2 uv{(x + 0.5f) / 48.f, (y + 0.5f) / 48.f};
            if (!envmap_uv_is_valid(EnvMapping_Angular, uv))
                continue;
            CAPTURE(x);
            CAPTURE(y);
            CHECK(out(x, y) == doctest::Approx(f(envmap_uv_to_xyz(EnvMapping_Angular, uv))).epsilon(0.05));
        }
}

TEST_CASE("EWA beats point sampling on a heavy reduction")
{
    // What the option is for. A high-frequency source shrunk hard aliases under a handful of point
    // samples, where an elliptical filter over a pyramid averages the whole footprint -- so its result
    // sits far closer to the true mean of the source.
    // Narrow stripes: two lit columns in every eight, so a quarter of the source is lit. A tap lands where
    // it lands and reports what is under it -- here, on the lit edge -- while the footprint it stands for
    // is three quarters dark. A checkerboard would not do: its local average is its global one, so every
    // filter gets the right answer for the wrong reason.
    // Period sixteen, so that one output pixel covers exactly one period. Deliberately not eight: a level
    // one step too sharp averages over eight texels, which for a period-eight pattern is still exactly one
    // period and so gives the right answer for the wrong reason.
    Array2Df src{int2{256, 256}};
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) src(x, y) = (x % 16 < 4) ? 1.f : 0.f;
    const double true_mean = 0.25;

    const Array2Df ewa = remapped_envmap(src, int2{16, 16}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA);
    // One tap per output pixel, which is what "point sampling" costs when it is chosen for speed. The taps
    // are bilinear, so at two or more per axis they already average this particular pattern away -- the
    // gap the option exists to close is the cheap setting, and a heavier reduction than the sample count
    // can cover.
    const Array2Df point =
        remapped_envmap(src, int2{16, 16}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_Point, 1);

    // Each output pixel covers sixteen source columns, which is two whole periods of the pattern, so the
    // answer everywhere is the pattern's mean.
    double ewa_err = 0.0, point_err = 0.0;
    for (int i = 0; i < ewa.num_elements(); ++i)
    {
        ewa_err += std::abs(double(ewa(i)) - true_mean);
        point_err += std::abs(double(point(i)) - true_mean);
    }

    CHECK(ewa_err < point_err);

    // And in absolute terms, not merely better: each output pixel covers two whole periods, so a filter
    // that is covering its footprint lands on the mean. Choosing a mip level one step too sharp -- which
    // halving the extents before taking the logarithm does -- leaves the stripes partly intact and fails
    // this while still comfortably beating a single tap.
    CHECK(ewa_err / double(ewa.num_elements()) < 0.05);
}

TEST_CASE("EWA holds up when the footprint is far wider than it is tall")
{
    // The case the whole design is for, and the one a mip level alone cannot serve: reducing hard along
    // one axis and not at all along the other. Choosing the level from the short axis leaves the long one
    // to the probes, and too few of them shows up here as the stripes surviving instead of averaging.
    Array2Df src{int2{512, 64}};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 512; ++x) src(x, y) = (x % 8 < 2) ? 1.f : 0.f;
    const double true_mean = 0.25;

    // Sixteen source columns per output column, one source row per output row: an aspect of 16 to 1.
    const Array2Df out =
        remapped_envmap(src, int2{32, 64}, EnvMapping_LatLong, EnvMapping_LatLong, EnvMapSampling_EWA, 16);

    double err = 0.0;
    for (int i = 0; i < out.num_elements(); ++i) err += std::abs(double(out(i)) - true_mean);
    err /= double(out.num_elements());

    CHECK(err < 0.05);
}

TEST_CASE("Too few taps blurs rather than aliases")
{
    // When the probes cannot walk the footprint the level has to rise to cover it, so the failure mode of
    // an inadequate tap budget is a soft result and not a broken one. Worth pinning: the alternative --
    // keeping the level and sampling too sparsely -- is what aliases.
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

TEST_CASE("The mip level is doing something, and the bias moves it")
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
}
