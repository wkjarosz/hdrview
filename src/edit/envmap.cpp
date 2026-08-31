//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/envmap.h"

#include "common.h"

#include <smallthreadpool.h>

#include <algorithm>
#include <cmath>

// Ported from HDRView 1.8's envmap.cpp, with linalg vectors in place of nanogui's.

namespace
{

constexpr float k_pi      = 3.14159265358979323846f;
constexpr float k_half_pi = k_pi / 2.f;

float sqr(float x) { return x * x; }

// -------------------------------------------------------------------------------------------------
// image point -> direction
// -------------------------------------------------------------------------------------------------

float3 angular_to_xyz(float2 uv)
{
    // image plane coordinates going from (-1,1) for x and y
    // with center of image being (0,0)
    const float2 xy = 2.f * uv - float2{1.f};

    // phi varies linearly with the radius from center
    const float phi   = std::clamp(la::length(xy) * k_pi, 0.f, k_pi);
    const float theta = std::atan2(xy.y, xy.x);

    const float sin_phi = std::sin(phi);
    return float3{sin_phi * std::cos(theta), -sin_phi * std::sin(theta), std::cos(phi)};
}

float3 mirror_ball_to_xyz(float2 uv)
{
    // image plane coordinates going from (-1,1) for x and y
    // with center of image being (0,0)
    const float2 xy = 2.f * uv - float2{1.f};

    // sin(phi) varies linearly with the radius from center, which is what a photograph of a mirrored
    // sphere records
    const float phi   = 2.f * std::asin(std::clamp(la::length(xy), 0.f, 1.f));
    const float theta = std::atan2(xy.y, xy.x);

    const float sin_phi = std::sin(phi);
    return float3{sin_phi * std::cos(theta), -sin_phi * std::sin(theta), std::cos(phi)};
}

float3 lat_long_to_xyz(float2 uv)
{
    // theta varies linearly with U,
    // and phi varies linearly with V
    const float theta = lerp(1.5f * k_pi, -k_half_pi, uv.x);
    const float phi   = uv.y * k_pi;

    const float sin_phi = std::sin(phi);
    return float3{sin_phi * std::cos(theta), std::cos(phi), sin_phi * std::sin(theta)};
}

float3 cylindrical_to_xyz(float2 uv)
{
    // theta varies linearly with U,
    // and y=cosPhi varies linearly with V -- so every row covers the same solid angle, which lat-long
    // does not
    const float theta   = lerp(1.5f * k_pi, -k_half_pi, uv.x);
    const float cos_phi = lerp(1.f, -1.f, uv.y);

    const float sin_phi = std::sqrt(std::max(0.f, 1.f - cos_phi * cos_phi));
    return float3{sin_phi * std::cos(theta), cos_phi, sin_phi * std::sin(theta)};
}

//! The cube face vector for \p uv before normalizing: one component is +-1 and the other two are the
//! face coordinates. Its length is what the Jacobian needs, so the two share this.
float3 cube_map_face_vector(float2 uv)
{
    // This is assuming that the Cubemap is a vertical cross: the upright column of four down the middle
    // third, and the two side faces either side of it
    float3 xyz{0.f};

    if (uv.x >= 1.f / 3.f && uv.x <= 2.f / 3.f)
    {
        xyz.x = (uv.x - 0.5f) * 6.f;
        if (uv.y <= 0.25f)
        {
            xyz.y = 1.f;
            xyz.z = (uv.y - 0.125f) * 8.f;
        }
        else if (uv.y <= 0.5f)
        {
            xyz.y = (0.375f - uv.y) * 8.f;
            xyz.z = 1.f;
        }
        else if (uv.y <= 0.75f)
        {
            xyz.y = -1.f;
            xyz.z = (0.625f - uv.y) * 8.f;
        }
        else
        {
            xyz.y = (uv.y - 0.875f) * 8.f;
            xyz.z = -1.f;
        }
    }
    else if (uv.x < 1.f / 3.f)
    {
        xyz.x = -1.f;
        xyz.y = (0.375f - std::clamp(uv.y, 0.25f, 0.5f)) * 8.f;
        xyz.z = (std::clamp(uv.x, 0.f, 1.f / 3.f) - 1.f / 6.f) * 6.f;
    }
    else
    {
        xyz.x = 1.f;
        xyz.y = (0.375f - std::clamp(uv.y, 0.25f, 0.5f)) * 8.f;
        xyz.z = (5.f / 6.f - std::clamp(uv.x, 2.f / 3.f, 1.f)) * 6.f;
    }

    return xyz;
}

float3 cube_map_to_xyz(float2 uv) { return la::normalize(cube_map_face_vector(uv)); }

//
// Equal-area, adapted from PBRTv4, itself from Clarberg, "Fast Equal-Area Mapping of the (Hemi)Sphere
// using SIMD".
//
float3 equal_area_to_xyz(float2 p)
{
    const float u = 2.f * p.x - 1.f, v = 2.f * p.y - 1.f;
    const float up = std::abs(u), vp = std::abs(v);

    // Radius as the signed distance from the diagonal.
    const float signed_distance = 1.f - (up + vp);
    const float d               = std::abs(signed_distance);
    const float r               = 1.f - d;

    const float phi = (r == 0.f ? 1.f : (vp - up) / r + 1.f) * k_pi / 4.f;
    const float z   = std::copysign(1.f - sqr(r), signed_distance);

    const float cos_phi = std::copysign(std::cos(phi), u);
    const float sin_phi = std::copysign(std::sin(phi), v);
    const float scale   = r * std::sqrt(std::max(0.f, 2.f - sqr(r)));

    return float3{cos_phi * scale, z, -sin_phi * scale};
}

// -------------------------------------------------------------------------------------------------
// direction -> image point
// -------------------------------------------------------------------------------------------------

float2 xyz_to_angular(float3 xyz)
{
    const float phi   = std::acos(std::clamp(xyz.z, -1.f, 1.f));
    const float theta = std::atan2(xyz.y, xyz.x);

    const float u = (phi / k_pi) * std::cos(theta);
    const float v = -(phi / k_pi) * std::sin(theta);
    return float2{0.5f * (u + 1.f), 0.5f * (v + 1.f)};
}

float2 xyz_to_mirror_ball(float3 xyz)
{
    const float phi   = std::acos(std::clamp(xyz.z, -1.f, 1.f));
    const float theta = std::atan2(xyz.y, xyz.x);

    const float sin_half_phi = std::sin(phi / 2.f);
    return float2{0.5f * (sin_half_phi * std::cos(theta) + 1.f), 0.5f * (-sin_half_phi * std::sin(theta) + 1.f)};
}

float2 xyz_to_lat_long(float3 xyz)
{
    const float phi   = std::acos(std::clamp(xyz.y, -1.f, 1.f));
    const float theta = std::atan2(xyz.z, xyz.x);
    return float2{mod(lerp_factor(1.5f * k_pi, -k_half_pi, theta), 1.f), phi / k_pi};
}

float2 xyz_to_cylindrical(float3 xyz)
{
    const float theta = std::atan2(xyz.z, xyz.x);
    return float2{mod(lerp_factor(1.5f * k_pi, -k_half_pi, theta), 1.f), lerp_factor(1.f, -1.f, xyz.y)};
}

float2 xyz_to_cube_map(float3 xyz)
{
    // Again, the CubeMap is a vertical cross.
    // Make sure that the infinite norm of xyz == 1; the face tells us which side we're looking at.
    float l    = std::abs(xyz.x);
    int   face = int(sign(xyz.x));
    if (std::abs(xyz.y) > l)
    {
        l    = std::abs(xyz.y);
        face = int(sign(xyz.y)) * 2;
    }
    if (std::abs(xyz.z) > l)
    {
        l    = std::abs(xyz.z);
        face = int(sign(xyz.z)) * 3;
    }

    // Projected onto that face's plane, so one component is now +-1.
    const float3 t = xyz / l;

    switch (face)
    {
    case 3: return float2{t.x / 6.f + 0.5f, -t.y / 8.f + 0.375f};
    case -1: return float2{t.z / 6.f + 1.f / 6.f, -t.y / 8.f + 0.375f};
    case 1: return float2{-t.z / 6.f + 5.f / 6.f, -t.y / 8.f + 0.375f};
    case 2: return float2{t.x / 6.f + 0.5f, t.z / 8.f + 0.125f};
    case -2: return float2{t.x / 6.f + 0.5f, -t.z / 8.f + 0.625f};
    default: return float2{t.x / 6.f + 0.5f, t.y / 8.f + 0.875f};
    }
}

template <typename Float, typename C>
constexpr Float evaluate_polynomial(Float, C c)
{
    return c;
}

template <typename Float, typename C, typename... Args>
constexpr Float evaluate_polynomial(Float t, C c, Args... remaining)
{
    return t * evaluate_polynomial(t, remaining...) + c;
}

float2 xyz_to_equal_area(float3 d)
{
    // A 90-degree rotation relative to PBRT's frame, so that this agrees with the mappings above.
    d.z *= -1.f;
    std::swap(d.y, d.z);

    const float x = std::abs(d.x), y = std::abs(d.y), z = std::abs(d.z);
    const float r = std::sqrt(std::max(0.f, 1.f - z));

    const float a = std::max(x, y);
    float       b = std::min(x, y);
    b             = a == 0.f ? 0.f : b / a;

    // Minimax polynomial for atan(b)*2/pi over [0,1], to sixth degree.
    float phi = evaluate_polynomial(b, 0.406758566246788489601959989e-5f, 0.636226545274016134946890922156f,
                                    0.61572017898280213493197203466e-2f, -0.247333733281268944196501420480f,
                                    0.881770664775316294736387951347e-1f, 0.419038818029165735901852432784e-1f,
                                    -0.251390972343483509333252996350e-1f);
    if (x < y)
        phi = 1.f - phi;

    float v = phi * r;
    float u = r - v;

    if (d.z < 0.f)
    {
        std::swap(u, v);
        u = 1.f - u;
        v = 1.f - v;
    }

    u = std::copysign(u, d.x);
    v = std::copysign(v, d.y);

    return float2{0.5f * (u + 1.f), 0.5f * (v + 1.f)};
}

} // namespace

const char *envmapping_name(int mapping)
{
    switch (mapping)
    {
    case EnvMapping_Angular: return "Angular map";
    case EnvMapping_MirrorBall: return "Mirror ball";
    case EnvMapping_LatLong: return "Longitude-latitude";
    case EnvMapping_Cylindrical: return "Cylindrical";
    case EnvMapping_CubeMap: return "Cube map (vertical cross)";
    case EnvMapping_EqualArea: return "Equal area";
    default: return "";
    }
}

float envmapping_aspect(int mapping)
{
    switch (mapping)
    {
    case EnvMapping_LatLong:
    case EnvMapping_Cylindrical: return 2.f;   // a full turn across, half a turn down
    case EnvMapping_CubeMap: return 3.f / 4.f; // three faces across by four down
    default: return 1.f;                       // the discs and the equal-area square
    }
}

float3 envmap_uv_to_xyz(EnvMapping mapping, float2 uv)
{
    switch (mapping)
    {
    case EnvMapping_Angular: return angular_to_xyz(uv);
    case EnvMapping_MirrorBall: return mirror_ball_to_xyz(uv);
    case EnvMapping_LatLong: return lat_long_to_xyz(uv);
    case EnvMapping_Cylindrical: return cylindrical_to_xyz(uv);
    case EnvMapping_CubeMap: return cube_map_to_xyz(uv);
    default: return equal_area_to_xyz(uv);
    }
}

float2 envmap_xyz_to_uv(EnvMapping mapping, float3 xyz)
{
    switch (mapping)
    {
    case EnvMapping_Angular: return xyz_to_angular(xyz);
    case EnvMapping_MirrorBall: return xyz_to_mirror_ball(xyz);
    case EnvMapping_LatLong: return xyz_to_lat_long(xyz);
    case EnvMapping_Cylindrical: return xyz_to_cylindrical(xyz);
    case EnvMapping_CubeMap: return xyz_to_cube_map(xyz);
    default: return xyz_to_equal_area(xyz);
    }
}

namespace
{

//! Bilinear read with the borders clamped, in [0,1]^2 image coordinates.
float sample_bilinear(const Array2Df &a, float2 uv)
{
    const float x = uv.x * float(a.width()) - 0.5f;
    const float y = uv.y * float(a.height()) - 0.5f;

    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float tx = x - float(x0), ty = y - float(y0);

    auto at = [&a](int i, int j) { return a(std::clamp(i, 0, a.width() - 1), std::clamp(j, 0, a.height() - 1)); };

    return (1.f - ty) * ((1.f - tx) * at(x0, y0) + tx * at(x0 + 1, y0)) +
           ty * ((1.f - tx) * at(x0, y0 + 1) + tx * at(x0 + 1, y0 + 1));
}

} // namespace

float envmap_jacobian(EnvMapping mapping, float2 uv)
{
    if (!envmap_uv_is_valid(mapping, uv))
        return 0.f;

    constexpr float k_four_pi = 4.f * k_pi;

    switch (mapping)
    {
    case EnvMapping_LatLong:
        // dw = sin(phi) dtheta dphi, with theta spanning 2*pi across u and phi spanning pi down v.
        return 2.f * k_pi * k_pi * std::sin(k_pi * uv.y);

    case EnvMapping_Cylindrical:
        // Archimedes: v is the height rather than the angle, so every row covers the same solid angle.
        return k_four_pi;

    case EnvMapping_EqualArea:
        // What the mapping exists for.
        return k_four_pi;

    case EnvMapping_MirrorBall:
        // Also equal-area, which is less obvious: the sine of half the polar angle growing linearly with
        // the radius is exactly the condition for it.
        return 16.f;

    case EnvMapping_Angular:
    {
        // Here the polar angle itself grows linearly with the radius, so the sky compresses towards the rim.
        const float r = la::length(2.f * uv - float2{1.f});
        // sin(pi*r)/r tends to pi at the center rather than dividing by zero.
        return r < 1e-6f ? k_four_pi * k_pi : k_four_pi * std::sin(k_pi * r) / r;
    }

    default:
    {
        // A cube face's solid angle per unit face area is one over the cube of the distance to it; the
        // cross packs each face into a sixth of the image, which is where the constant comes from.
        const float3 v = cube_map_face_vector(uv);
        const float  l = la::length(v);
        return 48.f / (l * l * l);
    }
    }
}

bool envmap_uv_is_valid(EnvMapping mapping, float2 uv)
{
    switch (mapping)
    {
    case EnvMapping_Angular:
    case EnvMapping_MirrorBall:
        // The sphere fills the inscribed disc; the corners are outside it.
        return la::length(2.f * uv - float2{1.f}) <= 1.f;

    case EnvMapping_CubeMap:
        // The upright column of four faces, plus the two side faces beside its second row.
        return (uv.x >= 1.f / 3.f && uv.x <= 2.f / 3.f) || (uv.y >= 0.25f && uv.y <= 0.5f);

    default: return true;
    }
}

namespace
{

/*!
    A mip pyramid, each level half the size of the one before and averaged from it.

    Built once per remap rather than per sample. Level 0 is the source itself, so the pyramid owns a copy
    of it and every level can be addressed the same way.
*/
std::vector<Array2Df> build_mip_pyramid(const Array2Df &src)
{
    std::vector<Array2Df> levels;
    levels.emplace_back(src);

    while (levels.back().width() > 1 || levels.back().height() > 1)
    {
        const Array2Df &prev = levels.back();
        const int2      size{std::max(1, prev.width() / 2), std::max(1, prev.height() / 2)};

        Array2Df next{size};
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                // The four the new sample sits between, so it lands halfway rather than on one of them.
                // The guards matter on an odd dimension, where the last row or column has no partner.
                const int x0 = std::min(2 * x, prev.width() - 1), x1 = std::min(2 * x + 1, prev.width() - 1);
                const int y0 = std::min(2 * y, prev.height() - 1), y1 = std::min(2 * y + 1, prev.height() - 1);
                next(x, y) = 0.25f * (prev(x0, y0) + prev(x1, y0) + prev(x0, y1) + prev(x1, y1));
            }

        levels.push_back(std::move(next));
    }

    return levels;
}

//! Gather one level of \p levels through the ellipse with axes \p d0 and \p d1 (in uv), centered at \p uv.
/*!
    Every texel the ellipse encloses contributes, weighted by a Gaussian in the ellipse's own space, so the
    filter takes the whole of the footprint's shape. The quadratic \f$A s^2 + B s t + C t^2\f$ is one
    exactly on the ellipse's boundary, which makes the test for "inside" a comparison against one and the
    weight a function of that same number.

    The `+ 1` on \f$A\f$ and \f$C\f$ is the part that is easy to leave out and expensive to: it convolves
    the ellipse with a reconstruction filter one texel across, so a footprint smaller than a texel widens
    into one instead of collapsing onto a point sample. Without it, magnification aliases.

    Follows PBRT's `MIPMap::EWA`.
*/
float ewa_level(const Array2Df &lvl, float2 uv, float2 d0, float2 d1)
{
    const float2 res{float(lvl.width()), float(lvl.height())};

    // Continuous texel coordinates, with the axes in the same units, so the level's own resolution enters
    // in one place and nowhere else.
    const float2 st = uv * res - 0.5f;
    const float2 a = d0 * res, b = d1 * res;

    float A = sqr(a.y) + sqr(b.y) + 1.f;
    float B = -2.f * (a.x * a.y + b.x * b.y);
    float C = sqr(a.x) + sqr(b.x) + 1.f;

    const float inv_f = 1.f / std::max(1e-20f, A * C - 0.25f * sqr(B));
    A *= inv_f;
    B *= inv_f;
    C *= inv_f;

    // The axis-aligned box that just contains the ellipse, which is where the scan runs.
    const float det     = std::max(1e-20f, 4.f * A * C - sqr(B));
    const float inv_det = 1.f / det;
    const float ext_s   = 2.f * inv_det * std::sqrt(std::max(0.f, det * C));
    const float ext_t   = 2.f * inv_det * std::sqrt(std::max(0.f, det * A));

    const int s0 = int(std::ceil(st.x - ext_s)), s1 = int(std::floor(st.x + ext_s));
    const int t0 = int(std::ceil(st.y - ext_t)), t1 = int(std::floor(st.y + ext_t));

    auto at = [&lvl](int i, int j)
    { return lvl(std::clamp(i, 0, lvl.width() - 1), std::clamp(j, 0, lvl.height() - 1)); };

    float sum = 0.f, total = 0.f;
    for (int t = t0; t <= t1; ++t)
    {
        const float dt = float(t) - st.y;
        for (int s = s0; s <= s1; ++s)
        {
            const float ds = float(s) - st.x;
            const float r2 = A * sqr(ds) + B * ds * dt + C * sqr(dt);
            if (r2 >= 1.f)
                continue;

            // Subtracting the value at the boundary lands the Gaussian on zero there rather than stepping
            // off its tail, which would show up as a ring where the ellipse ends.
            const float w = std::exp(-2.f * r2) - std::exp(-2.f);

            sum += w * at(s, t);
            total += w;
        }
    }

    // An ellipse narrow enough to fall between texels encloses none of them; read the level instead.
    return total > 0.f ? sum / total : sample_bilinear(lvl, uv);
}

/*!
    Sample \p levels at \p uv through the footprint \p du by \p dv, allowing at most \p max_aniso of
    eccentricity and shifting the chosen level by \p mip_bias.

    The mip level comes from the ellipse's *shorter* axis, so a texel of that level spans the narrow
    direction and the gather itself covers the long one. That split is what a mip level alone cannot do: a
    lat-long's pole is stretched hundreds of times more across than down, and a level chosen to cover the
    wide direction erases the narrow one.

    Walking a long axis costs a texel per step, so \p max_aniso is where that stops. Past it the *shorter*
    axis is lengthened until the ratio is affordable -- widening the ellipse rather than raising the level,
    since the level would blur both directions when only one of them cannot be afforded.

    Follows PBRT's `MIPMap::Filter`.
*/
float sample_ewa(const std::vector<Array2Df> &levels, float2 uv, float2 du, float2 dv, int max_aniso, float mip_bias)
{
    const float2 base{float(levels[0].width()), float(levels[0].height())};

    // Longer axis first, measured in level-0 texels: du and dv are the step from one destination pixel to
    // the next, so their lengths are how far across the source a destination pixel reaches, and the level
    // follows directly from that.
    float2 d0 = du, d1 = dv;
    if (la::length2(d0 * base) < la::length2(d1 * base))
        std::swap(d0, d1);

    const float longer  = la::length(d0 * base);
    float       shorter = la::length(d1 * base);

    const float aniso = float(std::max(1, max_aniso));
    if (shorter > 0.f && shorter * aniso < longer)
    {
        const float scale = longer / (shorter * aniso);
        d1 *= scale;
        shorter *= scale;
    }

    if (shorter <= 0.f)
        return sample_bilinear(levels[0], uv);

    // Blended across the two levels either side rather than snapped to one: a snapped level is up to a
    // factor of two too sharp, which aliases in bands wherever the scale crosses a power of two.
    const float lod   = std::log2(shorter) + mip_bias;
    const int   lo    = std::clamp(int(std::floor(lod)), 0, int(levels.size()) - 1);
    const int   hi    = std::min(lo + 1, int(levels.size()) - 1);
    const float blend = std::clamp(lod - std::floor(lod), 0.f, 1.f);

    const float low = ewa_level(levels[size_t(lo)], uv, d0, d1);
    return lo == hi || blend <= 0.f ? low : (1.f - blend) * low + blend * ewa_level(levels[size_t(hi)], uv, d0, d1);
}

} // namespace

Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling, int supersample, float mip_bias, AtomicProgress progress)
{
    Array2Df out{size};

    // The same control means different things to the two samplers: how eccentric the ellipse may be before
    // it is widened for EWA, samples per axis within the pixel for point sampling.
    const int   ss  = std::max(1, supersample);
    const float inv = 1.f / float(ss * ss);

    // Only EWA needs the pyramid, and building it copies the source, so it is not built otherwise.
    const std::vector<Array2Df> levels =
        sampling == EnvMapSampling_EWA ? build_mip_pyramid(src) : std::vector<Array2Df>{};

    progress.set_num_steps(size.y);

    stp::parallel_for(
        stp::blocked_range<int>(0, size.y, 1),
        [&](int y0, int y1, int, int)
        {
            for (int y = y0; y < y1; ++y)
            {
                if (progress.canceled())
                    return;

                for (int x = 0; x < size.x; ++x)
                {
                    const float2 uv{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)};

                    // Parts of a disc or a cube cross are not sphere; leave them empty rather than filling
                    // them with whatever direction clamping produces.
                    if (!envmap_uv_is_valid(dst_mapping, uv))
                    {
                        out(x, y) = 0.f;
                        continue;
                    }

                    if (sampling == EnvMapSampling_EWA)
                    {
                        const float2 c = convert_envmap_uv(src_mapping, dst_mapping, uv);

                        // The footprint is the step to a *neighboring destination pixel*, taken in source
                        // coordinates, which composes both mappings in one step -- no derivative of either
                        // is needed. Both neighbors are tried and the nearer kept: these mappings are
                        // discontinuous -- a cube face's edge, a lat-long's wrap -- and a difference taken
                        // across one of those jumps is enormous and meaningless, which sends the level to
                        // the top of the pyramid and returns the average of the whole image. One side of a
                        // sample is always on the near side of any single seam.
                        auto step = [&](float2 delta)
                        {
                            const float2 fwd = convert_envmap_uv(src_mapping, dst_mapping, uv + delta) - c;
                            const float2 bwd = c - convert_envmap_uv(src_mapping, dst_mapping, uv - delta);
                            return la::length2(fwd) <= la::length2(bwd) ? fwd : bwd;
                        };

                        out(x, y) = sample_ewa(levels, c, step(float2{1.f / float(size.x), 0.f}),
                                               step(float2{0.f, 1.f / float(size.y)}), ss, mip_bias);
                        continue;
                    }

                    float sum = 0.f;
                    for (int sy = 0; sy < ss; ++sy)
                        for (int sx = 0; sx < ss; ++sx)
                        {
                            const float2 s_uv{(float(x) + (float(sx) + 0.5f) / float(ss)) / float(size.x),
                                              (float(y) + (float(sy) + 0.5f) / float(ss)) / float(size.y)};
                            sum += sample_bilinear(src, convert_envmap_uv(src_mapping, dst_mapping, s_uv));
                        }
                    out(x, y) = sum * inv;
                }

                ++progress;
            }
        });

    return out;
}

Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, AtomicProgress progress)
{
    // Ramamoorthi and Hanrahan: the cosine kernel falls off fast enough in frequency that nine spherical
    // harmonic coefficients reproduce the irradiance to about a percent. Projecting onto them and then
    // evaluating costs the two resolutions added rather than multiplied.
    //
    // Integrated over the image's own samples, each weighted by the solid angle it covers, so every
    // sample contributes exactly once and none is missed or read twice. That weight is what
    // envmap_jacobian() is: measuring it from how far the direction moves per sample instead is what went
    // wrong at the cube cross's seams.
    const int2   in_size = src.size();
    const double dA      = 1.0 / (double(in_size.x) * double(in_size.y));

    double sh[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    progress.set_num_steps(in_size.y + 1);

    for (int y = 0; y < in_size.y; ++y)
    {
        if (progress.canceled())
            return Array2Df{size};

        for (int x = 0; x < in_size.x; ++x)
        {
            const float2 uv{(float(x) + 0.5f) / float(in_size.x), (float(y) + 0.5f) / float(in_size.y)};
            const float  dw = envmap_jacobian(mapping, uv);
            if (dw <= 0.f)
                continue; // not sphere here, so there is nothing to gather

            const float3 d = envmap_uv_to_xyz(mapping, uv);
            const double L = double(src(x, y)) * double(dw) * dA;

            sh[0] += L * 0.282095;
            sh[1] += L * 0.488603 * double(d.y);
            sh[2] += L * 0.488603 * double(d.z);
            sh[3] += L * 0.488603 * double(d.x);
            sh[4] += L * 1.092548 * double(d.x) * double(d.y);
            sh[5] += L * 1.092548 * double(d.y) * double(d.z);
            sh[6] += L * 0.315392 * (3.0 * double(d.z) * double(d.z) - 1.0);
            sh[7] += L * 1.092548 * double(d.x) * double(d.z);
            sh[8] += L * 0.546274 * (double(d.x) * double(d.x) - double(d.y) * double(d.y));
        }

        ++progress;
    }

    // The constants that turn those coefficients into irradiance, from the same paper.
    constexpr double c1 = 0.429043, c2 = 0.511664, c3 = 0.743125, c4 = 0.886227, c5 = 0.247708;

    Array2Df out{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            const float2 uv{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)};
            const float3 n  = envmap_uv_to_xyz(mapping, uv);
            const double nx = n.x, ny = n.y, nz = n.z;

            const double e = c1 * sh[8] * (nx * nx - ny * ny) + c3 * sh[6] * nz * nz + c4 * sh[0] - c5 * sh[6] +
                             2.0 * c1 * (sh[4] * nx * ny + sh[7] * nx * nz + sh[5] * ny * nz) +
                             2.0 * c2 * (sh[3] * nx + sh[1] * ny + sh[2] * nz);

            // Divided by pi, so this is what a white lambertian surface facing this way reflects rather
            // than the irradiance arriving at it -- which is the number an environment lookup wants.
            out(x, y) = float(e / 3.14159265358979323846);
        }

    ++progress;
    return out;
}
