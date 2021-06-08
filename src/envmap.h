//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <nanogui/vector.h>
#include <string>
#include <vector>

using UV2XYZFn = nanogui::Vector3f(const nanogui::Vector2f &);
using XYZ2UVFn = nanogui::Vector2f(const nanogui::Vector3f &);

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
nanogui::Vector2f convertEnvMappingUV(EEnvMappingUVMode dst, EEnvMappingUVMode src, const nanogui::Vector2f &srcUV);

const std::vector<std::string> &envMappingNames();

// functions that convert from UV image plane coordinates to
// XYZ world coordinates for the various light probe representations
nanogui::Vector3f angularMapToXYZ(const nanogui::Vector2f &uv);
nanogui::Vector3f mirrorBallToXYZ(const nanogui::Vector2f &uv);
nanogui::Vector3f latLongToXYZ(const nanogui::Vector2f &uv);
nanogui::Vector3f cylindricalToXYZ(const nanogui::Vector2f &uv);
nanogui::Vector3f cubeMapToXYZ(const nanogui::Vector2f &uv);

UV2XYZFn *envMapUVToXYZ(EEnvMappingUVMode mode);

// functions that convert from XYZ world coordinates to
// UV image plane coordinates for the various light probe representations
nanogui::Vector2f XYZToAngularMap(const nanogui::Vector3f &xyz);
nanogui::Vector2f XYZToMirrorBall(const nanogui::Vector3f &xyz);
nanogui::Vector2f XYZToLatLong(const nanogui::Vector3f &xyz);
nanogui::Vector2f XYZToCylindrical(const nanogui::Vector3f &xyz);
nanogui::Vector2f XYZToCubeMap(const nanogui::Vector3f &xyz);

XYZ2UVFn *XYZToEnvMapUV(EEnvMappingUVMode mode);
