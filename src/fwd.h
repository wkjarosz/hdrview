//
// Created by Wojciech Jarosz on 9/4/17.
//

#pragma once

// forward declarations
class Color3;
class Color4;
class GLDitherTexture;
class ImageButton;
class ImageCommandUndo;
class FullImageUndo;
class LambdaUndo;
class CommandHistory;
class GLImage;
class HDRImage;
class HDRViewScreen;
class HDRImageManager;
class HDRImageViewer;
class HelpWindow;
class MultiGraph;
class EditImagePanel;
class HistogramPanel;
class ImageListPanel;
class Timer;
template<typename T> class Range;


namespace nanogui { class Widget; }
namespace nanogui { class Button; }
namespace nanogui { class CheckBox; }
namespace nanogui { class Label; }
namespace nanogui { class MessageDialog; }
namespace nanogui { class Slider; }
namespace nanogui { class VScrollPanel; }
namespace nanogui { class Window; }
namespace nanogui { template <typename Scalar> class FloatBox; }
namespace nanogui { class GLShader; }


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
	LUMINANCE,
	CIEL,
	CIEa,
	CIEb,
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