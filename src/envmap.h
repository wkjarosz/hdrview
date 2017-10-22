//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <string>
#include <vector>
#include <Eigen/Core>

using UV2XYZFn = Eigen::Vector3f(const Eigen::Vector2f &);
using XYZ2UVFn = Eigen::Vector2f(const Eigen::Vector3f &);


enum EEnvMappingUVMode : int
{
	ANGULAR_MAP = 0,
	MIRROR_BALL,
	LAT_LONG,
	CYLINDRICAL,
	CUBE_MAP
};


/*!
 * @brief		Generic environment map conversion
 *
 * Converts from a source envmap uv parametrization \a src to
 * a destination envmap uv parametrization \a dst,
 * each specified using the \a EEnvMappingMode enumeriation
 *
 * @param dst 	Destination envmap parametrization
 * @param src	Source envmap parametrization
 * @param srcUV	2D uv coordinates in source parametrization
 */
Eigen::Vector2f convertEnvMappingUV(EEnvMappingUVMode dst, EEnvMappingUVMode src, const Eigen::Vector2f & srcUV);

const std::vector<std::string> & envMappingNames();

// functions that convert from UV image plane coordinates to
// XYZ world coordinates for the various light probe representations
Eigen::Vector3f angularMapToXYZ(const Eigen::Vector2f & uv);
Eigen::Vector3f mirrorBallToXYZ(const Eigen::Vector2f & uv);
Eigen::Vector3f latLongToXYZ(const Eigen::Vector2f & uv);
Eigen::Vector3f cylindricalToXYZ(const Eigen::Vector2f & uv);
Eigen::Vector3f cubeMapToXYZ(const Eigen::Vector2f & uv);

UV2XYZFn * envMapUVToXYZ(EEnvMappingUVMode mode);

// functions that convert from XYZ world coordinates to
// UV image plane coordinates for the various light probe representations
Eigen::Vector2f XYZToAngularMap(const Eigen::Vector3f & xyz);
Eigen::Vector2f XYZToMirrorBall(const Eigen::Vector3f & xyz);
Eigen::Vector2f XYZToLatLong(const Eigen::Vector3f & xyz);
Eigen::Vector2f XYZToCylindrical(const Eigen::Vector3f & xyz);
Eigen::Vector2f XYZToCubeMap(const Eigen::Vector3f & xyz);

XYZ2UVFn * XYZToEnvMapUV(EEnvMappingUVMode mode);
