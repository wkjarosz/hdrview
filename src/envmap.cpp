//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#if defined(_MSC_VER)
// Make MS cmath define M_PI
#define _USE_MATH_DEFINES
#endif

#include "envmap.h"
#include "common.h"
#include <cmath>

using namespace nanogui;
using std::string;
using std::vector;

Vector2f convertEnvMappingUV(EEnvMappingUVMode dst, EEnvMappingUVMode src, const Vector2f &srcUV)
{
    Vector2f uv;
    Vector3f xyz;

    switch (src)
    {
    case ANGULAR_MAP: xyz = angularMapToXYZ(srcUV); break;
    case MIRROR_BALL: xyz = mirrorBallToXYZ(srcUV); break;
    case LAT_LONG: xyz = latLongToXYZ(srcUV); break;
    case CYLINDRICAL: xyz = cylindricalToXYZ(srcUV); break;
    case CUBE_MAP: xyz = cubeMapToXYZ(srcUV); break;
    case EQUAL_AREA:
    default: xyz = equalAreaToXYZ(srcUV); break;
    }

    switch (dst)
    {
    case ANGULAR_MAP: uv = XYZToAngularMap(xyz); break;
    case MIRROR_BALL: uv = XYZToMirrorBall(xyz); break;
    case LAT_LONG: uv = XYZToLatLong(xyz); break;
    case CYLINDRICAL: uv = XYZToCylindrical(xyz); break;
    case CUBE_MAP: uv = XYZToCubeMap(xyz); break;
    case EQUAL_AREA:
    default: uv = XYZToEqualArea(xyz); break;
    }

    return uv;
}

const vector<string> &envMappingNames()
{
    static const vector<string> names = {"Angular map", "Mirror ball", "Longitude-latitude",
                                         "Cylindrical", "Cube map",    "Equal Area"};
    return names;
}

UV2XYZFn *envMapUVToXYZ(EEnvMappingUVMode mode)
{
    switch (mode)
    {
    case ANGULAR_MAP: return angularMapToXYZ;
    case MIRROR_BALL: return mirrorBallToXYZ;
    case LAT_LONG: return latLongToXYZ;
    case CYLINDRICAL: return cylindricalToXYZ;
    case CUBE_MAP: return cubeMapToXYZ;
    case EQUAL_AREA:
    default: return equalAreaToXYZ;
    }
}

XYZ2UVFn *XYZToEnvMapUV(EEnvMappingUVMode mode)
{
    switch (mode)
    {
    case ANGULAR_MAP: return XYZToAngularMap;
    case MIRROR_BALL: return XYZToMirrorBall;
    case LAT_LONG: return XYZToLatLong;
    case CYLINDRICAL: return XYZToCylindrical;
    case CUBE_MAP: return XYZToCubeMap;
    case EQUAL_AREA:
    default: return XYZToEqualArea;
    }
}

Vector3f angularMapToXYZ(const Vector2f &UV)
{
    // image plane coordinates going from (-1,1) for x and y
    // with center of image being (0,0)
    Vector2f XY = 2 * UV - Vector2f(1.f);

    // phi varies linearly with the radius from center
    float phi   = std::clamp<float>(norm(XY) * M_PI, 0.0, M_PI);
    float theta = std::atan2(XY[1], XY[0]);

    float sinPhi = std::sin(phi);
    return Vector3f(sinPhi * std::cos(theta), -sinPhi * std::sin(theta), std::cos(phi));
}

Vector3f mirrorBallToXYZ(const Vector2f &UV)
{
    // image plane coordinates going from (-1,1) for x and y
    // with center of image being (0,0)
    Vector2f XY = 2 * UV - Vector2f(1.f);

    // sin(phi) varies linearly with the radius from center
    float phi   = 2 * std::asin(std::clamp(norm(XY), 0.0f, 1.0f));
    float theta = std::atan2(XY[1], XY[0]);

    float sinPhi = std::sin(phi);
    return Vector3f(sinPhi * std::cos(theta), -sinPhi * std::sin(theta), std::cos(phi));
}

Vector3f latLongToXYZ(const Vector2f &UV)
{
    // theta varies linearly with U,
    // and phi varies linearly with V
    float theta = lerp<float>(1.5f * M_PI, -M_PI_2, UV[0]);
    float phi   = UV[1] * M_PI;

    float sinPhi = std::sin(phi);
    return Vector3f(sinPhi * std::cos(theta), std::cos(phi), sinPhi * std::sin(theta));
}

Vector3f cylindricalToXYZ(const Vector2f &UV)
{
    // theta varies linearly with U,
    // and y=cosPhi varies linearly with V
    float theta  = lerp<float>(1.5f * M_PI, -M_PI_2, UV[0]);
    float cosPhi = lerp<float>(1.f, -1.f, UV[1]);

    float sinPhi = std::sqrt(1.f - cosPhi * cosPhi);
    return Vector3f(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta));
}

Vector3f cubeMapToXYZ(const Vector2f &UV)
{
    // This is assuming that the Cubemap is a vertical cross
    Vector3f xyz;
    float    k, j;

    if (std::clamp(UV[0], (1.0f / 3.0f), (2.0f / 3.0f)) == UV[0])
    {
        j      = std::clamp(UV[0], (1.0f / 3.0f), (2.0f / 3.0f));
        xyz[0] = (UV[0] - 0.5f) * 6.0f;
        if (std::clamp(UV[1], 0.0f, 0.25f) == UV[1])
        {
            xyz[1] = 1;
            xyz[2] = (UV[1] - 0.125f) * 8.0f;
        }
        else if (std::clamp(UV[1], 0.25f, 0.5f) == UV[1])
        {
            xyz[1] = (0.375f - UV[1]) * 8.0f;
            xyz[2] = 1;
        }
        else if (std::clamp(UV[1], 0.5f, 0.75f) == UV[1])
        {
            xyz[1] = -1;
            xyz[2] = (0.625f - UV[1]) * 8.0f;
        }
        else
        {
            xyz[1] = (UV[1] - 0.875f) * 8.0f;
            xyz[2] = -1;
        }
    }
    else if (std::clamp(UV[0], 0.0f, (1.0f / 3.0f)) == UV[0])
    {
        xyz[0] = -1;
        k      = std::clamp(UV[1], 0.25f, 0.5f);
        j      = std::clamp(UV[0], 0.0f, (1.0f / 3.0f));
        xyz[1] = (0.375f - k) * 8.0f;
        xyz[2] = (j - (1.0f / 6.0f)) * 6.0f;
    }
    else
    {
        xyz[0] = 1;
        k      = std::clamp(UV[1], 0.25f, 0.5f);
        j      = std::clamp(UV[0], (2.0f / 3.0f), 1.0f);
        xyz[1] = (0.375f - k) * 8.0f;
        xyz[2] = ((5.0f / 6.0f) - j) * 6.0f;
    }
    return normalize(xyz);
}

////////////////////////////////

Vector2f XYZToAngularMap(const Vector3f &xyz)
{
    float phi   = std::acos(xyz[2]);
    float theta = std::atan2(xyz[1], xyz[0]);

    float U = (phi / M_PI) * std::cos(theta);
    float V = -(phi / M_PI) * std::sin(theta);

    return Vector2f(0.5f * (U + 1.0f), 0.5f * (V + 1.0f));
}

Vector2f XYZToMirrorBall(const Vector3f &xyz)
{
    float phi   = std::acos(xyz[2]);
    float theta = std::atan2(xyz[1], xyz[0]);

    float sinPhi2 = std::sin(phi / 2.0f);
    return Vector2f(0.5f * (sinPhi2 * std::cos(theta) + 1.0f), 0.5f * (-sinPhi2 * std::sin(theta) + 1.0f));
}

Vector2f XYZToLatLong(const Vector3f &xyz)
{
    // theta varies linearly with U,
    // and phi varies linearly with V
    float phi   = std::acos(xyz[1]);
    float theta = std::atan2(xyz[2], xyz[0]);

    return Vector2f(mod(lerpFactor<float>(1.5f * M_PI, -M_PI_2, theta), 1.0f), phi / M_PI);
}

Vector2f XYZToCylindrical(const Vector3f &xyz)
{
    // U varies linearly with theta,
    // and V varies linearly with y=cosPhi
    float theta = std::atan2(xyz[2], xyz[0]);
    return Vector2f(mod(lerpFactor<float>(1.5f * M_PI, -M_PI_2, theta), 1.0f), lerpFactor(1.f, -1.f, xyz[1]));
}

Vector2f XYZToCubeMap(const Vector3f &xyz)
{
    // Again, the CubeMap is a vertical cross
    float    U, V;
    float    l;   // infinite-norm of xyz
    int      flg; // Marker of which window we're in
    Vector3f temp;

    // Make sure that the infinite norm of xyz == 1;
    // flg determines which side we're looking at.
    l   = fabs(xyz[0]);
    flg = (int)sign(xyz[0]);
    if (fabs(xyz[1]) > l)
    {
        l   = fabs(xyz[1]);
        flg = (int)sign(xyz[1]) * 2;
    }
    if (fabs(xyz[2]) > l)
    {
        l   = fabs(xyz[2]);
        flg = (int)sign(xyz[2]) * 3;
    }
    l    = 1 / l;
    temp = l * xyz;

    if (flg == 3)
    {
        U = temp[0] / 6.0f + 0.5f;
        V = -temp[1] / 8.0f + 0.375f;
    }
    else if (flg == -1)
    {
        U = temp[2] / 6.0f + (1.0f / 6.0f); // stupid type promotion oddity
        V = -temp[1] / 8.0f + 0.375f;       // with Viz Studio so this explicit
    }                                       // float notation crap has to be done.
    else if (flg == 1)
    {
        U = -temp[2] / 6.0f + (5.0f / 6.0f); // I actually had a 180-degree out of
        V = -temp[1] / 8.0f + 0.375f;        // phase thing going w/o the explicit
    }                                        // float notation and parenthesis.
    else if (flg == 2)
    {
        U = temp[0] / 6.0f + 0.5f;
        V = temp[2] / 8.0f + 0.125f;
    }
    else if (flg == -2)
    {
        U = temp[0] / 6.0f + 0.5f;
        V = -temp[2] / 8.0f + 0.625f;
    }
    else
    {
        U = temp[0] / 6.0f + 0.5f;
        V = temp[1] / 8.0f + 0.875f;
    }

    return Vector2f(U, V);
}

//
// The following functions are adapted from PBRTv4, which is itself via the source code from:
//      Clarberg: Fast Equal-Area Mapping of the (Hemi)Sphere using SIMD
//
Vector3f equalAreaToXYZ(const Vector2f &p)
{
    // Transform _p_ to $[-1,1]^2$ and compute absolute values
    float u = 2 * p.x() - 1, v = 2 * p.y() - 1;
    float up = std::abs(u), vp = std::abs(v);

    // Compute radius _r_ as signed distance from diagonal
    float signedDistance = 1 - (up + vp);
    float d              = std::abs(signedDistance);
    float r              = 1 - d;

    // Compute angle $\phi$ for square to sphere mapping
    float phi = (r == 0 ? 1 : (vp - up) / r + 1) * M_PI / 4;

    // Find $z$ coordinate for spherical direction
    float z = std::copysign(1 - sqr(r), signedDistance);

    // Compute $\cos \phi$ and $\sin \phi$ for original quadrant and return vector
    float cosPhi = std::copysign(std::cos(phi), u);
    float sinPhi = std::copysign(std::sin(phi), v);
    return Vector3f(cosPhi * r * std::sqrt(std::max(0.f, 2 - sqr(r))), z,
                    -sinPhi * r * std::sqrt(std::max(0.f, 2 - sqr(r))));
}

template <typename T>
inline T FMA(T a, T b, T c)
{
    return a * b + c;
}

template <typename Float, typename C>
inline constexpr Float EvaluatePolynomial(Float t, C c)
{
    return c;
}

template <typename Float, typename C, typename... Args>
inline constexpr Float EvaluatePolynomial(Float t, C c, Args... cRemaining)
{
    return FMA(t, EvaluatePolynomial(t, cRemaining...), c);
}

Vector2f XYZToEqualArea(const Vector3f &d_)
{
    Vector3f d(d_);
    // 90 degree rotation compared to code in PBRT
    d[2] *= -1.f;
    std::swap(d[1], d[2]);

    float x = std::abs(d.x()), y = std::abs(d.y()), z = std::abs(d.z());

    // Compute the radius r
    float r = std::sqrt(std::max(0.f, 1 - z)); // r = sqrt(1-|z|)

    // Compute the argument to atan (detect a=0 to avoid div-by-zero)
    float a = std::max(x, y), b = std::min(x, y);
    b = a == 0 ? 0 : b / a;

    // Polynomial approximation of atan(x)*2/pi, x=b
    // Coefficients for 6th degree minimax approximation of atan(x)*2/pi,
    // x=[0,1].
    const float t1  = 0.406758566246788489601959989e-5;
    const float t2  = 0.636226545274016134946890922156;
    const float t3  = 0.61572017898280213493197203466e-2;
    const float t4  = -0.247333733281268944196501420480;
    const float t5  = 0.881770664775316294736387951347e-1;
    const float t6  = 0.419038818029165735901852432784e-1;
    const float t7  = -0.251390972343483509333252996350e-1;
    float       phi = EvaluatePolynomial(b, t1, t2, t3, t4, t5, t6, t7);

    // Extend phi if the input is in the range 45-90 degrees (u<v)
    if (x < y)
        phi = 1 - phi;

    // Find (u,v) based on (r,phi)
    float v = phi * r;
    float u = r - v;

    if (d.z() < 0)
    {
        // southern hemisphere -> mirror u,v
        std::swap(u, v);
        u = 1 - u;
        v = 1 - v;
    }

    // Move (u,v) to the correct quadrant based on the signs of (x,y)
    u = std::copysign(u, d.x());
    v = std::copysign(v, d.y());

    // Transform (u,v) from [-1,1] to [0,1]
    return Vector2f(0.5f * (u + 1), 0.5f * (v + 1));
}
