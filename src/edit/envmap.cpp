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

// -------------------------------------------------------------------------------------------------
// The six faces of the cube, in the one orientation convention both cube layouts arrange
// -------------------------------------------------------------------------------------------------

/// The six faces of the cube, in the order everything below indexes them.
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

/// Direction for \p st on \p face, in that face's own [0,1]^2; not normalized.
/**
    Not clamped: an \p st outside [0,1]^2 names a direction past that edge of the cube, which is how a face
    reaches its neighbors.
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

/// Which face \p d falls on: the axis it points most steeply along, ties going to x and then y.
int cube_face_of(float3 d)
{
    const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
    if (ax >= ay && ax >= az)
        return d.x >= 0.f ? Face_PosX : Face_NegX;
    if (ay >= az)
        return d.y >= 0.f ? Face_PosY : Face_NegY;
    return d.z >= 0.f ? Face_PosZ : Face_NegZ;
}

/// Where \p d falls on \p face's plane, in that face's own [0,1]^2; the inverse of cube_face_vector().
/**
    \p face need not be the one \p d is really on: a direction off to the side lands outside [0,1]^2, so a
    footprint reaching past an edge is still described in one face's coordinates.
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

    // a direction at or behind this face's horizon has no place on its plane; the floor on the divisor
    // puts it far outside the face instead of at infinity
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

/// Cell of the cross's 3-by-4 grid holding each face, as (column, row).
constexpr int2 k_cross_cell[Face_COUNT] = {int2{2, 1}, int2{0, 1}, int2{1, 0}, int2{1, 2}, int2{1, 1}, int2{1, 3}};

/// The face in the cross's (\p col, \p row) cell; the inverse of k_cross_cell.
int cross_face(int col, int row)
{
    switch (col)
    {
    case 0: return Face_NegX;
    case 2: return Face_PosX;
    default: return row == 0 ? Face_PosY : row == 1 ? Face_PosZ : row == 2 ? Face_NegY : Face_NegZ;
    }
}

/// Which of a face's two axes the single-column layout turns to match OpenEXR; the other it keeps.
constexpr bool k_column_flips_s[Face_COUNT] = {true, true, false, false, true, false};

/// \p st turned as the column layout turns \p face, which being a single flip is its own inverse.
float2 column_st(int face, float2 st)
{
    return k_column_flips_s[face] ? float2{1.f - st.x, st.y} : float2{st.x, 1.f - st.y};
}

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

    // sin(phi) varies linearly with the radius from center
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
    // and y=cosPhi varies linearly with V, so every row covers the same solid angle
    const float theta   = lerp(1.5f * k_pi, -k_half_pi, uv.x);
    const float cos_phi = lerp(1.f, -1.f, uv.y);

    const float sin_phi = std::sqrt(std::max(0.f, 1.f - cos_phi * cos_phi));
    return float3{sin_phi * std::cos(theta), cos_phi, sin_phi * std::sin(theta)};
}

/// The cube face vector for \p uv before normalizing: one component is +-1, the other two face coordinates.
/**
    Its length is what the Jacobian needs.
*/
float3 cube_map_face_vector(float2 uv)
{
    // This is assuming that the Cubemap is a vertical cross: the upright column of four down the middle
    // third, and the two side faces either side of it
    const bool middle = uv.x >= 1.f / 3.f && uv.x <= 2.f / 3.f;
    const int  col    = middle ? 1 : (uv.x < 1.f / 3.f ? 0 : 2);
    const int  row    = middle ? (uv.y <= 0.25f ? 0 : uv.y <= 0.5f ? 1 : uv.y <= 0.75f ? 2 : 3) : 1;

    // a uv in one of the four corner cells stands for no direction; clamping it into the side face beside
    // it gives that face's edge
    const float u = middle ? uv.x : std::clamp(uv.x, float(col) / 3.f, float(col + 1) / 3.f);
    const float v = middle ? uv.y : std::clamp(uv.y, 0.25f, 0.5f);

    return cube_face_vector(cross_face(col, row), float2{u * 3.f - float(col), v * 4.f - float(row)});
}

float3 cube_map_to_xyz(float2 uv) { return la::normalize(cube_map_face_vector(uv)); }

/**
    The cube face vector for \p uv in the single-column layout, before normalizing.

    Six faces stacked top to bottom in the CubeFace order, each turned the way OpenEXR turns it. OpenEXR
    puts its outermost samples exactly on the face edges, where this puts them half a texel inside as a GPU
    does; see CubeLevel. Same layout, sample grid offset by half a texel.
*/
float3 cube_column_face_vector(float2 uv)
{
    const int face = std::clamp(int(uv.y * 6.f), 0, 5);
    return cube_face_vector(face, column_st(face, float2{uv.x, uv.y * 6.f - float(face)}));
}

float3 cube_column_to_xyz(float2 uv) { return la::normalize(cube_column_face_vector(uv)); }

/// Where \p xyz falls in the single-column layout; the inverse of cube_column_face_vector().
float2 xyz_to_cube_column(float3 xyz)
{
    const int    face = cube_face_of(xyz);
    const float2 st   = column_st(face, cube_face_st(face, xyz));
    return float2{st.x, (float(face) + st.y) / 6.f};
}

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
    // Again, the CubeMap is a vertical cross: the face tells us which side we're looking at, and its cell
    // where that side sits in the 3-by-4 grid.
    const int    face = cube_face_of(xyz);
    const float2 st   = cube_face_st(face, xyz);
    const int2   cell = k_cross_cell[face];

    return float2{(float(cell.x) + st.x) / 3.f, (float(cell.y) + st.y) / 4.f};
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
    case EnvMapping_CubeMapColumn: return "Cube map (vertical column)";
    case EnvMapping_EqualArea: return "Equal area";
    default: return "";
    }
}

float envmapping_aspect(int mapping)
{
    switch (mapping)
    {
    case EnvMapping_LatLong:
    case EnvMapping_Cylindrical: return 2.f;         // a full turn across, half a turn down
    case EnvMapping_CubeMap: return 3.f / 4.f;       // three faces across by four down
    case EnvMapping_CubeMapColumn: return 1.f / 6.f; // one across by six down
    default: return 1.f;                             // the discs and the equal-area square
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
    case EnvMapping_CubeMapColumn: return cube_column_to_xyz(uv);
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
    case EnvMapping_CubeMapColumn: return xyz_to_cube_column(xyz);
    default: return xyz_to_equal_area(xyz);
    }
}

namespace
{

/// Whether \p mapping's u covers a full turn, so that one edge of the image continues at the other.
bool wraps_in_u(EnvMapping mapping) { return mapping == EnvMapping_LatLong || mapping == EnvMapping_Cylindrical; }

/// Texel of \p a with out-of-range coordinates wrapped as the sphere does.
/**
    Lat-long and cylindrical wrap in u; past a pole the row reflects and u shifts by half a turn. Every
    other mapping clamps.
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

/// Blend of the four texels around continuous coordinate (\p x, \p y), each supplied by \p at.
template <typename At>
float bilinear(float x, float y, At &&at)
{
    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float tx = x - float(x0), ty = y - float(y0);

    return (1.f - ty) * ((1.f - tx) * at(x0, y0) + tx * at(x0 + 1, y0)) +
           ty * ((1.f - tx) * at(x0, y0 + 1) + tx * at(x0 + 1, y0 + 1));
}

/// Bilinear read in [0,1]^2 image coordinates, reading past the edges as \p mapping says.
float sample_bilinear(const Array2Df &a, float2 uv, EnvMapping mapping)
{
    return bilinear(uv.x * float(a.width()) - 0.5f, uv.y * float(a.height()) - 0.5f,
                    [&a, mapping](int i, int j) { return texel(a, i, j, mapping); });
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
        // Archimedes: v is the height and not the angle, so every row covers the same solid angle.
        return k_four_pi;

    case EnvMapping_EqualArea:
        // equal-area by construction
        return k_four_pi;

    case EnvMapping_MirrorBall:
        // Also equal-area, which is less obvious: the sine of half the polar angle growing linearly with
        // the radius is exactly the condition for it.
        return 16.f;

    case EnvMapping_Angular:
    {
        // Here the polar angle itself grows linearly with the radius, so the sky compresses towards the rim.
        const float r = la::length(2.f * uv - float2{1.f});
        // sin(pi*r)/r tends to pi at the center, where the ratio itself would divide by zero.
        return r < 1e-6f ? k_four_pi * k_pi : k_four_pi * std::sin(k_pi * r) / r;
    }

    case EnvMapping_CubeMapColumn:
    {
        // As below, but the column gives each face a sixth of the image where the cross gives a twelfth.
        const float3 v = cube_column_face_vector(uv);
        const float  l = la::length(v);
        return 24.f / (l * l * l);
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
        // straight out onto the circle, landing a hair inside it, where the mapping is less singular
        const float2 d = 2.f * uv - float2{1.f};
        const float  r = la::length(d);
        if (r <= 0.f)
            return uv;
        return 0.5f * (d * ((1.f - 1e-4f) / r) + float2{1.f});
    }

    case EnvMapping_CubeMap:
    {
        // the cross is two arms: the column of four, and the row of three across its second cell
        const float2 to_column{std::clamp(uv.x, 1.f / 3.f, 2.f / 3.f), uv.y};
        const float2 to_row{uv.x, std::clamp(uv.y, 0.25f, 0.5f)};

        return la::length2(to_column - uv) <= la::length2(to_row - uv) ? to_column : to_row;
    }

    default: return uv;
    }
}

namespace
{

/// A mip pyramid, each level half the size of the one before and averaged from it.
/**
    Level 0 is a copy of the source, so every level is addressed the same way.
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
                // the guards matter on an odd dimension, where the last row or column has no partner
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

/// One mip level of a cube map: six faces, each ringed by a texel of whatever lies past its edges.
/**
    These faces are cell-centered, as a GPU's are, so the outermost sample sits half a texel inside the
    cube's edge and two faces meeting there share no sample; the ring is what interpolating out to the edge
    reads. (OpenEXR instead puts samples on the edges, which needs no ring but does not halve cleanly.)
*/
using CubeLevel = std::array<Array2Df, Face_COUNT>;

/// Texels along one side of a face, not counting its ring.
int face_size(const CubeLevel &level) { return level[0].width() - 2; }

/// Whether \p mapping lays the sphere out as six flat faces, whichever arrangement it uses for them.
bool is_cube(EnvMapping mapping) { return mapping == EnvMapping_CubeMap || mapping == EnvMapping_CubeMapColumn; }

/// Side of the finest face \p mapping can carve out of an image of \p size.
int face_size_for(EnvMapping mapping, int2 size)
{
    return mapping == EnvMapping_CubeMapColumn ? std::max(1, std::min(size.x, size.y / 6))
                                               : std::max(1, std::min(size.x / 3, size.y / 4));
}

/// Bilinear read of \p face at \p st in face coordinates, the ring standing in past its edges.
/**
    An \p interior read stops at the edge of the face proper, which is what filling the ring must do.
*/
float sample_face(const Array2Df &face, float2 st, bool interior = false)
{
    const int n  = face.width() - 2;
    const int lo = interior ? 1 : 0, hi = interior ? n : n + 1;

    return bilinear(st.x * float(n) + 0.5f, st.y * float(n) + 0.5f,
                    [&face, lo, hi](int i, int j) { return face(std::clamp(i, lo, hi), std::clamp(j, lo, hi)); });
}

/// Fill every face's ring from whichever face the direction just past that edge belongs to.
/**
    No table of who borders whom is needed, and reading from the interiors alone makes the fill order
    irrelevant.
*/
void fill_face_padding(CubeLevel &level)
{
    const int n = face_size(level);

    auto pad = [&level, n](int f, int i, int j)
    {
        const float2 st{(float(i) + 0.5f) / float(n), (float(j) + 0.5f) / float(n)};
        const float3 d = cube_face_vector(f, st);
        const int    g = cube_face_of(d);

        level[size_t(f)](i + 1, j + 1) = sample_face(level[size_t(g)], cube_face_st(g, d), true);
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

/// The six faces of the cube map in \p src, \p n texels a side.
CubeLevel faces_from_source(const Array2Df &src, int n, EnvMapping mapping)
{
    CubeLevel level;
    for (int f = 0; f < Face_COUNT; ++f)
    {
        level[size_t(f)] = Array2Df{int2{n + 2, n + 2}};

        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                const float2 st{(float(i) + 0.5f) / float(n), (float(j) + 0.5f) / float(n)};

                // one source texel whenever the faces divide the image evenly, since the two grids then
                // share centers; any other shape is resampled onto this one
                level[size_t(f)](i + 1, j + 1) =
                    sample_bilinear(src, envmap_xyz_to_uv(mapping, cube_face_vector(f, st)), mapping);
            }
    }

    fill_face_padding(level);
    return level;
}

/// \p prev at half its resolution, averaged 2x2 over the interiors alone and ringed afresh.
/**
    The ring is rebuilt, not decimated: a coarser level's reads want a ring of what its own neighbors hold
    at that level.
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
                // the guards matter on an odd face size, where the last row or column has no partner
                const int i0 = std::min(2 * i, pn - 1) + 1, i1 = std::min(2 * i + 1, pn - 1) + 1;
                const int j0 = std::min(2 * j, pn - 1) + 1, j1 = std::min(2 * j + 1, pn - 1) + 1;

                next[size_t(f)](i + 1, j + 1) = 0.25f * (prev[size_t(f)](i0, j0) + prev[size_t(f)](i1, j0) +
                                                         prev[size_t(f)](i0, j1) + prev[size_t(f)](i1, j1));
            }
    }

    fill_face_padding(next);
    return next;
}

/**
    Gather through the ellipse with axes \p d0 and \p d1, centered at \p uv, over a grid of \p res texels.

    Every texel the ellipse encloses contributes, weighted by a Gaussian in the ellipse's own space. The
    quadratic \f$A s^2 + B s t + C t^2\f$ is one on the ellipse's boundary, so "inside" is a comparison
    against one and the weight is a function of the same number. The `+ 1` on \f$A\f$ and \f$C\f$
    convolves the ellipse with a one-texel reconstruction filter, without which magnification aliases.

    \p tap supplies one texel by integer coordinate: a whole image reads through its mapping's wrap rules,
    a cube face through its own ring and, past that, by direction. False when the ellipse enclosed no texel
    at all, which only a bilinear read can answer.

    Follows PBRT's `MIPMap::EWA`.
*/
template <typename Tap>
bool ewa_gather(float2 uv, float2 d0, float2 d1, float2 res, Tap &&tap, float &result)
{
    // continuous texel coordinates, with the axes in the same units
    const float2 st = uv * res - 0.5f;
    const float2 a = d0 * res, b = d1 * res;

    float A = sqr(a.y) + sqr(b.y) + 1.f;
    float B = -2.f * (a.x * a.y + b.x * b.y);
    float C = sqr(a.x) + sqr(b.x) + 1.f;

    const float inv_f = 1.f / std::max(1e-20f, A * C - 0.25f * sqr(B));
    A *= inv_f;
    B *= inv_f;
    C *= inv_f;

    // the axis-aligned box that just contains the ellipse, which is where the scan runs
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

            // subtracting the boundary value lands the Gaussian on zero there, so no ring appears at
            // the ellipse's edge
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

/// Which levels an elliptical footprint reads, and the ellipse it reads them through.
struct EWAFootprint
{
    float2 d0, d1;     ///< Ellipse axes, longer first, after the anisotropy clamp
    int    lo, hi;     ///< The levels either side of the one the footprint asks for
    float  blend;      ///< How much of hi
    bool   degenerate; ///< A footprint of no extent, which only a bilinear read can answer
};

/**
    Pick the levels for a footprint \p du by \p dv over a base level of \p base texels.

    The level comes from the ellipse's shorter axis, so a texel of that level spans the narrow direction
    and the gather covers the long one; a level chosen for the wide direction would erase the narrow one.
    Walking a long axis costs a texel per step, so past \p max_aniso the shorter axis is lengthened until
    the ratio is affordable.

    Follows PBRT's `MIPMap::Filter`.
*/
EWAFootprint choose_ewa_levels(float2 du, float2 dv, float2 base, int num_levels, int max_aniso)
{
    EWAFootprint fp;

    // longer axis first, measured in base-level texels: du and dv are the step from one destination
    // pixel to the next, so their lengths are how far across the source that pixel reaches
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

    // blend across the two levels either side, since a snapped level aliases in bands wherever the scale
    // crosses a power of two. Clamp the lod before splitting it into level and fraction, or a lod just
    // below 0 blends toward level 1.
    const float lod = std::clamp(std::log2(shorter), 0.f, float(num_levels - 1));
    fp.lo           = int(std::floor(lod));
    fp.hi           = std::min(fp.lo + 1, num_levels - 1);
    fp.blend        = lod - float(fp.lo);
    return fp;
}

/**
    A source image prepared to be read by direction instead of by image coordinate.

    A cube map's layout is six charts in one image: texels either side of a face join are not neighboring
    directions, some cells stand for no direction, and a pyramid over the whole picture averages both into
    the faces. So a cube map is taken apart into six faces, each with its own pyramid and ring; every other
    mapping keeps the single image it is, read through texel()'s wrap rules.
*/
class EnvSource
{
public:
    /// \p with_mips builds the levels EWA needs; point sampling reads only the finest.
    EnvSource(const Array2Df &src, EnvMapping mapping, bool with_mips) : m_src(src), m_mapping(mapping)
    {
        if (is_cube(mapping))
        {
            m_faces.push_back(faces_from_source(src, face_size_for(mapping, src.size()), mapping));
            while (with_mips && face_size(m_faces.back()) > 1) m_faces.push_back(decimated(m_faces.back()));
        }
        else if (with_mips)
            m_levels = build_mip_pyramid(src);
    }

    /// Bilinear at the finest level, in the direction \p d.
    float point(float3 d) const
    {
        if (!m_faces.empty())
            return sample_face_at(0, d);

        return sample_bilinear(level(0), envmap_xyz_to_uv(m_mapping, d), m_mapping);
    }

    /// Elliptically filtered over the footprint one destination pixel covers.
    /**
        \p uv is that pixel's center in \p dst's parameterization, \p delta_u and \p delta_v the step to
        the next pixel.
    */
    float ewa(EnvMapping dst, float2 uv, float2 delta_u, float2 delta_v, int max_aniso) const
    {
        const float3 d = envmap_uv_to_xyz(dst, uv);

        if (!m_faces.empty())
            return ewa_on_face(dst, uv, d, delta_u, delta_v, max_aniso);

        // footprint = source-space step to the neighboring destination pixel, which composes both
        // mappings at once. Use the smaller of the forward and backward differences so a seam (lat-long
        // wrap, disc rim) on one side doesn't blow it up.
        const float2 c    = envmap_xyz_to_uv(m_mapping, d);
        auto         step = [&](float2 delta)
        {
            const float2 fwd = convert_envmap_uv(m_mapping, dst, uv + delta) - c;
            const float2 bwd = c - convert_envmap_uv(m_mapping, dst, uv - delta);
            return la::length2(fwd) <= la::length2(bwd) ? fwd : bwd;
        };

        const float2       base{float(level(0).width()), float(level(0).height())};
        const EWAFootprint fp = choose_ewa_levels(step(delta_u), step(delta_v), base, num_levels(), max_aniso);

        if (fp.degenerate)
            return sample_bilinear(level(0), c, m_mapping);

        const float low = ewa_level(fp.lo, c, fp.d0, fp.d1);
        return fp.lo == fp.hi || fp.blend <= 0.f
                   ? low
                   : (1.f - fp.blend) * low + fp.blend * ewa_level(fp.hi, c, fp.d0, fp.d1);
    }

private:
    int num_levels() const { return std::max(1, int(m_levels.size())); }

    /// Level \p i of the whole-image pyramid; the source itself when there is none, it being level 0.
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

        // an ellipse narrow enough to fall between texels encloses none of them
        return sample_bilinear(a, uv, m_mapping);
    }

    float ewa_face(int lvl, int face, float2 st, float2 d0, float2 d1) const
    {
        const Array2Df &f = m_faces[size_t(lvl)][size_t(face)];
        const int       n = f.width() - 2;

        // the ring covers the first texel past an edge, which is as far as all but the widest ellipse
        // reaches; beyond it the face coordinates still name a direction, so look up whichever face that
        // direction lands on
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

    float ewa_on_face(EnvMapping dst, float2 uv, float3 d, float2 delta_u, float2 delta_v, int max_aniso) const
    {
        const int    face = cube_face_of(d);
        const float2 st   = cube_face_st(face, d);

        // the footprint in this face's own coordinates, since a step across a face join is a jump to
        // somewhere else in the image. Smaller of the two differences, as above.
        auto step = [&](float2 delta)
        {
            const float2 fwd = cube_face_st(face, envmap_uv_to_xyz(dst, uv + delta)) - st;
            const float2 bwd = st - cube_face_st(face, envmap_uv_to_xyz(dst, uv - delta));
            return la::length2(fwd) <= la::length2(bwd) ? fwd : bwd;
        };

        const float2       base{float(face_size(m_faces[0]))};
        const EWAFootprint fp = choose_ewa_levels(step(delta_u), step(delta_v), base, int(m_faces.size()), max_aniso);

        if (fp.degenerate)
            return sample_face_at(0, d);

        const float low = ewa_face(fp.lo, face, st, fp.d0, fp.d1);
        return fp.lo == fp.hi || fp.blend <= 0.f
                   ? low
                   : (1.f - fp.blend) * low + fp.blend * ewa_face(fp.hi, face, st, fp.d0, fp.d1);
    }

    const Array2Df        &m_src;
    EnvMapping             m_mapping;
    std::vector<Array2Df>  m_levels; ///< The whole image, for every mapping but a cube map
    std::vector<CubeLevel> m_faces;  ///< Six ringed faces per level, for a cube map
};
} // namespace

Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling, int supersample, AtomicProgress progress)
{
    Array2Df out{size};

    // supersample means max anisotropy to EWA and samples per axis to point sampling
    const int   ss  = std::max(1, supersample);
    const float inv = 1.f / float(ss * ss);

    // prepared once for the whole remap: the levels EWA reads, and, for a cube map, the six faces both
    // samplers read instead of the cross
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
                    // parts of a disc or a cube cross are not sphere; fill them from the nearest
                    // direction that is, so a later bilinear read at the rim doesn't pull in zeros
                    const float2 uv = nearest_valid_envmap_uv(
                        dst_mapping, float2{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)});

                    if (sampling == EnvMapSampling_EWA)
                    {
                        out(x, y) = source.ewa(dst_mapping, uv, float2{1.f / float(size.x), 0.f},
                                               float2{0.f, 1.f / float(size.y)}, ss);
                        continue;
                    }

                    float sum = 0.f;
                    for (int sy = 0; sy < ss; ++sy)
                        for (int sx = 0; sx < ss; ++sx)
                        {
                            // snapped like the pixel's center, since a sub-sample can land off the
                            // sphere even where the center did not
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
    // harmonic coefficients reproduce the irradiance to about a percent.
    //
    // Integrated over the image's own samples, each weighted by the solid angle it covers
    // (envmap_jacobian()), so every sample contributes once.
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

    // the constants that turn those coefficients into irradiance, from the same paper
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

            // divided by pi: what a white lambertian surface facing this way reflects, which is what an
            // environment lookup wants
            out(x, y) = float(e / 3.14159265358979323846);
        }

    ++progress;
    return out;
}
