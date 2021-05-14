//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "common.h"
#include "envmap.h"

using namespace Eigen;
using std::vector;
using std::string;

Vector2f convertEnvMappingUV(EEnvMappingUVMode dst, EEnvMappingUVMode src, const Vector2f & srcUV)
{
    Vector2f uv;
    Vector3f xyz;

    switch (src)
    {
        case ANGULAR_MAP:
            xyz = angularMapToXYZ(srcUV);
            break;
        case MIRROR_BALL:
            xyz = mirrorBallToXYZ(srcUV);
            break;
        case LAT_LONG:
            xyz = latLongToXYZ(srcUV);
            break;
	    case CYLINDRICAL:
		    xyz = cylindricalToXYZ(srcUV);
		    break;
        case CUBE_MAP:
            xyz = cubeMapToXYZ(srcUV);
            break;
    }

    switch (dst)
    {
        case ANGULAR_MAP:
            uv = XYZToAngularMap(xyz);
            break;
        case MIRROR_BALL:
            uv = XYZToMirrorBall(xyz);
            break;
        case LAT_LONG:
            uv = XYZToLatLong(xyz);
            break;
	    case CYLINDRICAL:
		    uv = XYZToCylindrical(xyz);
		    break;
        case CUBE_MAP:
            uv = XYZToCubeMap(xyz);
            break;
    }

    return uv;
}


const vector<string> & envMappingNames()
{
    static const vector<string> names =
        {
            "Angular map",
            "Mirror ball",
            "Longitude-latitude",
            "Cylindrical",
            "Cube map"
        };
    return names;
}

UV2XYZFn * envMapUVToXYZ(EEnvMappingUVMode mode)
{
    switch (mode)
    {
        case ANGULAR_MAP:
            return angularMapToXYZ;
        case MIRROR_BALL:
            return mirrorBallToXYZ;
        case LAT_LONG:
            return latLongToXYZ;
	    case CYLINDRICAL:
		    return cylindricalToXYZ;
        case CUBE_MAP:
            return cubeMapToXYZ;
    }
}

XYZ2UVFn * XYZToEnvMapUV(EEnvMappingUVMode mode)
{
    switch (mode)
    {
        case ANGULAR_MAP:
            return XYZToAngularMap;
        case MIRROR_BALL:
            return XYZToMirrorBall;
        case LAT_LONG:
            return XYZToLatLong;
	    case CYLINDRICAL:
		    return XYZToCylindrical;
        case CUBE_MAP:
            return XYZToCubeMap;
    }
}

Vector3f angularMapToXYZ(const Vector2f& UV)
{
    // image plane coordinates going from (-1,1) for x and y
    // with center of image being (0,0)
    Vector2f XY = 2*UV - Vector2f::Ones();

    // phi varies linearly with the radius from center
    float phi   = ::clamp(XY.norm() * M_PI, 0.0, M_PI);
    float theta = std::atan2(XY(1),XY(0));

    float sinPhi = std::sin(phi);
    return Vector3f( sinPhi*std::cos(theta),
                    -sinPhi*std::sin(theta),
                     std::cos(phi));
}

Vector3f mirrorBallToXYZ(const Vector2f& UV)
{
    // image plane coordinates going from (-1,1) for x and y
    // with center of image being (0,0)
    Vector2f XY = 2*UV - Vector2f::Ones();

    // sin(phi) varies linearly with the radius from center
    float phi   = 2*std::asin(::clamp(XY.norm(), 0.0f, 1.0f));
    float theta = std::atan2(XY(1),XY(0));

    float sinPhi = std::sin(phi);
    return Vector3f( sinPhi*std::cos(theta),
                    -sinPhi*std::sin(theta),
                     std::cos(phi));
}

Vector3f latLongToXYZ(const Vector2f& UV)
{
    // theta varies linearly with U,
    // and phi varies linearly with V
    float theta = lerp<float>(1.5f*M_PI, -M_PI_2, UV(0));
    float phi   = UV(1)*M_PI;

    float sinPhi = std::sin(phi);
    return Vector3f( sinPhi*std::cos(theta),
                     std::cos(phi),
                     sinPhi*std::sin(theta));
}


Vector3f cylindricalToXYZ(const Vector2f& UV)
{
    // theta varies linearly with U,
    // and y=cosPhi varies linearly with V
    float theta  = lerp<float>(1.5f*M_PI, -M_PI_2, UV(0));
    float cosPhi = lerp<float>(1.f, -1.f, UV(1));

    float sinPhi = std::sqrt(1.f-cosPhi*cosPhi);
    return Vector3f( sinPhi*std::cos(theta),
                     cosPhi,
                     sinPhi*std::sin(theta));
}

Vector3f cubeMapToXYZ(const Vector2f& UV)
{
    // This is assuming that the Cubemap is a vertical cross
    Vector3f xyz;
    float k, j;

    if (::clamp(UV(0), (1.0f/3.0f), (2.0f/3.0f)) == UV(0))
    {
        j = ::clamp(UV(0), (1.0f/3.0f), (2.0f/3.0f));
        xyz(0) = (UV(0) - 0.5f) * 6.0f;
        if (::clamp(UV(1), 0.0f, 0.25f) == UV(1))
        {
            xyz(1) = 1;
            xyz(2) = (UV(1) - 0.125f) * 8.0f;
        }
        else if (::clamp(UV(1), 0.25f, 0.5f) == UV(1))
        {
            xyz(1) = (0.375f - UV(1)) * 8.0f;
            xyz(2) = 1;
        }
        else if (::clamp(UV(1), 0.5f, 0.75f) == UV(1))
        {
            xyz(1) = -1;
            xyz(2) = (0.625f - UV(1)) * 8.0f;
        }
        else
        {
            xyz(1) = (UV(1) - 0.875f) * 8.0f;
            xyz(2) = -1;
        }
    }
    else if (::clamp(UV(0), 0.0f, (1.0f/3.0f)) == UV(0))
    {
        xyz(0) = -1;
        k = ::clamp(UV(1), 0.25f, 0.5f);
        j = ::clamp(UV(0), 0.0f, (1.0f/3.0f));
        xyz(1) = (0.375f - k) * 8.0f;
        xyz(2) = (j - (1.0f/6.0f)) * 6.0f;
    }
    else
    {
        xyz(0) = 1;
        k = ::clamp(UV(1), 0.25f, 0.5f);
        j = ::clamp(UV(0), (2.0f/3.0f), 1.0f);
        xyz(1) = (0.375f - k) * 8.0f;
        xyz(2) = ((5.0f/6.0f) - j) * 6.0f;
    }
    return xyz.normalized();
}

////////////////////////////////

Vector2f XYZToAngularMap(const Vector3f& xyz)
{
    float phi   = std::acos(xyz(2));
    float theta = std::atan2(xyz(1), xyz(0));

    float U =  (phi/M_PI)*std::cos(theta);
    float V = -(phi/M_PI)*std::sin(theta);

    return Vector2f(0.5f*(U + 1.0f), 0.5f*(V + 1.0f));
}

Vector2f XYZToMirrorBall(const Vector3f& xyz)
{
    float phi   = std::acos(xyz(2));
    float theta = std::atan2(xyz(1),xyz(0));

    float sinPhi2 = std::sin(phi/2.0f);
    return Vector2f(0.5f*( sinPhi2*std::cos(theta) + 1.0f),
                    0.5f*(-sinPhi2*std::sin(theta) + 1.0f));
}


Vector2f XYZToLatLong(const Vector3f& xyz)
{
    // theta varies linearly with U,
    // and phi varies linearly with V
    float phi   = std::acos(xyz(1));
    float theta = std::atan2(xyz(2), xyz(0));

    return Vector2f(mod(lerpFactor<float>(1.5f*M_PI, -M_PI_2, theta), 1.0f),
                    phi/M_PI);
}


Vector2f XYZToCylindrical(const Vector3f& xyz)
{
    // U varies linearly with theta,
    // and V varies linearly with y=cosPhi
    float theta = std::atan2(xyz(2), xyz(0));
    return Vector2f(mod(lerpFactor<float>(1.5f*M_PI, -M_PI_2, theta), 1.0f),
                    lerpFactor(1.f, -1.f, xyz(1)));
}


Vector2f XYZToCubeMap(const Vector3f& xyz)
{
    // Again, the CubeMap is a vertical cross
    float U, V;
    float l;            // infinite-norm of xyz
    int flg;            // Marker of which window we're in
    Vector3f temp;

    // Make sure that the infinite norm of xyz == 1;
    // flg determines which side we're looking at.
    l = fabs(xyz(0));
    flg = (int)sign(xyz(0));
    if (fabs(xyz(1)) > l)
    {
        l = fabs(xyz(1));
        flg = (int)sign(xyz(1)) * 2;
    }
    if (fabs(xyz(2)) > l)
    {
        l = fabs(xyz(2));
        flg = (int)sign(xyz(2)) * 3;
    }
    l = 1 / l;
    temp = l * xyz;

    if (flg == 3)
    {
        U = temp(0) / 6.0f + 0.5f;
        V = -temp(1) / 8.0f + 0.375f;
    }
    else if (flg == -1)
    {
        U = temp(2) / 6.0f + (1.0f/6.0f);   // stupid type promotion oddity
        V = -temp(1) / 8.0f + 0.375f;       // with Viz Studio so this explicit
    }                                       // float notation crap has to be done.
    else if (flg == 1)
    {
        U = -temp(2) / 6.0f + (5.0f/6.0f);  // I actually had a 180-degree out of
        V = -temp(1) / 8.0f + 0.375f;       // phase thing going w/o the explicit
    }                                       // float notation and parenthesis.
    else if (flg == 2)
    {
        U = temp(0) / 6.0f + 0.5f;
        V = temp(2) / 8.0f + 0.125f;
    }
    else if (flg == -2)
    {
        U = temp(0) / 6.0f + 0.5f;
        V = -temp(2) / 8.0f + 0.625f;
    }
    else
    {
        U = temp(0) / 6.0f + 0.5f;
        V = temp(1) / 8.0f + 0.875f;
    }

    return Vector2f(U, V);
}
