//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <Eigen/Core>

using UV2XYZFn = Eigen::Vector3f(const Eigen::Vector2f &);
using XYZ2UVFn = Eigen::Vector2f(const Eigen::Vector3f &);

// functions that convert from UV image plane coordinates to
// XYZ world coordinates for the various light probe representations
Eigen::Vector3f angularMapToXYZ(const Eigen::Vector2f & uv);
Eigen::Vector3f mirrorBallToXYZ(const Eigen::Vector2f & uv);
Eigen::Vector3f latLongToXYZ(const Eigen::Vector2f & uv);
Eigen::Vector3f cubeMapToXYZ(const Eigen::Vector2f & uv);

// functions that convert from XYZ world coordinates to
// UV image plane coordinates for the various light probe representations
Eigen::Vector2f XYZToAngularMap(const Eigen::Vector3f & xyz);
Eigen::Vector2f XYZToMirrorBall(const Eigen::Vector3f & xyz);
Eigen::Vector2f XYZToLatLong(const Eigen::Vector3f & xyz);
Eigen::Vector2f XYZToCubeMap(const Eigen::Vector3f & xyz);
