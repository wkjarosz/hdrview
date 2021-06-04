//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/vector.h>




inline nanogui::Vector3f operator*(const nanogui::Matrix3f & M, const nanogui::Vector3f & v)
{
	nanogui::Vector3f result;
	result[0] = M.m[0][0] * v[0] + M.m[0][1] * v[1] + M.m[0][2] * v[2];
	result[1] = M.m[1][0] * v[0] + M.m[1][1] * v[1] + M.m[1][2] * v[2];
	result[2] = M.m[2][0] * v[0] + M.m[2][1] * v[1] + M.m[2][2] * v[2];
	return result;
}


// forward declarations
template <typename Vec, typename Value = typename Vec::Value, size_t Dims = Vec::Size> class Box;
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
class HelpWindow;
class EditImagePanel;
class ImageListPanel;
class Timer;
template<typename T> class Range;

namespace nanogui
{
class Widget;
class Button;
class CheckBox;
class Label;
class MessageDialog;
class Slider;
class VScrollPanel;
class Window;
template <typename Scalar> class FloatBox;

class HDRColorPicker;
class ColorWheel2;
class ColorSlider;
class MultiGraph;
class Well;
class ImageButton;
class HSLGradient;
}

// define some common types
using Box2i     = Box<nanogui::Vector2i>;


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