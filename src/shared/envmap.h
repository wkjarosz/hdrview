/** \file envmap.h
    The ways an environment map flattens the sphere onto a rectangle, in both directions, and how a point moves
    across one, which the remap edits and the image shader's preview of them compile from this one source. See shim.h
    for the dialect it is written in.

    A mapping is one of the ENVMAP_ constants below, the values of EnvMapping in src/edit/envmap.h. An image point is
    in [0,1]^2 and a direction on the unit sphere, y up. tests/test_envmap.cpp holds every mapping to its inverse, to
    its poles and edges, and to the solid angle it covers.
*/

#ifndef HDRVIEW_SHARED_ENVMAP_H
#define HDRVIEW_SHARED_ENVMAP_H

#ifdef __cplusplus
#include "shim.h"
#endif

SHARED_BEGIN

// clang-format off
SHARED_CONST int ENVMAP_ANGULAR        = 0; ///< Polar angle grows linearly with distance from the center
SHARED_CONST int ENVMAP_MIRROR_BALL    = 1; ///< What a photograph of a mirrored sphere records
SHARED_CONST int ENVMAP_LAT_LONG       = 2; ///< Longitude across, latitude down
SHARED_CONST int ENVMAP_CYLINDRICAL    = 3; ///< Longitude across, height down, so each row covers equal solid angle
SHARED_CONST int ENVMAP_CUBE_MAP       = 4; ///< Six faces laid out as a vertical cross
SHARED_CONST int ENVMAP_CUBE_COLUMN    = 5; ///< Six faces stacked in one column, as OpenEXR stores them
SHARED_CONST int ENVMAP_EQUAL_AREA     = 6; ///< Clarberg's square-to-sphere map

// How a remap reads its source; see envmap_supersample() and envmap_ewa() in envmap_sample.h.
SHARED_CONST int ENVMAP_SAMPLE_SUPERSAMPLE = 0;
SHARED_CONST int ENVMAP_SAMPLE_EWA         = 1;

// The six faces of the cube, in the order everything below indexes them.
SHARED_CONST int FACE_POS_X = 0;
SHARED_CONST int FACE_NEG_X = 1;
SHARED_CONST int FACE_POS_Y = 2;
SHARED_CONST int FACE_NEG_Y = 3;
SHARED_CONST int FACE_POS_Z = 4;
SHARED_CONST int FACE_NEG_Z = 5;

SHARED_CONST float ENVMAP_PI = 3.14159265358979323846f;
// clang-format on

//
// The cube's six faces, each in the one orientation both cube layouts start from
//

/// Direction for \p st on \p face, in that face's own [0,1]^2; not normalized.
/**
    Not clamped: an \p st outside [0,1]^2 names a direction past that edge of the cube, which is how a face reaches
    its neighbors.
*/
SHARED_FN vec3 cube_face_vector(int face, vec2 st)
{
    float a = 2.0f * st.x - 1.0f, b = 2.0f * st.y - 1.0f;
    if (face == FACE_POS_X)
        return vec3(1.0f, -b, -a);
    if (face == FACE_NEG_X)
        return vec3(-1.0f, -b, a);
    if (face == FACE_POS_Y)
        return vec3(a, 1.0f, b);
    if (face == FACE_NEG_Y)
        return vec3(a, -1.0f, -b);
    if (face == FACE_POS_Z)
        return vec3(a, -b, 1.0f);
    return vec3(a, b, -1.0f);
}

/// Which face \p d falls on: the axis it points most steeply along, ties going to x and then y.
SHARED_FN int cube_face_of(vec3 d)
{
    float x = abs(d.x), y = abs(d.y), z = abs(d.z);
    if (x >= y && x >= z)
        return d.x < 0.0f ? FACE_NEG_X : FACE_POS_X;
    if (y >= z)
        return d.y < 0.0f ? FACE_NEG_Y : FACE_POS_Y;
    return d.z < 0.0f ? FACE_NEG_Z : FACE_POS_Z;
}

/// Where \p d falls on \p face's plane, in that face's own [0,1]^2; the inverse of cube_face_vector().
/**
    \p face need not be the one \p d is really on: a direction off to the side lands outside [0,1]^2, so a footprint
    reaching past an edge is still described in one face's coordinates. A direction at or behind the face's horizon
    has no place on its plane; the floor on the divisor puts it far outside the face instead of at infinity.
*/
SHARED_FN vec2 cube_face_st(int face, vec3 d)
{
    float axis = face == FACE_POS_X   ? d.x
                 : face == FACE_NEG_X ? -d.x
                 : face == FACE_POS_Y ? d.y
                 : face == FACE_NEG_Y ? -d.y
                 : face == FACE_POS_Z ? d.z
                                      : -d.z;
    vec3  t    = d / max(axis, 1e-6f);

    float a, b;
    if (face == FACE_POS_X)
    {
        a = -t.z;
        b = -t.y;
    }
    else if (face == FACE_NEG_X)
    {
        a = t.z;
        b = -t.y;
    }
    else if (face == FACE_POS_Y)
    {
        a = t.x;
        b = t.z;
    }
    else if (face == FACE_NEG_Y)
    {
        a = t.x;
        b = -t.z;
    }
    else if (face == FACE_POS_Z)
    {
        a = t.x;
        b = -t.y;
    }
    else
    {
        a = t.x;
        b = t.y;
    }
    return vec2(0.5f * (a + 1.0f), 0.5f * (b + 1.0f));
}

/// The cell of the cross's 3-by-4 grid holding \p face, as (column, row).
SHARED_FN ivec2 cross_cell(int face)
{
    if (face == FACE_POS_X)
        return ivec2(2, 1);
    if (face == FACE_NEG_X)
        return ivec2(0, 1);
    if (face == FACE_POS_Y)
        return ivec2(1, 0);
    if (face == FACE_NEG_Y)
        return ivec2(1, 2);
    if (face == FACE_POS_Z)
        return ivec2(1, 1);
    return ivec2(1, 3);
}

/// The face in the cross's (\p col, \p row) cell; the inverse of cross_cell().
SHARED_FN int cross_face(int col, int row)
{
    if (col == 0)
        return FACE_NEG_X;
    if (col == 2)
        return FACE_POS_X;
    return row == 0 ? FACE_POS_Y : row == 1 ? FACE_POS_Z : row == 2 ? FACE_NEG_Y : FACE_NEG_Z;
}

/// Whether the single-column layout flips \p face across, rather than down, to match OpenEXR.
SHARED_FN bool column_flips_s(int face) { return face == FACE_POS_X || face == FACE_NEG_X || face == FACE_POS_Z; }

/// \p st turned as the single-column layout turns \p face, which being one flip is its own inverse.
SHARED_FN vec2 column_st(int face, vec2 st)
{ return column_flips_s(face) ? vec2(1.0f - st.x, st.y) : vec2(st.x, 1.0f - st.y); }

/// Whether \p mapping lays the sphere out as six flat faces, whichever arrangement it uses for them.
SHARED_FN bool envmap_is_cube(int mapping) { return mapping == ENVMAP_CUBE_MAP || mapping == ENVMAP_CUBE_COLUMN; }

/// Side of the faces \p mapping carves out of an image of \p size.
SHARED_FN int envmap_face_size(int mapping, ivec2 size)
{
    if (mapping == ENVMAP_CUBE_COLUMN)
        return max(1, min(size.x, size.y / 6));
    return max(1, min(size.x / 3, size.y / 4));
}

/// Where texel \p p of \p face, \p n texels a side, lies in the image a cube map of \p mapping lays out.
SHARED_FN ivec2 envmap_face_texel_in_image(int mapping, int face, ivec2 p, int n)
{
    if (mapping == ENVMAP_CUBE_COLUMN)
        return column_flips_s(face) ? ivec2(n - 1 - p.x, face * n + p.y) : ivec2(p.x, face * n + n - 1 - p.y);
    ivec2 cell = cross_cell(face);
    return ivec2(cell.x * n + p.x, cell.y * n + p.y);
}

//
// Image point -> direction
//

SHARED_FN vec3 angular_to_xyz(vec2 uv)
{
    // the image plane from -1 to 1, centered on the image, where the polar angle grows linearly with the radius
    vec2  xy      = vec2(2.0f * uv.x - 1.0f, 2.0f * uv.y - 1.0f);
    float phi     = clamp(sqrt(dot(xy, xy)) * ENVMAP_PI, 0.0f, ENVMAP_PI);
    float theta   = atan2(xy.y, xy.x);
    float sin_phi = sin(phi);
    return vec3(sin_phi * cos(theta), -sin_phi * sin(theta), cos(phi));
}

SHARED_FN vec3 mirror_ball_to_xyz(vec2 uv)
{
    // the sine of half the polar angle grows linearly with the radius
    vec2  xy      = vec2(2.0f * uv.x - 1.0f, 2.0f * uv.y - 1.0f);
    float phi     = 2.0f * asin(clamp(sqrt(dot(xy, xy)), 0.0f, 1.0f));
    float theta   = atan2(xy.y, xy.x);
    float sin_phi = sin(phi);
    return vec3(sin_phi * cos(theta), -sin_phi * sin(theta), cos(phi));
}

SHARED_FN vec3 lat_long_to_xyz(vec2 uv)
{
    // longitude linear in u, latitude linear in v
    float theta   = ENVMAP_PI * (1.5f - 2.0f * uv.x);
    float phi     = uv.y * ENVMAP_PI;
    float sin_phi = sin(phi);
    return vec3(sin_phi * cos(theta), cos(phi), sin_phi * sin(theta));
}

SHARED_FN vec3 cylindrical_to_xyz(vec2 uv)
{
    // longitude linear in u, and height linear in v, so every row covers the same solid angle
    float theta   = ENVMAP_PI * (1.5f - 2.0f * uv.x);
    float cos_phi = 1.0f - 2.0f * uv.y;
    float sin_phi = sqrt(max(0.0f, 1.0f - cos_phi * cos_phi));
    return vec3(sin_phi * cos(theta), cos_phi, sin_phi * sin(theta));
}

/// The cube face vector for \p uv in the vertical cross, before normalizing: one component is +-1.
/**
    The upright column of four down the middle third, and the two side faces either side of its second cell. A uv
    in one of the four corner cells stands for no direction; clamping it into the side face beside it gives that
    face's edge. Its length is what the Jacobian needs.
*/
SHARED_FN vec3 cube_map_face_vector(vec2 uv)
{
    bool  middle = uv.x >= 1.0f / 3.0f && uv.x <= 2.0f / 3.0f;
    int   col    = middle ? 1 : (uv.x < 1.0f / 3.0f ? 0 : 2);
    int   row    = middle ? (uv.y <= 0.25f ? 0 : uv.y <= 0.5f ? 1 : uv.y <= 0.75f ? 2 : 3) : 1;
    float u      = middle ? uv.x : clamp(uv.x, float(col) / 3.0f, float(col + 1) / 3.0f);
    float v      = middle ? uv.y : clamp(uv.y, 0.25f, 0.5f);
    return cube_face_vector(cross_face(col, row), vec2(u * 3.0f - float(col), v * 4.0f - float(row)));
}

SHARED_FN vec3 cube_map_to_xyz(vec2 uv) { return normalize(cube_map_face_vector(uv)); }

/// The cube face vector for \p uv in the single-column layout, before normalizing.
/**
    Six faces stacked top to bottom in the face order, each turned the way OpenEXR turns it. OpenEXR puts its
    outermost samples exactly on the face edges, where this puts them half a texel inside as a GPU does: the same
    layout, its sample grid offset by half a texel.
*/
SHARED_FN vec3 cube_column_face_vector(vec2 uv)
{
    int face = clamp(int(uv.y * 6.0f), 0, 5);
    return cube_face_vector(face, column_st(face, vec2(uv.x, uv.y * 6.0f - float(face))));
}

SHARED_FN vec3 cube_column_to_xyz(vec2 uv) { return normalize(cube_column_face_vector(uv)); }

/// Clarberg's equal-area square to sphere, from "Fast Equal-Area Mapping of the (Hemi)Sphere using SIMD", via PBRTv4.
SHARED_FN vec3 equal_area_to_xyz(vec2 p)
{
    float u = 2.0f * p.x - 1.0f, v = 2.0f * p.y - 1.0f;
    float up = abs(u), vp = abs(v);

    // the radius as the signed distance from the diagonal
    float signed_distance = 1.0f - (up + vp);
    float r               = 1.0f - abs(signed_distance);

    float phi     = (r == 0.0f ? 1.0f : (vp - up) / r + 1.0f) * ENVMAP_PI / 4.0f;
    float z       = copysign(1.0f - r * r, signed_distance);
    float cos_phi = copysign(cos(phi), u);
    float sin_phi = copysign(sin(phi), v);
    float scale   = r * sqrt(max(0.0f, 2.0f - r * r));
    return vec3(cos_phi * scale, z, -sin_phi * scale);
}

//
// Direction -> image point
//

SHARED_FN vec2 xyz_to_angular(vec3 xyz)
{
    float phi   = acos(clamp(xyz.z, -1.0f, 1.0f));
    float theta = atan2(xyz.y, xyz.x);
    float u     = (phi / ENVMAP_PI) * cos(theta);
    float v     = -(phi / ENVMAP_PI) * sin(theta);
    return vec2(0.5f * (u + 1.0f), 0.5f * (v + 1.0f));
}

SHARED_FN vec2 xyz_to_mirror_ball(vec3 xyz)
{
    float phi          = acos(clamp(xyz.z, -1.0f, 1.0f));
    float theta        = atan2(xyz.y, xyz.x);
    float sin_half_phi = sin(phi / 2.0f);
    return vec2(0.5f * (sin_half_phi * cos(theta) + 1.0f), 0.5f * (-sin_half_phi * sin(theta) + 1.0f));
}

/// How far around a full turn the longitude \p theta is, as lat_long_to_xyz() lays it out across u.
SHARED_FN float longitude_u(float theta)
{
    float t = 0.75f - theta / (2.0f * ENVMAP_PI);
    return t - floor(t);
}

SHARED_FN vec2 xyz_to_lat_long(vec3 xyz)
{
    float phi = acos(clamp(xyz.y, -1.0f, 1.0f));
    return vec2(longitude_u(atan2(xyz.z, xyz.x)), phi / ENVMAP_PI);
}

SHARED_FN vec2 xyz_to_cylindrical(vec3 xyz) { return vec2(longitude_u(atan2(xyz.z, xyz.x)), (1.0f - xyz.y) / 2.0f); }

SHARED_FN vec2 xyz_to_cube_map(vec3 xyz)
{
    // the face says which side is seen, and its cell where that side sits in the 3-by-4 grid
    int   face = cube_face_of(xyz);
    vec2  st   = cube_face_st(face, xyz);
    ivec2 cell = cross_cell(face);
    return vec2((float(cell.x) + st.x) / 3.0f, (float(cell.y) + st.y) / 4.0f);
}

SHARED_FN vec2 xyz_to_cube_column(vec3 xyz)
{
    int  face = cube_face_of(xyz);
    vec2 st   = column_st(face, cube_face_st(face, xyz));
    return vec2(st.x, (float(face) + st.y) / 6.0f);
}

SHARED_FN vec2 xyz_to_equal_area(vec3 xyz)
{
    // a 90-degree turn from PBRT's frame, so that this agrees with the mappings above
    vec3  d = vec3(xyz.x, -xyz.z, xyz.y);
    float x = abs(d.x), y = abs(d.y), z = abs(d.z);
    float r = sqrt(max(0.0f, 1.0f - z));

    float a = max(x, y);
    float b = a == 0.0f ? 0.0f : min(x, y) / a;

    // the minimax polynomial for atan(b) 2 / pi over [0,1], to sixth degree, by Horner's rule
    float phi =
        0.406758566246788489601959989e-5f +
        b * (0.636226545274016134946890922156f +
             b * (0.61572017898280213493197203466e-2f +
                  b * (-0.247333733281268944196501420480f +
                       b * (0.881770664775316294736387951347e-1f +
                            b * (0.419038818029165735901852432784e-1f + b * -0.251390972343483509333252996350e-1f)))));
    if (x < y)
        phi = 1.0f - phi;

    float v = phi * r;
    float u = r - v;
    if (d.z < 0.0f)
    {
        float swapped = u;
        u             = 1.0f - v;
        v             = 1.0f - swapped;
    }
    return vec2(0.5f * (copysign(u, d.x) + 1.0f), 0.5f * (copysign(v, d.y) + 1.0f));
}

//
// Dispatch over the mappings
//

/// Direction on the unit sphere for the image point \p uv, in [0,1]^2, of \p mapping.
SHARED_FN vec3 envmap_uv_to_xyz(int mapping, vec2 uv)
{
    if (mapping == ENVMAP_ANGULAR)
        return angular_to_xyz(uv);
    if (mapping == ENVMAP_MIRROR_BALL)
        return mirror_ball_to_xyz(uv);
    if (mapping == ENVMAP_LAT_LONG)
        return lat_long_to_xyz(uv);
    if (mapping == ENVMAP_CYLINDRICAL)
        return cylindrical_to_xyz(uv);
    if (mapping == ENVMAP_CUBE_MAP)
        return cube_map_to_xyz(uv);
    if (mapping == ENVMAP_CUBE_COLUMN)
        return cube_column_to_xyz(uv);
    return equal_area_to_xyz(uv);
}

/// Point on the image in \p mapping for a direction; the inverse of envmap_uv_to_xyz().
SHARED_FN vec2 envmap_xyz_to_uv(int mapping, vec3 xyz)
{
    if (mapping == ENVMAP_ANGULAR)
        return xyz_to_angular(xyz);
    if (mapping == ENVMAP_MIRROR_BALL)
        return xyz_to_mirror_ball(xyz);
    if (mapping == ENVMAP_LAT_LONG)
        return xyz_to_lat_long(xyz);
    if (mapping == ENVMAP_CYLINDRICAL)
        return xyz_to_cylindrical(xyz);
    if (mapping == ENVMAP_CUBE_MAP)
        return xyz_to_cube_map(xyz);
    if (mapping == ENVMAP_CUBE_COLUMN)
        return xyz_to_cube_column(xyz);
    return xyz_to_equal_area(xyz);
}

/// Whether \p uv is part of the sphere at all in \p mapping.
/**
    The discs fill only their inscribed circle, and the cross only its column of four and the row across its second
    cell; every other mapping covers its whole image. Anything summing over the image has to skip the rest.
*/
SHARED_FN bool envmap_uv_is_valid(int mapping, vec2 uv)
{
    if (mapping == ENVMAP_ANGULAR || mapping == ENVMAP_MIRROR_BALL)
    {
        vec2 d = vec2(2.0f * uv.x - 1.0f, 2.0f * uv.y - 1.0f);
        return dot(d, d) <= 1.0f;
    }
    if (mapping == ENVMAP_CUBE_MAP)
        return (uv.x >= 1.0f / 3.0f && uv.x <= 2.0f / 3.0f) || (uv.y >= 0.25f && uv.y <= 0.5f);
    return true;
}

/// Nearest point of the sphere to \p uv in \p mapping (\p uv itself if it is already on the sphere).
/**
    What fills the parts of an image that are not sphere, so a bilinear read at the rim does not pull in zeros. A
    disc's corner goes straight out onto its circle, landing a hair inside it, where the mapping is less singular; a
    cross's corner cell goes to the nearer of its two arms.
*/
SHARED_FN vec2 nearest_valid_envmap_uv(int mapping, vec2 uv)
{
    if (envmap_uv_is_valid(mapping, uv))
        return uv;

    if (mapping == ENVMAP_ANGULAR || mapping == ENVMAP_MIRROR_BALL)
    {
        vec2  d = vec2(2.0f * uv.x - 1.0f, 2.0f * uv.y - 1.0f);
        float r = sqrt(dot(d, d));
        if (r <= 0.0f)
            return uv;
        float s = (1.0f - 1e-4f) / r;
        return vec2(0.5f * (d.x * s + 1.0f), 0.5f * (d.y * s + 1.0f));
    }
    if (mapping == ENVMAP_CUBE_MAP)
    {
        vec2 to_column = vec2(clamp(uv.x, 1.0f / 3.0f, 2.0f / 3.0f), uv.y);
        vec2 to_row    = vec2(uv.x, clamp(uv.y, 0.25f, 0.5f));
        vec2 dc = to_column - uv, dr = to_row - uv;
        return dot(dc, dc) <= dot(dr, dr) ? to_column : to_row;
    }
    return uv;
}

//
// Sampling around a point
//

/// Point (\p i, \p j) of an \p n by \p n set of equally weighted samples of a Gaussian, before its scaling.
/**
    The centers of the unit square's strata, taken to the disc by Shirley and Chiu's concentric map in Dave Cline's
    form, with the fixes its readers posted (the second branch's angle pi/2 - (pi/4)(a/b), which matches the original
    map, and a guard at the middle, which divided by zero), then given Box-Muller's radius, sqrt(-2 ln(1 - r^2)). No
    stratum's center reaches the square's edge, where that radius goes to infinity, so the set stops short of the
    tails and is narrower than the Gaussian; envmap_gaussian_scale() gives it the Gaussian's variance. A set of one is
    the middle point alone.
*/
SHARED_FN vec2 envmap_gaussian_point(int i, int j, int n)
{
    float a = 2.0f * (float(i) + 0.5f) / float(n) - 1.0f, b = 2.0f * (float(j) + 0.5f) / float(n) - 1.0f;
    float r, phi;
    if (abs(a) > abs(b))
    {
        r   = a;
        phi = (ENVMAP_PI / 4.0f) * (b / a);
    }
    else if (b != 0.0f)
    {
        r   = b;
        phi = ENVMAP_PI / 2.0f - (ENVMAP_PI / 4.0f) * (a / b);
    }
    else
        return vec2(0.0f);
    // r is signed, so r (cos phi, sin phi) is the point on the disc, and its radius is |r|
    float g = sqrt(-2.0f * log(1.0f - r * r));
    return (g / abs(r)) * vec2(r * cos(phi), r * sin(phi));
}

/// What envmap_gaussian_point()'s \p n by \p n set is scaled by to sample a Gaussian of standard deviation \p sigma.
/**
    The set's mean squared radius made the Gaussian's, 2 sigma^2, so that each n has the same variance, the set being
    the same down both axes and across them.
*/
SHARED_FN float envmap_gaussian_scale(int n, float sigma)
{
    float sum = 0.0f;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
        {
            vec2 p = envmap_gaussian_point(i, j, n);
            sum += dot(p, p);
        }
    return sum > 0.0f ? sigma * sqrt(2.0f * float(n * n) / sum) : 0.0f;
}

/// The direction of output point \p uv of \p dst at \p size, moved \p o output pixels, as the sphere continues.
/**
    A cube layout's point moves across the plane of its own face, which goes on past the face's edges to the
    directions of the faces beyond, as the cube does; the layout's next cell may hold another face entirely. Elsewhere
    the point moves across the image: past a lat-long's edges its own functions continue onto the sphere, past a
    cylinder's pole it comes back down half a turn round, and past the equal-area square's edge it folds back across
    it. Past a disc's rim, the one direction -z, it stops at the rim.
*/
SHARED_FN vec3 envmap_moved_dir(int dst, ivec2 size, vec2 uv, vec2 o)
{
    if (envmap_is_cube(dst))
    {
        vec3  d    = envmap_uv_to_xyz(dst, uv);
        int   face = cube_face_of(d);
        float n    = float(envmap_face_size(dst, size));
        // the column turns a face's st as column_st() says, which is a flip either across or down
        vec2 step = dst == ENVMAP_CUBE_COLUMN ? (column_flips_s(face) ? vec2(-o.x, o.y) : vec2(o.x, -o.y)) : o;
        return normalize(cube_face_vector(face, cube_face_st(face, d) + step / n));
    }

    vec2 p = uv + o / vec2(float(size.x), float(size.y));
    if (dst == ENVMAP_CYLINDRICAL && (p.y < 0.0f || p.y > 1.0f))
        p = vec2(p.x + 0.5f, p.y < 0.0f ? -p.y : 2.0f - p.y);
    else if (dst == ENVMAP_EQUAL_AREA)
    {
        if (p.x < 0.0f || p.x > 1.0f)
            p = vec2(p.x < 0.0f ? -p.x : 2.0f - p.x, 1.0f - p.y);
        if (p.y < 0.0f || p.y > 1.0f)
            p = vec2(1.0f - p.x, p.y < 0.0f ? -p.y : 2.0f - p.y);
    }
    return envmap_uv_to_xyz(dst, nearest_valid_envmap_uv(dst, p));
}

SHARED_END

#endif // HDRVIEW_SHARED_ENVMAP_H
