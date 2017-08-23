/*!
    \file envmap.cpp
    \author Wojciech Jarosz
*/

#define _USE_MATH_DEFINES
#include <cmath>
#include "envmap.h"
#include "common.h"

using namespace Eigen;

Vector3f angularMapToXYZ(const Vector2f& UV)
{
    // image plane coordinates going from (-1,1) for x and y
    // with center of image being (0,0)
    Vector2f XY = 2*UV - Vector2f::Ones();

    // phi varies linearly with the radius from center
    float phi   = clamp(XY.norm() * M_PI, 0.0, M_PI);
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
    float phi   = 2*std::asin(clamp(XY.norm(), 0.0f, 1.0f));
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
    float theta = (UV(0)-0.25f)*2*M_PI;
    float phi   = UV(1)*M_PI;

    float sinPhi = std::sin(phi);
    return Vector3f(-sinPhi*std::cos(theta),
                     std::cos(phi),
                     sinPhi*std::sin(theta));
}

Vector3f cubeMapToXYZ(const Vector2f& UV)
{
    // This is assuming that the Cubemap is a vertical cross
    Vector3f xyz;
    float k, j;

    if (clamp(UV(0), (1.0f/3.0f), (2.0f/3.0f)) == UV(0))
    {
        j = clamp(UV(0), (1.0f/3.0f), (2.0f/3.0f));
        xyz(0) = (UV(0) - 0.5f) * 6.0f;
        if (clamp(UV(1), 0.0f, 0.25f) == UV(1))
        {
            xyz(1) = 1;
            xyz(2) = (UV(1) - 0.125f) * 8.0f;
        }
        else if (clamp(UV(1), 0.25f, 0.5f) == UV(1))
        {
            xyz(1) = (0.375f - UV(1)) * 8.0f;
            xyz(2) = 1;
        }
        else if (clamp(UV(1), 0.5f, 0.75f) == UV(1))
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
    else if (clamp(UV(0), 0.0f, (1.0f/3.0f)) == UV(0))
    {
        xyz(0) = -1;
        k = clamp(UV(1), 0.25f, 0.5f);
        j = clamp(UV(0), 0.0f, (1.0f/3.0f));
        xyz(1) = (0.375f - k) * 8.0f;
        xyz(2) = (j - (1.0f/6.0f)) * 6.0f;
    }
    else
    {
        xyz(0) = 1;
        k = clamp(UV(1), 0.25f, 0.5f);
        j = clamp(UV(0), (2.0f/3.0f), 1.0f);
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

    return Vector2f(mod(float(0.5f - theta*M_1_PI*0.5f)+0.25f, 1.0f), phi/M_PI);
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
