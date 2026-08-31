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
    // Image plane coordinates over [-1,1], centered on the image.
    const float2 xy = 2.f * uv - float2{1.f};

    // The polar angle grows linearly with the distance from the center, which is what makes this mapping
    // cover the whole sphere in one disc.
    const float phi   = std::clamp(la::length(xy) * k_pi, 0.f, k_pi);
    const float theta = std::atan2(xy.y, xy.x);

    const float sin_phi = std::sin(phi);
    return float3{sin_phi * std::cos(theta), -sin_phi * std::sin(theta), std::cos(phi)};
}

float3 mirror_ball_to_xyz(float2 uv)
{
    const float2 xy = 2.f * uv - float2{1.f};

    // Here it is the *sine* of half the polar angle that grows linearly with the radius, which is what a
    // photograph of a mirrored sphere records.
    const float phi   = 2.f * std::asin(std::clamp(la::length(xy), 0.f, 1.f));
    const float theta = std::atan2(xy.y, xy.x);

    const float sin_phi = std::sin(phi);
    return float3{sin_phi * std::cos(theta), -sin_phi * std::sin(theta), std::cos(phi)};
}

float3 lat_long_to_xyz(float2 uv)
{
    const float theta = lerp(1.5f * k_pi, -k_half_pi, uv.x);
    const float phi   = uv.y * k_pi;

    const float sin_phi = std::sin(phi);
    return float3{sin_phi * std::cos(theta), std::cos(phi), sin_phi * std::sin(theta)};
}

float3 cylindrical_to_xyz(float2 uv)
{
    // Longitude across as before, but height rather than latitude down -- so every row covers the same
    // solid angle, which lat-long does not.
    const float theta   = lerp(1.5f * k_pi, -k_half_pi, uv.x);
    const float cos_phi = lerp(1.f, -1.f, uv.y);

    const float sin_phi = std::sqrt(std::max(0.f, 1.f - cos_phi * cos_phi));
    return float3{sin_phi * std::cos(theta), cos_phi, sin_phi * std::sin(theta)};
}

//! The cube face vector for \p uv before normalizing: one component is +-1 and the other two are the
//! face coordinates. Its length is what the Jacobian needs, so the two share this.
float3 cube_map_face_vector(float2 uv)
{
    // The six faces laid out as a vertical cross: the upright column of four down the middle third, and
    // the two side faces either side of it.
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
    // Which face: the axis the direction is largest along, and its sign.
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

Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         int supersample, FilterProgress progress)
{
    Array2Df    out{size};
    const int   ss  = std::max(1, supersample);
    const float inv = 1.f / float(ss * ss);

    progress.set_num_steps(size.y);

    stp::parallel_for(stp::blocked_range<int>(0, size.y, 1),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                          {
                              if (progress.canceled())
                                  return;

                              for (int x = 0; x < size.x; ++x)
                              {
                                  // Parts of a disc or a cube cross are not sphere; leave them empty
                                  // rather than filling them with whatever direction clamping produces.
                                  if (!envmap_uv_is_valid(dst_mapping, float2{(float(x) + 0.5f) / float(size.x),
                                                                              (float(y) + 0.5f) / float(size.y)}))
                                  {
                                      out(x, y) = 0.f;
                                      continue;
                                  }

                                  float sum = 0.f;
                                  for (int sy = 0; sy < ss; ++sy)
                                      for (int sx = 0; sx < ss; ++sx)
                                      {
                                          const float2 uv{(float(x) + (float(sx) + 0.5f) / float(ss)) / float(size.x),
                                                          (float(y) + (float(sy) + 0.5f) / float(ss)) / float(size.y)};
                                          sum += sample_bilinear(src, convert_envmap_uv(src_mapping, dst_mapping, uv));
                                      }
                                  out(x, y) = sum * inv;
                              }

                              ++progress;
                          }
                      });

    return out;
}

Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, FilterProgress progress)
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
