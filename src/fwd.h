//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

// #define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

#include "linalg.h"
#include <memory>
using namespace linalg::aliases;

// define extra conversion here before including imgui
#define IM_VEC2_CLASS_EXTRA                                                                                            \
    constexpr ImVec2(const float2 &f) : x(f.x), y(f.y) {}                                                              \
    operator float2() const { return float2(x, y); }                                                                   \
    constexpr ImVec2(const int2 &i) : x(i.x), y(i.y) {}                                                                \
    operator int2() const { return int2((int)x, (int)y); }

#define IM_VEC4_CLASS_EXTRA                                                                                            \
    constexpr ImVec4(const float4 &f) : x(f.x), y(f.y), z(f.z), w(f.w) {}                                              \
    operator float4() const { return float4(x, y, z, w); }

// Shortname for the linalg namespace
namespace la = linalg;

// forward declarations
template <typename Vec_, typename Value_ = typename linalg::scalar_t<Vec_>,
          size_t Dims_ = linalg::detail::apply<linalg::detail::op_pos, void, Vec_>::size>
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

enum EChannel : int
{
    RGB = 0,
    RED,
    GREEN,
    BLUE,
    ALPHA,
    Y,

    NUM_CHANNELS
};

using Tonemap_ = int;
enum Tonemap : Tonemap_
{
    Tonemap_Gamma = 0,
    Tonemap_FalseColor,
    Tonemap_PositiveNegative,

    Tonemap_COUNT
};

enum EBlendMode : int
{
    NORMAL_BLEND = 0,
    MULTIPLY_BLEND,
    DIVIDE_BLEND,
    ADD_BLEND,
    AVERAGE_BLEND,
    SUBTRACT_BLEND,
    DIFFERENCE_BLEND,
    RELATIVE_DIFFERENCE_BLEND,

    NUM_BLEND_MODES
};

enum EBGMode : int
{
    BG_BLACK = 0,
    BG_WHITE,
    BG_DARK_CHECKER,
    BG_LIGHT_CHECKER,
    BG_CUSTOM_COLOR,

    NUM_BG_MODES
};

enum EDirection
{
    Forward,
    Backward,
};

using Orientation_ = int;
enum Orientation : Orientation_
{
    None        = 0,
    TopLeft     = 1,
    TopRight    = 2,
    BottomRight = 3,
    BottomLeft  = 4,
    LeftTop     = 5,
    RightTop    = 6,
    RightBottom = 7,
    LeftBottom  = 8,
};

using AxisScale_ = int;
enum AxisScale : AxisScale_
{
    AxisScale_Linear = 0,
    AxisScale_SRGB,
    AxisScale_Asinh,
    AxisScale_SymLog,

    AxisScale_COUNT
};

using Target_ = int;
enum Target : Target_
{
    Target_Primary   = 0,
    Target_Secondary = 1,

    Target_COUNT
};

inline const char *target_name(Target t)
{
    switch (t)
    {
    case Target_Primary: return "primary";
    case Target_Secondary: return "secondary";
    default: return "unknown";
    }
}

using MouseMode_ = int;
enum MouseMode : MouseMode_
{
    MouseMode_PanZoom = 0,
    MouseMode_ColorInspector,
    MouseMode_RectangularSelection,

    MouseMode_COUNT
};
