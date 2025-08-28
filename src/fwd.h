//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

// #define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

#include "linalg.h" // IWYU pragma: export
#include <memory>

// Shortname for the linalg namespace
namespace la = linalg;
using namespace la::aliases;

// define extra conversion here before including imgui
#define IM_VEC2_CLASS_EXTRA                                                                                            \
    constexpr ImVec2(const float2 &f) : x(f.x), y(f.y) {}                                                              \
    operator float2() const { return float2(x, y); }                                                                   \
    constexpr ImVec2(const int2 &i) : x(i.x), y(i.y) {}                                                                \
    operator int2() const { return int2((int)x, (int)y); }

#define IM_VEC4_CLASS_EXTRA                                                                                            \
    constexpr ImVec4(const float4 &f) : x(f.x), y(f.y), z(f.z), w(f.w) {}                                              \
    operator float4() const { return float4(x, y, z, w); }

// forward declarations
template <typename Vec_, typename Value_ = typename la::scalar_t<Vec_>,
          size_t Dims_ = la::detail::apply<la::detail::op_pos, void, Vec_>::size>
class Box;
using Color3 = float3;
using Color4 = float4;

// define some common types
using Box1f = Box<float1>;
using Box1d = Box<double1>;
using Box1i = Box<int1>;

using Box2f = Box<float2>;
using Box2d = Box<double2>;
using Box2i = Box<int2>;

using Box3f = Box<float3>;
using Box3d = Box<double3>;
using Box3i = Box<int3>;

using Box4f = Box<float4>;
using Box4d = Box<double4>;
using Box4i = Box<int4>;

class Shader;
class Texture;
struct Image;
class Texture;
class Timer;

using ConstImagePtr = std::shared_ptr<const Image>;
using ImagePtr      = std::shared_ptr<Image>;

using Channels = int;
enum Channels_ : Channels
{
    Channels_RGBA = 0,
    Channels_RGB,
    Channels_Red,
    Channels_Green,
    Channels_Blue,
    Channels_Alpha,
    Channels_Y,

    Channels_COUNT
};

using Tonemap = int;
enum Tonemap_ : Tonemap
{
    Tonemap_Gamma = 0,
    Tonemap_FalseColor,
    Tonemap_PositiveNegative,

    Tonemap_COUNT
};

using BlendMode = int;
enum BlendMode_ : BlendMode
{
    BlendMode_Normal = 0,
    BlendMode_Multiply,
    BlendMode_Divide,
    BlendMode_Add,
    BlendMode_Average,
    BlendMode_Subtract,
    BlendMode_Relative_Subtract,
    BlendMode_Difference,
    BlendMode_Relative_Difference,

    BlendMode_COUNT
};

using BackgroundMode = int;
enum BackgroundMode_ : BackgroundMode
{
    BGMode_Black = 0,
    BGMode_White,
    BGMode_Dark_Checker,
    BGMode_Light_Checker,
    BGMode_Custom_Color,

    BGMode_COUNT
};

using Direction = int;
enum Direction_ : Direction
{
    Direction_Forward = 0,
    Direction_Backward,
};

using AxisScale = int;
enum AxisScale_ : AxisScale
{
    AxisScale_Linear = 0,
    AxisScale_SRGB,
    AxisScale_Asinh,
    AxisScale_SymLog,

    AxisScale_COUNT
};

using Target = int;
enum Target_ : Target
{
    Target_Primary   = 0,
    Target_Secondary = 1,

    Target_COUNT
};

inline const char *target_name(Target_ t)
{
    switch (t)
    {
    case Target_Primary: return "primary";
    case Target_Secondary: return "secondary";
    default: return "unknown";
    }
}

using MouseMode = int;
enum MouseMode_ : MouseMode
{
    MouseMode_PanZoom = 0,
    MouseMode_ColorInspector,
    MouseMode_RectangularSelection,

    MouseMode_COUNT
};
