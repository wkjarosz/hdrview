/*!
    \file envmap.h
    \brief Contains conversions between the various envmap parametrizations
    \author Wojciech Jarosz
*/

#pragma once

#include <Eigen/Core>

typedef Eigen::Vector3f (*UV2XYZFn)(const Eigen::Vector2f &);
typedef Eigen::Vector2f (*XYZ2UVFn)(const Eigen::Vector3f &);

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
