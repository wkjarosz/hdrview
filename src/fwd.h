//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <memory>
#include <nanogui/vector.h>
#include <nlohmann/json_fwd.hpp>

// forward declarations
template <typename Vec, typename Value = typename Vec::Value, size_t Dims = Vec::Size>
class Box;
class Color3;
class Color4;
class ImageCommandUndo;
class FullImageUndo;
class LambdaUndo;
class CommandHistory;
class XPUImage;
class HDRImage;
class HDRViewScreen;
class HDRImageView;
class ImageListPanel;
class Timer;

class Tool;
class Brush;
class RectangularMarquee;
class HandTool;
class BrushTool;
class EraserTool;
class CloneStampTool;
class Eyedropper;
class Ruler;
class LineTool;

template <typename T>
class Range;

using ConstHDRImagePtr = std::shared_ptr<const HDRImage>;
using HDRImagePtr      = std::shared_ptr<HDRImage>;

using ConstXPUImagePtr = std::shared_ptr<const XPUImage>;
using XPUImagePtr      = std::shared_ptr<XPUImage>;

NAMESPACE_BEGIN(nanogui)
class Widget;
class Button;
class MenuBar;
class Dropdown;
class MenuItem;
class Separator;
class PopupMenu;
class PopupWrapper;
class CheckBox;
class Label;
class Slider;
class VScrollPanel;
class Window;
class HelpWindow;
class Dialog;
class SimpleDialog;
template <typename Scalar>
class FloatBox;

class HDRColorPicker;
class DualHDRColorPicker;
class ColorWheel2;
class ColorSlider;
class MultiGraph;
class Well;
class ImageButton;
class HSLGradient;
NAMESPACE_END(nanogui)

// define some common types
using Box2i = Box<nanogui::Vector2i>;

enum ETool : int
{
    Tool_None = 0,
    Tool_Rectangular_Marquee,
    Tool_Brush,
    Tool_Eraser,
    Tool_Clone_Stamp,
    Tool_Eyedropper,
    Tool_Ruler,
    Tool_Line,
    Tool_Gradient,
    Tool_Num_Tools
};

enum EColorSpace : int
{
    LinearSRGB_CS = 0,
    LinearSGray_CS,
    LinearAdobeRGB_CS,
    CIEXYZ_CS,
    CIELab_CS,
    CIELuv_CS,
    CIExyY_CS,
    HLS_CS,
    HSV_CS
};

enum EChannel : int
{
    RGB = 0,
    RED,
    GREEN,
    BLUE,
    ALPHA,
    LUMINANCE,
    GRAY,
    CIE_L,
    CIE_a,
    CIE_b,
    CIE_CHROMATICITY,
    FALSE_COLOR,
    POSITIVE_NEGATIVE,

    NUM_CHANNELS
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