//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

// forward declarations
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

namespace spdlog { class logger; }


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