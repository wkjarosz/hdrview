//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
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
    EnvMapSampling_Point = 0, ///< Supersample and average
    EnvMapSampling_EWA        ///< Elliptical filter over a mip pyramid
};

/**
    Resample \p src, a \p src_mapping of the sphere, into \p dst_mapping at \p size.

    Point sampling averages \p supersample^2 samples per output pixel. EWA reads a mip pyramid through an
    ellipse fitted to the pixel's footprint in the source (PBRT's MIPMap::EWA); there \p supersample is the
    maximum anisotropy.
*/
Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling = EnvMapSampling_Point, int supersample = 2,
                         AtomicProgress progress = {});

/// Convolve \p src, a \p mapping of incident radiance, with a clamped cosine, giving irradiance per direction.
/**
    Via nine spherical harmonic coefficients, which the cosine kernel's fast falloff makes enough, so this
    costs the two resolutions added and not multiplied.
*/
Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, AtomicProgress progress = {});
