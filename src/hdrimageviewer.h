#include <utility>

//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>
#include <vector>
#include "fwd.h"
#include "common.h"
#include "glimage.h"
#include "imageshader.h"

using namespace nanogui;
using namespace Eigen;

/*!
 * @class 	HDRImageViewer hdrimageviewer.h
 * @brief	Widget used to manage and display multiple HDR images.
 */
class HDRImageViewer : public Widget
{
public:
	HDRImageViewer(Widget * parent, HDRViewScreen * screen);

	void set_current_image(ConstImagePtr cur)    {m_currentImage = std::move(cur);}
	void set_reference_image(ConstImagePtr ref)  {m_referenceImage = std::move(ref);}

	// overridden Widget virtual functions
	void draw(NVGcontext* ctx) override;
	bool mouse_drag_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers) override;
	bool mouse_motion_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers) override;
	bool scroll_event(const nanogui::Vector2i &p, const nanogui::Vector2f &rel) override;

	// Getters and setters

	float scale() const                                     { return m_zoom; }

	const nanogui::Vector2f& offset() const                 { return m_offset; }
	void set_offset(const nanogui::Vector2f& offset)         { m_offset = offset; }

	float zoom_sensitivity() const                           { return m_zoomSensitivity; }
	void set_zoom_sensitivity(float zoom_sensitivity)          { m_zoomSensitivity = zoom_sensitivity; }

	float grid_threshold() const                             { return m_gridThreshold; }
	void set_grid_threshold(float grid_threshold)              { m_gridThreshold = grid_threshold; }

	float pixel_info_threshold() const                        { return m_pixelInfoThreshold; }
	void set_pixel_info_threshold(float pixel_info_threshold)    { m_pixelInfoThreshold = pixel_info_threshold; }

	/// Function indicating whether the grid is currently visible.
	bool grid_visible() const;

	/// Function indicating whether the pixel information is currently visible.
	bool pixel_info_visible() const;

	/// Function indicating whether any of the overlays are visible.
	bool helpers_visible() const;


	// Image transformation functions.

	/// Calculates the image coordinates of the given pixel position on the widget.
	nanogui::Vector2f image_coordinate_at(const nanogui::Vector2f& position) const;

	/**
	 * Calculates the image coordinates of the given pixel position on the widget.
	 * If the position provided corresponds to a coordinate outside the range of
	 * the image, the coordinates are clamped to edges of the image.
	 */
	nanogui::Vector2f clamped_image_coordinate_at(const nanogui::Vector2f& position) const;

	/// Calculates the position inside the widget for the given image coordinate.
	nanogui::Vector2f position_for_coordinate(const nanogui::Vector2f& imageCoordinate) const;

	/// Calculates the position inside the widget for the given image coordinate.
	nanogui::Vector2f screen_position_for_coordinate(const nanogui::Vector2f& imageCoordinate) const;

	/**
	 * Modifies the internal state of the image viewer widget so that the pixel at the provided
	 * position on the widget has the specified image coordinate. Also clamps the values of offset
	 * to the sides of the widget.
	 */
	void set_image_coordinate_at(const nanogui::Vector2f& position, const nanogui::Vector2f& imageCoordinate);

	/// Centers the image without affecting the scaling factor.
	void center();

	/// Centers and scales the image so that it fits inside the widget.
	void fit();

	/// Moves the offset by the specified amount. Does bound checking.
	void move_offset(const nanogui::Vector2f& delta);

	/**
	 * Changes the scale factor by the provided amount modified by the zoom sensitivity member variable.
	 * The scaling occurs such that the image coordinate under the focused position remains in
	 * the same position before and after the scaling.
	 */
	void zoom_by(float amount, const nanogui::Vector2f &focusPosition);

	/// Zoom in to the next power of two
	void zoom_in();

	/// Zoom out to the previous power of two
	void zoom_out();


	float zoom_level() const     {return m_zoomLevel;}
	void set_zoom_level(float l);

	EChannel channel()          {return m_channel;}
	void set_channel(EChannel c) {m_channel = c;}

	EBlendMode blend_mode()      {return m_blendMode;}
	void set_blend_mode(EBlendMode b) {m_blendMode = b;}

	float gamma() const         {return m_gamma;}
	void set_gamma(float g)      {if (m_gamma != g) {m_gamma = g; m_gammaCallback(g);}}

	float exposure() const      {return m_exposure;}
	void set_exposure(float e)   {if (m_exposure != e) {m_exposure = e; m_exposureCallback(e);}}

	bool sRGB() const           {return m_sRGB;}
	void set_sRGB(bool b)        {m_sRGB = b; m_sRGBCallback(b);}

	bool dithering_on() const    {return m_dither;}
	void set_dithering(bool b)   {m_dither = b;}

	bool draw_grid_on() const     {return m_drawGrid;}
	void set_draw_grid(bool b)    {m_drawGrid = b;}

	bool draw_values_on() const   {return m_drawValues;}
	void set_draw_values(bool b)  {m_drawValues = b;}

	// Callback functions

	/// Callback executed whenever the gamma value has been changed, e.g. via @ref set_gamma
	const std::function<void(float)>& gamma_callback() const { return m_gammaCallback; }
	void set_gamma_callback(const std::function<void(float)> &callback) { m_gammaCallback = callback; }

	/// Callback executed whenever the exposure value has been changed, e.g. via @ref set_exposure
	const std::function<void(float)>& exposure_callback() const { return m_exposureCallback; }
	void set_exposure_callback(const std::function<void(float)> &callback) { m_exposureCallback = callback; }

	/// Callback executed whenever the sRGB setting has been changed, e.g. via @ref set_sRGB
	const std::function<void(bool)>& sRGB_callback() const { return m_sRGBCallback; }
	void set_sRGB_callback(const std::function<void(bool)> &callback) { m_sRGBCallback = callback; }

	/// Callback executed when the zoom level changes
	const std::function<void(float)>& zoom_callback() const { return m_zoomCallback; }
	void set_zoom_callback(const std::function<void(float)> &callback) { m_zoomCallback = callback; }

	/// Callback executed when mouse hovers over different parts of the image, provides pixel coordinates and values
	const std::function<void(const nanogui::Vector2i &, const Color4 &, const Color4 &)> pixel_hover_callback() const { return m_pixelHoverCallback; }
	void set_pixel_hover_callback(const std::function<void(const nanogui::Vector2i &, const Color4 &, const Color4 &)> &callback) { m_pixelHoverCallback = callback; }

private:
	nanogui::Vector2f position_f() const                            { return nanogui::Vector2f(m_pos); }
	nanogui::Vector2f size_f() const                                { return nanogui::Vector2f(m_size); }
	nanogui::Vector2f screen_size_f() const;

	nanogui::Vector2i imageSize(ConstImagePtr img) const           { return img ? img->size() : nanogui::Vector2i(0,0); }
	nanogui::Vector2f image_size_f(ConstImagePtr img) const          { return nanogui::Vector2f(imageSize(img)); }
	nanogui::Vector2f scaled_image_size_f(ConstImagePtr img) const    { return m_zoom * image_size_f(img); }

	// Helper drawing methods.
	void draw_widget_border(NVGcontext* ctx) const;
	void draw_image_border(NVGcontext* ctx) const;
	void draw_helpers(NVGcontext* ctx) const;
	void draw_pixel_grid(NVGcontext* ctx) const;
	void draw_pixel_info(NVGcontext *ctx) const;
	void image_position_and_scale(nanogui::Vector2f & position,
							   nanogui::Vector2f & scale,
	                           ConstImagePtr image);

	nanogui::Vector2f center_offset(ConstImagePtr img) const;

	ImageShader m_shader;

	HDRViewScreen * m_screen = nullptr;
	ConstImagePtr m_currentImage = nullptr;
	ConstImagePtr m_referenceImage = nullptr;
	float m_exposure = 0.f,
		  m_gamma = 2.2f;
	bool m_sRGB = true,
		 m_dither = true,
		 m_drawGrid = true,
		 m_drawValues = true;


	// Image display parameters.
	float m_zoom;                           ///< The scale/zoom of the image
	float m_zoomLevel;                      ///< The zoom level
	nanogui::Vector2f m_offset;             ///< The panning offset of the
	EChannel m_channel = EChannel::RGB;     ///< Which channel to display
	EBlendMode m_blendMode = EBlendMode::NORMAL_BLEND;     ///< How to blend the current and reference images

	// Fine-tuning parameters.
	float m_zoomSensitivity = 1.0717734625f;

	// Image info parameters.
	float m_gridThreshold = -1;
	float m_pixelInfoThreshold = -1;

	// various callback functions
	std::function<void(float)> m_exposureCallback;
	std::function<void(float)> m_gammaCallback;
	std::function<void(bool)> m_sRGBCallback;
	std::function<void(float)> m_zoomCallback;
	std::function<void(const nanogui::Vector2i &, const Color4 &, const Color4 &)> m_pixelHoverCallback;
};