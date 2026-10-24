//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "edit/progress.h"
#include "fwd.h"
#include "shared/envmap.h"

/// The ways an environment map can be flattened onto a rectangle.
/**
    Each is a pair of functions between a direction and a point on the image, all in the same world frame.
*/
enum EnvMapping : int
{
    EnvMapping_Angular = 0,   ///< Polar angle grows linearly with distance from the center
    EnvMapping_MirrorBall,    ///< What a photograph of a mirrored sphere records
    EnvMapping_LatLong,       ///< Longitude across, latitude down; the usual exchange format
    EnvMapping_Cylindrical,   ///< Longitude across, height down, so each row covers equal solid angle
    EnvMapping_CubeMap,       ///< Six faces laid out as a vertical cross
    EnvMapping_CubeMapColumn, ///< Six faces stacked in one column, as OpenEXR stores them
    EnvMapping_EqualArea,     ///< Clarberg's square-to-sphere map, which distributes samples evenly

    EnvMapping_COUNT
};
// the shader's names for them, which the shared mappings branch on
static_assert(EnvMapping_Angular == shader::ENVMAP_ANGULAR && EnvMapping_MirrorBall == shader::ENVMAP_MIRROR_BALL &&
              EnvMapping_LatLong == shader::ENVMAP_LAT_LONG && EnvMapping_Cylindrical == shader::ENVMAP_CYLINDRICAL &&
              EnvMapping_CubeMap == shader::ENVMAP_CUBE_MAP && EnvMapping_CubeMapColumn == shader::ENVMAP_CUBE_COLUMN &&
              EnvMapping_EqualArea == shader::ENVMAP_EQUAL_AREA);

const char *envmapping_name(int mapping);

/// Width divided by height for an image in \p mapping that wastes no resolution.
float envmapping_aspect(int mapping);

// The mappings themselves, which the image shader shares (src/shared/envmap.h): envmap_uv_to_xyz() and its inverse
// envmap_xyz_to_uv(), envmap_uv_is_valid() and nearest_valid_envmap_uv(), each taking an EnvMapping.
using shader::envmap_uv_is_valid;
using shader::envmap_uv_to_xyz;
using shader::envmap_xyz_to_uv;
using shader::nearest_valid_envmap_uv;

/// Where \p uv in the \p src mapping falls in the \p dst mapping.
inline float2 convert_envmap_uv(EnvMapping dst, EnvMapping src, float2 uv)
{
    return envmap_xyz_to_uv(dst, envmap_uv_to_xyz(src, uv));
}

/// Solid angle per unit image area at \p uv, in steradians; 0 off the sphere.
/**
    Weights anything integrating over the sphere, and integrates to 4*pi over [0,1]^2 for every mapping.
    Analytic, since a finite difference is wrong across cube seams.
*/
float envmap_jacobian(EnvMapping mapping, float2 uv);

/// How remapped_envmap() reads the source.
enum EnvMapSampling : int
{
    EnvMapSampling_Point = 0, ///< Supersample a Gaussian of half a pixel
    EnvMapSampling_EWA        ///< Elliptical Gaussian over mip levels
};
static_assert(EnvMapSampling_Point == shader::ENVMAP_SAMPLE_SUPERSAMPLE &&
                  EnvMapSampling_EWA == shader::ENVMAP_SAMPLE_EWA,
              "EnvMapSampling must match src/shared/envmap.h");

/// The rotation that turns by \p yaw about +y, then \p pitch about +x, then \p roll about +z, all in radians.
/**
    R = Ry(yaw) Rx(pitch) Rz(roll): yaw turns the horizon around the up axis, pitch tips it, and roll turns about
    the view's forward axis.
*/
float3x3 envmap_rotation(float yaw, float pitch, float roll);
/// The yaw, pitch and roll of \p rotation, with pitch in [-pi/2, pi/2]; the inverse of envmap_rotation().
/**
    At a pitch of a quarter turn the yaw and the roll turn about the same axis, so only their sum or difference is
    set; the roll is then taken as \p roll and the yaw makes up the rest.
*/
float3 envmap_euler(const float3x3 &rotation, float roll = 0.f);
/// The shortest rotation, as a unit quaternion (x, y, z, w), taking unit direction \p from to unit direction \p to.
float4 rotation_between(float3 from, float3 to);

/**
    Resample \p src, a \p src_mapping of the sphere, into \p dst_mapping at \p size, turned by \p rotation.

    The output's direction d reads the source in direction rotation d. Both samplers filter each pixel with a
    Gaussian of half a pixel, carried into the source by the remap. Point sampling spreads \p supersample^2 bilinear
    reads as that Gaussian is, and takes each through the remap itself. EWA takes the Gaussian through the remap's
    local stretch: it reads the mip levels its short axis calls for, at points along its long axis; there
    \p supersample is the maximum anisotropy. Both are envmap_supersample() and envmap_ewa() in
    src/shared/envmap_sample.h, which the image shader's preview shares.
*/
Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling = EnvMapSampling_Point, int supersample = 2,
                         const float3x3 &rotation = float3x3{la::identity}, AtomicProgress progress = {});

/// Convolve \p src, a \p mapping of incident radiance, with a clamped cosine, giving irradiance per direction.
/**
    Via nine spherical harmonic coefficients, which the cosine kernel's fast falloff makes enough, so this
    costs the two resolutions added and not multiplied.
*/
Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, AtomicProgress progress = {});
