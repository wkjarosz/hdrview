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

//! The ways an environment map can be flattened onto a rectangle. Each is a pair of functions between a
//! direction and a point on the image, all in the same world frame.
enum EnvMapping : int
{
    EnvMapping_Angular = 0,   //!< Polar angle grows linearly with distance from the center
    EnvMapping_MirrorBall,    //!< What a photograph of a mirrored sphere records
    EnvMapping_LatLong,       //!< Longitude across, latitude down; the usual exchange format
    EnvMapping_Cylindrical,   //!< Longitude across, height down, so each row covers equal solid angle
    EnvMapping_CubeMap,       //!< Six faces laid out as a vertical cross
    EnvMapping_CubeMapColumn, //!< Six faces stacked in one column, as OpenEXR stores them
    EnvMapping_EqualArea,     //!< Clarberg's square-to-sphere map, which distributes samples evenly

    EnvMapping_COUNT
};

const char *envmapping_name(int mapping);

//! Width divided by height for an image in \p mapping that wastes no resolution: 2:1 for a lat-long,
//! square for the discs, 3:4 for the cube cross.
float envmapping_aspect(int mapping);

//! Direction for a point on the image, both in [0,1]^2 and on the unit sphere respectively.
float3 envmap_uv_to_xyz(EnvMapping mapping, float2 uv);
//! Point on the image for a direction; the inverse of envmap_uv_to_xyz().
float2 envmap_xyz_to_uv(EnvMapping mapping, float3 xyz);

//! Where \p uv in the \p src mapping falls in the \p dst mapping.
inline float2 convert_envmap_uv(EnvMapping dst, EnvMapping src, float2 uv)
{
    return envmap_xyz_to_uv(dst, envmap_uv_to_xyz(src, uv));
}

//! Whether \p uv is part of the sphere at all in \p mapping. The discs leave their corners empty and the
//! cube cross leaves four; anything summing over the image has to skip those.
bool envmap_uv_is_valid(EnvMapping mapping, float2 uv);

//! Nearest point of the sphere to \p uv (\p uv itself if it is already on the sphere).
//! Used to fill the empty corners of a disc or cube cross so bilinear reads at the rim don't pull in zeros.
float2 nearest_valid_envmap_uv(EnvMapping mapping, float2 uv);

//! Solid angle per unit image area at \p uv, in steradians; 0 off the sphere. Weights anything
//! integrating over the sphere. Integrates to 4*pi over [0,1]^2 for every mapping. Analytic, since a
//! finite difference is wrong across cube seams.
float envmap_jacobian(EnvMapping mapping, float2 uv);

//! How remapped_envmap() reads the source.
enum EnvMapSampling : int
{
    EnvMapSampling_Point = 0, //!< Supersample and average
    EnvMapSampling_EWA        //!< Elliptical filter over a mip pyramid
};

/*!
    Resample \p src, a \p src_mapping of the sphere, into \p dst_mapping at \p size.

    Point sampling averages \p supersample^2 samples per output pixel. EWA reads a mip pyramid through an
    ellipse fitted to the pixel's footprint in the source (PBRT's MIPMap::EWA); there \p supersample is the
    maximum anisotropy, and \p mip_bias shifts the level it computes (negative sharper, positive blurrier).
*/
Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling = EnvMapSampling_Point, int supersample = 2, float mip_bias = 0.f,
                         AtomicProgress progress = {});

//! Convolve \p src, a \p mapping of incident radiance, with a clamped cosine, giving the irradiance
//! arriving at a surface facing each direction. Via nine spherical harmonic coefficients, which the cosine
//! kernel's fast falloff makes enough, so this costs the two resolutions added and not multiplied.
Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, AtomicProgress progress = {});
