//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/envmap.h"

#include "common.h"

#include <smallthreadpool.h>

#include <algorithm>
#include <array>
#include <cmath>

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

//! Whether \p mapping's u covers a full turn, so that one edge of the image continues at the other.
/*!
    True of the two that lay longitude out along u. A disc has no such edge -- what lies outside it is not
    sphere at all -- and neither the equal-area square nor the cube cross joins up so simply.
*/
bool wraps_in_u(EnvMapping mapping) { return mapping == EnvMapping_LatLong || mapping == EnvMapping_Cylindrical; }

//! One texel of \p a, addressed the way \p mapping's own topology says.
/*!
    Reading past the edge of an image is reading somewhere else on the sphere, and where that is depends
    on the parameterization. Clamping is right only where the edge really is the end of the data.

    For the two that wrap: a step past the left edge arrives at the right, and a step past a pole carries
    on down the *far* side -- the row reflects and the longitude turns half way round, which is what the
    sphere does there. Clamping instead extends the pole row outward, which smears whatever sits at the
    top of the image along a band beneath it.
*/
float texel(const Array2Df &a, int x, int y, EnvMapping mapping)
{
    if (wraps_in_u(mapping))
    {
        if (y < 0)
        {
            y = -1 - y;
            x += a.width() / 2;
        }
        else if (y >= a.height())
        {
            y = 2 * a.height() - 1 - y;
            x += a.width() / 2;
        }

        return a(mod(x, a.width()), std::clamp(y, 0, a.height() - 1));
    }

    return a(std::clamp(x, 0, a.width() - 1), std::clamp(y, 0, a.height() - 1));
}

//! Bilinear read in [0,1]^2 image coordinates, reading past the edges as \p mapping says.
float sample_bilinear(const Array2Df &a, float2 uv, EnvMapping mapping)
{
    const float x = uv.x * float(a.width()) - 0.5f;
    const float y = uv.y * float(a.height()) - 0.5f;

    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float tx = x - float(x0), ty = y - float(y0);

    auto at = [&a, mapping](int i, int j) { return texel(a, i, j, mapping); };

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

float2 nearest_valid_envmap_uv(EnvMapping mapping, float2 uv)
{
    if (envmap_uv_is_valid(mapping, uv))
        return uv;

    switch (mapping)
    {
    case EnvMapping_Angular:
    case EnvMapping_MirrorBall:
    {
        // Straight out from the middle onto the circle, which is the nearest point of a disc to anything
        // outside it. Landing a hair inside keeps it on the sphere rather than exactly on the rim, where
        // the mapping is at its most singular.
        const float2 d = 2.f * uv - float2{1.f};
        const float  r = la::length(d);
        if (r <= 0.f)
            return uv;
        return 0.5f * (d * ((1.f - 1e-4f) / r) + float2{1.f});
    }

    case EnvMapping_CubeMap:
    {
        // The cross is two arms: the upright column of four, and the row of three across its second cell.
        // An empty corner is nearer one or the other, and snapping to it lands on that arm's edge.
        const float2 to_column{std::clamp(uv.x, 1.f / 3.f, 2.f / 3.f), uv.y};
        const float2 to_row{uv.x, std::clamp(uv.y, 0.25f, 0.5f)};

        return la::length2(to_column - uv) <= la::length2(to_row - uv) ? to_column : to_row;
    }

    default: return uv;
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

// -------------------------------------------------------------------------------------------------
// A cube map's six faces, each in its own coordinates
// -------------------------------------------------------------------------------------------------

/*!
    The six faces of the cube, in the order everything below indexes them.

    A vertical cross packs these into one image, but filtering wants them apart. Two texels side by side
    in the cross are neighboring directions only when they happen to share a face, four of its twelve
    cells stand for nothing at all, and a mip pyramid built over the whole image averages all of that
    together -- so by its third level a face is largely made of the empty cells beside it.
*/
enum CubeFace : int
{
    Face_PosX = 0,
    Face_NegX,
    Face_PosY,
    Face_NegY,
    Face_PosZ,
    Face_NegZ,

    Face_COUNT
};

/*!
    Direction for \p st on \p face, in that face's own [0,1]^2; not normalized.

    The same six faces xyz_to_cube_map() addresses, in each face's own coordinates rather than the
    cross's. Linear in \p st and deliberately not clamped: an \p st outside [0,1]^2 names a direction
    past that edge of the cube, which is how a face reaches into its neighbors without any of them
    having to say who they are.
*/
float3 cube_face_vector(int face, float2 st)
{
    const float a = 2.f * st.x - 1.f, b = 2.f * st.y - 1.f;
    switch (face)
    {
    case Face_PosX: return float3{1.f, -b, -a};
    case Face_NegX: return float3{-1.f, -b, a};
    case Face_PosY: return float3{a, 1.f, b};
    case Face_NegY: return float3{a, -1.f, -b};
    case Face_PosZ: return float3{a, -b, 1.f};
    default: return float3{a, b, -1.f};
    }
}

//! Which face \p d falls on: the axis it points most steeply along, ties going to x and then y.
int cube_face_of(float3 d)
{
    const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
    if (ax >= ay && ax >= az)
        return d.x >= 0.f ? Face_PosX : Face_NegX;
    if (ay >= az)
        return d.y >= 0.f ? Face_PosY : Face_NegY;
    return d.z >= 0.f ? Face_PosZ : Face_NegZ;
}

/*!
    Where \p d falls on \p face's plane, in that face's own [0,1]^2; the inverse of cube_face_vector().

    \p face need not be the face \p d is really on. One off to the side lands outside [0,1]^2 rather
    than being clamped into it, which is what lets a footprint reaching past an edge still be described
    in a single face's coordinates.
*/
float2 cube_face_st(int face, float3 d)
{
    float axis;
    switch (face)
    {
    case Face_PosX: axis = d.x; break;
    case Face_NegX: axis = -d.x; break;
    case Face_PosY: axis = d.y; break;
    case Face_NegY: axis = -d.y; break;
    case Face_PosZ: axis = d.z; break;
    default: axis = -d.z; break;
    }

    // A direction at or behind this face's horizon has no place on its plane at all. The smallest
    // positive divisor puts it far outside the face rather than at infinity, and whatever asked rejects
    // it for being that far away.
    const float3 t = d / std::max(axis, 1e-6f);

    float a, b;
    switch (face)
    {
    case Face_PosX:
        a = -t.z;
        b = -t.y;
        break;
    case Face_NegX:
        a = t.z;
        b = -t.y;
        break;
    case Face_PosY:
        a = t.x;
        b = t.z;
        break;
    case Face_NegY:
        a = t.x;
        b = -t.z;
        break;
    case Face_PosZ:
        a = t.x;
        b = -t.y;
        break;
    default:
        a = t.x;
        b = t.y;
        break;
    }

    return float2{0.5f * (a + 1.f), 0.5f * (b + 1.f)};
}

//! One mip level of a cube map: six faces, each ringed by a texel of whatever lies past its edges.
/*!
    A ring is needed because these faces are cell-centered, as a GPU's are: the outermost sample sits half
    a texel *inside* the cube's edge, so two faces meeting there share an edge but no sample along it, and
    interpolating out to it needs something from the other side. OpenEXR's cube maps take the opposite
    convention, putting samples exactly on the edges so that neighbors hold a row in common -- which needs
    no ring, at the cost of a grid that no longer halves cleanly into a pyramid.
*/
using CubeLevel = std::array<Array2Df, Face_COUNT>;

//! Texels along one side of a face, not counting its ring.
int face_size(const CubeLevel &level) { return level[0].width() - 2; }

//! Bilinear read of \p face at \p st in face coordinates, over its interior alone.
/*! What the ring is filled from, and so unable to look at it. */
float sample_face_interior(const Array2Df &face, float2 st)
{
    const int   n = face.width() - 2;
    const float x = st.x * float(n) + 0.5f, y = st.y * float(n) + 0.5f;

    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float tx = x - float(x0), ty = y - float(y0);

    auto at = [&face, n](int i, int j) { return face(std::clamp(i, 1, n), std::clamp(j, 1, n)); };

    return (1.f - ty) * ((1.f - tx) * at(x0, y0) + tx * at(x0 + 1, y0)) +
           ty * ((1.f - tx) * at(x0, y0 + 1) + tx * at(x0 + 1, y0 + 1));
}

//! Bilinear read of \p face at \p st in face coordinates, the ring standing in past its edges.
float sample_face(const Array2Df &face, float2 st)
{
    const int   n = face.width() - 2;
    const float x = st.x * float(n) + 0.5f, y = st.y * float(n) + 0.5f;

    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float tx = x - float(x0), ty = y - float(y0);

    auto at = [&face, n](int i, int j) { return face(std::clamp(i, 0, n + 1), std::clamp(j, 0, n + 1)); };

    return (1.f - ty) * ((1.f - tx) * at(x0, y0) + tx * at(x0 + 1, y0)) +
           ty * ((1.f - tx) * at(x0, y0 + 1) + tx * at(x0 + 1, y0 + 1));
}

//! Fill every face's ring from whichever face the direction just past that edge belongs to.
/*!
    A face's parameterization is linear in its own plane and defined beyond its edges, so extrapolating
    half a texel past one already names a direction on the next face over. Converting that direction
    back says which face that is and where on it -- so no table of who borders whom is needed, and the
    orientations fall out of the two parameterizations rather than being written down and gotten wrong.

    Read from the interiors alone, so the order the faces are filled in cannot matter.
*/
void fill_face_padding(CubeLevel &level)
{
    const int n = face_size(level);

    auto pad = [&level, n](int f, int i, int j)
    {
        const float2 st{(float(i) + 0.5f) / float(n), (float(j) + 0.5f) / float(n)};
        const float3 d = cube_face_vector(f, st);
        const int    g = cube_face_of(d);

        level[size_t(f)](i + 1, j + 1) = sample_face_interior(level[size_t(g)], cube_face_st(g, d));
    };

    for (int f = 0; f < Face_COUNT; ++f)
    {
        for (int i = -1; i <= n; ++i)
        {
            pad(f, i, -1);
            pad(f, i, n);
        }
        for (int j = 0; j < n; ++j)
        {
            pad(f, -1, j);
            pad(f, n, j);
        }
    }
}

//! The six faces of the vertical cross in \p src, \p n texels a side.
CubeLevel faces_from_cross(const Array2Df &src, int n)
{
    CubeLevel level;
    for (int f = 0; f < Face_COUNT; ++f)
    {
        level[size_t(f)] = Array2Df{int2{n + 2, n + 2}};

        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                const float2 st{(float(i) + 0.5f) / float(n), (float(j) + 0.5f) / float(n)};

                // Exactly one source texel whenever the cross is 3n by 4n, since the two grids then have
                // the same centers; a cross of some other shape is resampled onto this one instead.
                level[size_t(f)](i + 1, j + 1) =
                    sample_bilinear(src, xyz_to_cube_map(cube_face_vector(f, st)), EnvMapping_CubeMap);
            }
    }

    fill_face_padding(level);
    return level;
}

//! \p prev at half its resolution, averaged 2x2 and ringed again.
/*!
    Built from the interiors alone and padded afresh rather than decimating the ring along with them: a
    coarser level's reads want a ring of what its *own* neighbors hold.
*/
CubeLevel decimated(const CubeLevel &prev)
{
    const int pn = face_size(prev);
    const int n  = std::max(1, pn / 2);

    CubeLevel next;
    for (int f = 0; f < Face_COUNT; ++f)
    {
        next[size_t(f)] = Array2Df{int2{n + 2, n + 2}};

        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                // The guards matter on an odd face size, where the last row or column has no partner.
                const int i0 = std::min(2 * i, pn - 1) + 1, i1 = std::min(2 * i + 1, pn - 1) + 1;
                const int j0 = std::min(2 * j, pn - 1) + 1, j1 = std::min(2 * j + 1, pn - 1) + 1;

                next[size_t(f)](i + 1, j + 1) = 0.25f * (prev[size_t(f)](i0, j0) + prev[size_t(f)](i1, j0) +
                                                         prev[size_t(f)](i0, j1) + prev[size_t(f)](i1, j1));
            }
    }

    fill_face_padding(next);
    return next;
}

/*!
    Gather through the ellipse with axes \p d0 and \p d1, centered at \p uv, over a grid of \p res texels.

    Every texel the ellipse encloses contributes, weighted by a Gaussian in the ellipse's own space, so
    the filter takes the whole of the footprint's shape. The quadratic \f$A s^2 + B s t + C t^2\f$ is one
    exactly on the ellipse's boundary, which makes the test for "inside" a comparison against one and the
    weight a function of that same number.

    The `+ 1` on \f$A\f$ and \f$C\f$ is the part that is easy to leave out and expensive to: it convolves
    the ellipse with a reconstruction filter one texel across, so a footprint smaller than a texel widens
    into one instead of collapsing onto a point sample. Without it, magnification aliases.

    \p tap supplies one texel by integer coordinate, and is where the two source layouts differ: a whole
    image reads through its mapping's wrap rules, a cube face through its own ring and, past that, by
    direction. False when the ellipse was narrow enough to enclose no texel at all, which only a bilinear
    read can answer.

    Follows PBRT's `MIPMap::EWA`.
*/
template <typename Tap>
bool ewa_gather(float2 uv, float2 d0, float2 d1, float2 res, Tap &&tap, float &result)
{
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

            sum += w * tap(s, t);
            total += w;
        }
    }

    if (total <= 0.f)
        return false;

    result = sum / total;
    return true;
}

//! Which levels an elliptical footprint reads, and the ellipse it reads them through.
struct EWAFootprint
{
    float2 d0, d1;     //!< Ellipse axes, longer first, after the anisotropy clamp
    int    lo, hi;     //!< The levels either side of the one the footprint asks for
    float  blend;      //!< How much of hi
    bool   degenerate; //!< A footprint of no extent, which only a bilinear read can answer
};

/*!
    Pick the levels for a footprint \p du by \p dv over a base level of \p base texels.

    The level comes from the ellipse's *shorter* axis, so a texel of that level spans the narrow
    direction and the gather itself covers the long one. That split is what a mip level alone cannot do:
    a lat-long's pole is stretched hundreds of times more across than down, and a level chosen to cover
    the wide direction erases the narrow one.

    Walking a long axis costs a texel per step, so \p max_aniso is where that stops. Past it the
    *shorter* axis is lengthened until the ratio is affordable -- widening the ellipse rather than
    raising the level, since the level would blur both directions when only one of them cannot be
    afforded.

    Follows PBRT's `MIPMap::Filter`.
*/
EWAFootprint choose_ewa_levels(float2 du, float2 dv, float2 base, int num_levels, int max_aniso, float mip_bias)
{
    EWAFootprint fp;

    // Longer axis first, measured in base-level texels: du and dv are the step from one destination pixel
    // to the next, so their lengths are how far across the source a destination pixel reaches, and the
    // level follows directly from that.
    fp.d0 = du;
    fp.d1 = dv;
    if (la::length2(fp.d0 * base) < la::length2(fp.d1 * base))
        std::swap(fp.d0, fp.d1);

    const float longer  = la::length(fp.d0 * base);
    float       shorter = la::length(fp.d1 * base);

    const float aniso = float(std::max(1, max_aniso));
    if (shorter > 0.f && shorter * aniso < longer)
    {
        const float scale = longer / (shorter * aniso);
        fp.d1 *= scale;
        shorter *= scale;
    }

    fp.degenerate = shorter <= 0.f;
    if (fp.degenerate)
    {
        fp.lo = fp.hi = 0;
        fp.blend      = 0.f;
        return fp;
    }

    // Blended across the two levels either side rather than snapped to one: a snapped level is up to a
    // factor of two too sharp, which aliases in bands wherever the scale crosses a power of two.
    // Clamped before the level is taken from it, not after. Clamping the level while taking the fraction
    // from the unclamped one puts a whole level's worth of blend on a sample that is being magnified: at a
    // lod of -0.01 the fraction is 0.99, and at +0.01 it is 0.01, so the two sides of the boundary between
    // magnifying and minifying come out from opposite ends of the pyramid. In a remap that boundary is a
    // curve across the image, and it showed up as a seam along it.
    const float lod = std::clamp(std::log2(shorter) + mip_bias, 0.f, float(num_levels - 1));
    fp.lo           = int(std::floor(lod));
    fp.hi           = std::min(fp.lo + 1, num_levels - 1);
    fp.blend        = lod - float(fp.lo);
    return fp;
}

/*!
    A source image prepared to be read as a sphere: by direction, rather than by image coordinate.

    Which matters because a cube map cannot be read the way the others are. Its cross is six charts in
    one image: two texels either side of a face join are not neighboring directions, four of its cells
    stand for no direction at all, and a pyramid built over the whole picture averages both of those into
    the faces. So a cube map is taken apart into its six faces, each with its own pyramid and its own
    ring of what lies past its edges, while every other mapping keeps the single image it already is and
    the wrap rules texel() applies to it.
*/
class EnvSource
{
public:
    /// \p with_mips builds the levels EWA needs; point sampling reads only the finest.
    EnvSource(const Array2Df &src, EnvMapping mapping, bool with_mips) : m_src(src), m_mapping(mapping)
    {
        if (mapping == EnvMapping_CubeMap)
        {
            m_faces.push_back(faces_from_cross(src, std::max(1, std::min(src.width() / 3, src.height() / 4))));
            while (with_mips && face_size(m_faces.back()) > 1) m_faces.push_back(decimated(m_faces.back()));
        }
        else if (with_mips)
            m_levels = build_mip_pyramid(src);
    }

    //! Bilinear at the finest level, in the direction \p d.
    float point(float3 d) const
    {
        if (!m_faces.empty())
            return sample_face_at(0, d);

        return sample_bilinear(level(0), envmap_xyz_to_uv(m_mapping, d), m_mapping);
    }

    /*!
        Elliptically filtered over the footprint one destination pixel covers.

        \p uv is that pixel's center in \p dst's parameterization, and \p delta_u and \p delta_v the step
        from it to the next pixel along each axis.
    */
    float ewa(EnvMapping dst, float2 uv, float2 delta_u, float2 delta_v, int max_aniso, float mip_bias) const
    {
        const float3 d = envmap_uv_to_xyz(dst, uv);

        if (!m_faces.empty())
            return ewa_on_face(dst, uv, d, delta_u, delta_v, max_aniso, mip_bias);

        // The footprint is the step to a *neighboring destination pixel*, taken in source coordinates,
        // which composes both mappings in one step -- no derivative of either is needed. Both neighbors
        // are tried and the nearer kept: these mappings are discontinuous -- a lat-long's wrap, a disc's
        // rim -- and a difference taken across one of those jumps is enormous and meaningless, which
        // sends the level to the top of the pyramid and returns the average of the whole image. One side
        // of a sample is always on the near side of any single seam.
        const float2 c    = envmap_xyz_to_uv(m_mapping, d);
        auto         step = [&](float2 delta)
        {
            const float2 fwd = convert_envmap_uv(m_mapping, dst, uv + delta) - c;
            const float2 bwd = c - convert_envmap_uv(m_mapping, dst, uv - delta);
            return la::length2(fwd) <= la::length2(bwd) ? fwd : bwd;
        };

        const float2       base{float(level(0).width()), float(level(0).height())};
        const EWAFootprint fp =
            choose_ewa_levels(step(delta_u), step(delta_v), base, num_levels(), max_aniso, mip_bias);

        if (fp.degenerate)
            return sample_bilinear(level(0), c, m_mapping);

        const float low = ewa_level(fp.lo, c, fp.d0, fp.d1);
        return fp.lo == fp.hi || fp.blend <= 0.f
                   ? low
                   : (1.f - fp.blend) * low + fp.blend * ewa_level(fp.hi, c, fp.d0, fp.d1);
    }

private:
    int num_levels() const { return std::max(1, int(m_levels.size())); }

    //! Level \p i of the whole-image pyramid; the source itself when there is none, since it is level 0
    //! and a point-sampling instance would otherwise hold a second copy of it for nothing.
    const Array2Df &level(int i) const { return m_levels.empty() ? m_src : m_levels[size_t(i)]; }

    float sample_face_at(int lvl, float3 d) const
    {
        const int f = cube_face_of(d);
        return sample_face(m_faces[size_t(lvl)][size_t(f)], cube_face_st(f, d));
    }

    float ewa_level(int lvl, float2 uv, float2 d0, float2 d1) const
    {
        const Array2Df &a   = level(lvl);
        auto            tap = [&a, this](int i, int j) { return texel(a, i, j, m_mapping); };

        float value;
        if (ewa_gather(uv, d0, d1, float2{float(a.width()), float(a.height())}, tap, value))
            return value;

        // An ellipse narrow enough to fall between texels encloses none of them; read the level instead.
        return sample_bilinear(a, uv, m_mapping);
    }

    float ewa_face(int lvl, int face, float2 st, float2 d0, float2 d1) const
    {
        const Array2Df &f = m_faces[size_t(lvl)][size_t(face)];
        const int       n = f.width() - 2;

        // A tap is a direction. The face's coordinates run on past its edges, so one landing outside
        // still says where it is in space, and converting it finds which face that turns out to be --
        // the ellipse living on the sphere rather than in any one image.
        //
        // The ring answers that same question for the first texel out, which is as far as anything but
        // the widest ellipse reaches; and since the Gaussian is pinned to zero at the boundary, what
        // lies beyond it is weighted at almost nothing. So the ring carries this in practice and the
        // conversion is what keeps the rule true where it does not.
        auto tap = [&f, n, face, lvl, this](int s, int t)
        {
            if (s >= -1 && s <= n && t >= -1 && t <= n)
                return f(s + 1, t + 1);

            const float2 out{(float(s) + 0.5f) / float(n), (float(t) + 0.5f) / float(n)};
            return sample_face_at(lvl, cube_face_vector(face, out));
        };

        float value;
        if (ewa_gather(st, d0, d1, float2{float(n)}, tap, value))
            return value;

        return sample_face(f, st);
    }

    float ewa_on_face(EnvMapping dst, float2 uv, float3 d, float2 delta_u, float2 delta_v, int max_aniso,
                      float mip_bias) const
    {
        const int    face = cube_face_of(d);
        const float2 st   = cube_face_st(face, d);

        // The footprint in this face's own coordinates rather than the cross's, where a step across a
        // face join is a jump to somewhere else in the image and says nothing about how far the direction
        // moved. Both neighbors are tried and the nearer kept, as above: a destination mapping has seams
        // of its own, and a neighbor past this face's horizon lands arbitrarily far outside it.
        auto step = [&](float2 delta)
        {
            const float2 fwd = cube_face_st(face, envmap_uv_to_xyz(dst, uv + delta)) - st;
            const float2 bwd = st - cube_face_st(face, envmap_uv_to_xyz(dst, uv - delta));
            return la::length2(fwd) <= la::length2(bwd) ? fwd : bwd;
        };

        const float2       base{float(face_size(m_faces[0]))};
        const EWAFootprint fp =
            choose_ewa_levels(step(delta_u), step(delta_v), base, int(m_faces.size()), max_aniso, mip_bias);

        if (fp.degenerate)
            return sample_face_at(0, d);

        const float low = ewa_face(fp.lo, face, st, fp.d0, fp.d1);
        return fp.lo == fp.hi || fp.blend <= 0.f
                   ? low
                   : (1.f - fp.blend) * low + fp.blend * ewa_face(fp.hi, face, st, fp.d0, fp.d1);
    }

    const Array2Df        &m_src;
    EnvMapping             m_mapping;
    std::vector<Array2Df>  m_levels; //!< The whole image, for every mapping but a cube map
    std::vector<CubeLevel> m_faces;  //!< Six ringed faces per level, for a cube map
};
} // namespace

Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling, int supersample, float mip_bias, AtomicProgress progress)
{
    Array2Df out{size};

    // The same control means different things to the two samplers: how eccentric the ellipse may be before
    // it is widened for EWA, samples per axis within the pixel for point sampling.
    const int   ss  = std::max(1, supersample);
    const float inv = 1.f / float(ss * ss);

    // Prepared once for the whole remap: the levels EWA reads, and, for a cube map, the six faces both
    // samplers read instead of the cross.
    const EnvSource source{src, src_mapping, sampling == EnvMapSampling_EWA};

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
                    // Parts of a disc or a cube cross are not sphere. Rather than leaving them empty, each
                    // is given the nearest direction that is -- so the values just outside the sphere
                    // agree with those just inside, and a later bilinear read near that edge finds what it
                    // expects instead of pulling the emptiness inward.
                    const float2 uv = nearest_valid_envmap_uv(
                        dst_mapping, float2{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)});

                    if (sampling == EnvMapSampling_EWA)
                    {
                        out(x, y) = source.ewa(dst_mapping, uv, float2{1.f / float(size.x), 0.f},
                                               float2{0.f, 1.f / float(size.y)}, ss, mip_bias);
                        continue;
                    }

                    float sum = 0.f;
                    for (int sy = 0; sy < ss; ++sy)
                        for (int sx = 0; sx < ss; ++sx)
                        {
                            // Snapped like the pixel's own center: a sub-sample may land outside the
                            // sphere even where the center did not, and one that did would be reading
                            // from whatever direction a coordinate with no direction produces.
                            const float2 s_uv = nearest_valid_envmap_uv(
                                dst_mapping, float2{(float(x) + (float(sx) + 0.5f) / float(ss)) / float(size.x),
                                                    (float(y) + (float(sy) + 0.5f) / float(ss)) / float(size.y)});

                            sum += source.point(envmap_uv_to_xyz(dst_mapping, s_uv));
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
