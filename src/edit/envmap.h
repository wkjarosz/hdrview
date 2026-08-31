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
    Resample \p src, read as the \p src_mapping of a sphere, into \p dst_mapping at \p size.

    Every destination sample asks which direction it stands for, converts that direction into the source's
    parameterization, and reads there -- so nothing needs to know how the two mappings relate beyond the
    sphere they share.

    \p supersample samples per axis within each destination pixel, averaged. Worth more than it looks:
    the mappings stretch wildly in places (a lat-long's poles, a disc's rim), and one sample per pixel
    aliases badly wherever the source is being minified.
*/
Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         int supersample = 2, FilterProgress progress = {});

/*!
    Convolve \p src, a \p mapping of incident radiance, with a clamped cosine.

    Gives the irradiance arriving at a surface facing each direction -- what a diffuse surface reflects --
    which is why this is the one preprocessing step a lambertian environment lookup needs.

    Every output direction integrates over every input one, so this costs the product of the two
    resolutions and is by far the slowest thing here. The output is usually tiny for that reason: the
    result is so smooth that a few dozen samples across describe it.
*/
Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, FilterProgress progress = {});
