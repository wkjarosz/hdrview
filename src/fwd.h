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
class EditImagePanel;
class ImageListPanel;
class Timer;

class Brush;
class Tool;
class RectangularMarquee;
class HandTool;
class BrushTool;

template <typename T>
class Range;

using ConstHDRImagePtr = std::shared_ptr<const HDRImage>;
using HDRImagePtr      = std::shared_ptr<HDRImage>;

using ConstXPUImagePtr = std::shared_ptr<const XPUImage>;
using XPUImagePtr      = std::shared_ptr<XPUImage>;

NAMESPACE_BEGIN(nanogui)
class Widget;
class Button;
class Dropdown;
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

enum EColorSpace : int
{
    LinearSRGB_CS = 0,
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