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

/*!
    The ways an environment map can be flattened onto a rectangle.

    Each is a pair of functions between a direction on the sphere and a point on the image, which is all
    that converting between two of them needs: unproject through one, project through the other.

    Every one of these is in the same world frame, so a direction means the same thing whichever mapping
    produced it.
*/
enum EnvMapping : int
{
    EnvMapping_Angular = 0, //!< Polar angle grows linearly with distance from the center
    EnvMapping_MirrorBall,  //!< What a photograph of a mirrored sphere records
    EnvMapping_LatLong,     //!< Longitude across, latitude down; the usual exchange format
    EnvMapping_Cylindrical, //!< Longitude across, height down, so each row covers equal solid angle
    EnvMapping_CubeMap,     //!< Six faces laid out as a vertical cross
    EnvMapping_EqualArea,   //!< Clarberg's square-to-sphere map, which distributes samples evenly

    EnvMapping_COUNT
};

const char *envmapping_name(int mapping);

/*!
    Width divided by height for an image in \p mapping that wastes no resolution.

    A lat-long covers twice as much longitude as latitude, so it wants 2:1; the discs are round and want a
    square; the cube cross is three faces across by four down. Remapping to a size that ignores this either
    stretches the result or throws away samples along one axis.
*/
float envmapping_aspect(int mapping);

//! Direction for a point on the image, both in [0,1]^2 and on the unit sphere respectively.
float3 envmap_uv_to_xyz(EnvMapping mapping, float2 uv);
//! Point on the image for a direction; the inverse of envmap_uv_to_xyz().
float2 envmap_xyz_to_uv(EnvMapping mapping, float3 xyz);

/*!
    Where \p uv in the \p src mapping falls in the \p dst mapping.

    Composed of the two above, which is why adding a mapping means writing one pair of functions rather
    than one conversion per other mapping.
*/
inline float2 convert_envmap_uv(EnvMapping dst, EnvMapping src, float2 uv)
{
    return envmap_xyz_to_uv(dst, envmap_uv_to_xyz(src, uv));
}

/*!
    Whether \p uv is part of the sphere at all in \p mapping.

    Two of these do not fill their rectangle. The discs leave their corners empty, and the cube cross
    leaves four. Points there have no direction, and counting them would integrate some directions twice
    -- so anything summing over the image has to skip them.
*/
bool envmap_uv_is_valid(EnvMapping mapping, float2 uv);

/*!
    Solid angle per unit image area at \p uv, in steradians; 0 where \p uv is not sphere.

    How much sky one unit of image covers, which is what anything integrating over the sphere must weight
    by: a lat-long's polar rows cover far less than its equatorial ones. Analytic per mapping rather than
    measured from how far the direction moves, because the measured version is wrong at exactly the places
    that matter -- across a cube seam a finite difference straddles two faces and reports an area many
    times too large.

    Integrates to the sphere's 4*pi over the unit square, for every mapping.

    This is a ratio of areas, not a derivative. Filtering a remap anisotropically wants a footprint, which
    has a shape and not only a size, and the way to get that is the difference between *neighbouring
    destination pixels'* source coordinates -- which composes both mappings at once and needs none of this.
*/
float envmap_jacobian(EnvMapping mapping, float2 uv);

/*!
    Resample \p src, read as the \p src_mapping of a sphere, into \p dst_mapping at \p size.

    Every destination sample asks which direction it stands for, converts that direction into the source's
    parameterization, and reads there -- so nothing needs to know how the two mappings relate beyond the
    sphere they share.

    How the source is read is the choice that matters, since these mappings stretch unevenly and a
    destination pixel can cover a great many source ones:

    - Point supersampling takes \p supersample samples per axis inside each destination pixel and averages
      them. Simple and exact for magnification, but a minification of more than the sample count still
      aliases, and raising the count costs its square.
    - EWA reads a mip pyramid with an elliptical filter shaped by the footprint the destination pixel
      actually covers in the source. That footprint is anisotropic -- a lat-long's pole is stretched
      hundreds of times more across than down -- which is exactly what a mip level alone cannot express,
      and it costs the same whatever the scale.

    \p mip_bias shifts the level EWA computes, in levels: negative is sharper and eventually aliases,
    positive is blurrier. Zero is the level the footprint asks for, and is what anything other than
    diagnosing the filter should use.
*/
enum EnvMapSampling : int
{
    EnvMapSampling_Point = 0, //!< Supersample and average
    EnvMapSampling_EWA        //!< Elliptical filter over a mip pyramid
};

Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling = EnvMapSampling_Point, int supersample = 2, float mip_bias = 0.f,
                         AtomicProgress progress = {});

/*!
    Convolve \p src, a \p mapping of incident radiance, with a clamped cosine.

    Gives the irradiance arriving at a surface facing each direction -- what a diffuse surface reflects --
    which is why this is the one preprocessing step a lambertian environment lookup needs.

    Every output direction integrates over every input one, so this costs the product of the two
    resolutions and is by far the slowest thing here. The output is usually tiny for that reason: the
    result is so smooth that a few dozen samples across describe it.
*/
Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, AtomicProgress progress = {});
