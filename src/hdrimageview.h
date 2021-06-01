//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/canvas.h>
#include <nanogui/texture.h>
#include "xpuimage.h"
#include "fwd.h"


using namespace nanogui;

/*!
 * @class 	HDRImageView hdrimageview.h
 * @brief	Widget used to manage and display multiple HDR images.
 */
class HDRImageView : public Canvas {
public:
    using TextureRef = ref<Texture>;
    using PixelCallback = std::function<void(const Vector2i &, char **, size_t)>;
	using FloatCallback = std::function<void(float)>;
	using BoolCallback = std::function<void(bool)>;

    /// Initialize the widget
    HDRImageView(Widget *parent);


    // Widget implementation
    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) override;
	virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    virtual bool scroll_event(const Vector2i &p, const Vector2f &rel) override;
    virtual void draw(NVGcontext *ctx) override;
    virtual void draw_contents() override;


	// Getters and setters

	void set_current_image(ConstImagePtr cur);
	void set_reference_image(ConstImagePtr ref);

    /// Return the pixel offset of the zoomed image rectangle
    Vector2f offset() const 						  		  { return m_offset; }
    /// Set the pixel offset of the zoomed image rectangle
    void set_offset(const Vector2f &offset) 		  		  { m_offset = offset; }

	float zoom_sensitivity() const                            { return m_zoom_sensitivity; }
	void set_zoom_sensitivity(float zoom_sensitivity)         { m_zoom_sensitivity = zoom_sensitivity; }

	float grid_threshold() const                              { return m_grid_threshold; }
	void set_grid_threshold(float grid_threshold)             { m_grid_threshold = grid_threshold; }

	float pixel_info_threshold() const                        { return m_pixel_info_threshold; }
	void set_pixel_info_threshold(float pixel_info_threshold) { m_pixel_info_threshold = pixel_info_threshold; }


	// Image transformation functions.

	/// Calculates the image coordinates of the given pixel position on the widget.
	Vector2f image_coordinate_at(const Vector2f& position) const;

	/// Calculates the position inside the widget for the given image coordinate.
	Vector2f position_for_coordinate(const Vector2f& imageCoordinate) const;

	/// Calculates the position inside the widget for the given image coordinate.
	Vector2f screen_position_for_coordinate(const Vector2f& imageCoordinate) const;

	/**
	 * Modifies the internal state of the image viewer widget so that the pixel at the provided
	 * position on the widget has the specified image coordinate. Also clamps the values of offset
	 * to the sides of the widget.
	 */
	void set_image_coordinate_at(const Vector2f& position, const Vector2f& imageCoordinate);

	/// Centers the image without affecting the scaling factor.
	void center();

	/// Centers and scales the image so that it fits inside the widget.
	void fit();

	/**
	 * Changes the scale factor by the provided amount modified by the zoom sensitivity member variable.
	 * The scaling occurs such that the image coordinate under the focused position remains in
	 * the same position before and after the scaling.
	 */
	void zoom_by(float amount, const Vector2f &focusPosition);

	/// Zoom in to the next power of two
	void zoom_in();

	/// Zoom out to the previous power of two
	void zoom_out();


	float zoom_level() const     		{return m_zoom_level;}
	void set_zoom_level(float l);


	EChannel channel()          		{return m_channel;}
	void set_channel(EChannel c) 		{m_channel = c;}

	EBlendMode blend_mode()      		{return m_blend_mode;}
	void set_blend_mode(EBlendMode b) 	{m_blend_mode = b;}


	float gamma() const         		{return m_gamma;}
	void set_gamma(float g)      		{if (m_gamma != g) {m_gamma = g; m_gamma_callback(g);}}

	float exposure() const      		{return m_exposure;}
	void set_exposure(float e)   		{if (m_exposure != e) {m_exposure = e; m_exposure_callback(e);}}

	bool sRGB() const           		{return m_sRGB;}
	void set_sRGB(bool b)        		{m_sRGB = b; m_sRGB_callback(b);}

	bool dithering_on() const    		{return m_dither;}
	void set_dithering(bool b)   		{m_dither = b;}

	bool draw_grid_on() const     		{return m_draw_grid;}
	void set_draw_grid(bool b)    		{m_draw_grid = b;}

	bool draw_values_on() const   		{return m_draw_values;}
	void set_draw_values(bool b)  		{m_draw_values = b;}



	// Callback functions

	/// Callback executed whenever the gamma value has been changed, e.g. via @ref set_gamma
	const FloatCallback& gamma_callback() const { return m_gamma_callback; }
	void set_gamma_callback(const FloatCallback &cb) { m_gamma_callback = cb; }

	/// Callback executed whenever the exposure value has been changed, e.g. via @ref set_exposure
	const FloatCallback& exposure_callback() const { return m_exposure_callback; }
	void set_exposure_callback(const FloatCallback &cb) { m_exposure_callback = cb; }

	/// Callback executed whenever the sRGB setting has been changed, e.g. via @ref set_sRGB
	const BoolCallback& sRGB_callback() const { return m_sRGB_callback; }
	void set_sRGB_callback(const BoolCallback &cb) { m_sRGB_callback = cb; }

	/// Callback executed when the zoom level changes
	const FloatCallback& zoom_callback() const { return m_zoom_callback; }
	void set_zoom_callback(const FloatCallback &cb) { m_zoom_callback = cb; }

    /// Callback that is used to acquire information about pixel components
    const PixelCallback &pixel_callback() const { return m_pixel_callback; }
    void set_pixel_callback(const PixelCallback &cb) { m_pixel_callback = cb; }

protected:
    Vector2f position_f() const                             { return Vector2f(m_pos); }
	Vector2f size_f() const                                 { return Vector2f(m_size); }

	Vector2i image_size(ConstImagePtr img) const           	{ return img ? img->size() : Vector2i(0,0); }
	Vector2f image_size_f(ConstImagePtr img) const         	{ return Vector2f(image_size(img)); }
	Vector2f scaled_image_size_f(ConstImagePtr img) const  	{ return m_zoom * image_size_f(img); }

	Vector2f center_offset(ConstImagePtr img) const;

	// Helper drawing methods.
	void draw_widget_border(NVGcontext* ctx) const;
	void draw_image_border(NVGcontext* ctx) const;
	void draw_helpers(NVGcontext* ctx) const;
	void draw_pixel_grid(NVGcontext* ctx) const;
	void draw_pixel_info(NVGcontext *ctx) const;
	void draw_ROI(NVGcontext *ctx) const;
	void image_position_and_scale(Vector2f & position,
							   	  Vector2f & scale,
	                              ConstImagePtr image);

	ConstImagePtr m_current_image;
	ConstImagePtr m_reference_image;
	TextureRef m_null_image;

    ref<Shader> m_image_shader;
    TextureRef m_dither_tex;

    float m_exposure = 0.f,
		  m_gamma = 2.2f;
	bool m_sRGB = true,
		 m_dither = true,
		 m_draw_grid = true,
		 m_draw_values = true;

	// Image display parameters.
	float m_zoom;                           ///< The scale/zoom of the image
	float m_zoom_level;                     ///< The zoom level
	Vector2f m_offset = 0;         			///< The panning offset of the
	EChannel m_channel = EChannel::RGB;     ///< Which channel to display
	EBlendMode m_blend_mode = EBlendMode::NORMAL_BLEND;     ///< How to blend the current and reference images

	// Fine-tuning parameters.
	float m_zoom_sensitivity = 1.0717734625f;

	// Image info parameters.
	float m_grid_threshold = -1;
	float m_pixel_info_threshold = -1;

	// various callback functions
	FloatCallback m_exposure_callback;
	FloatCallback m_gamma_callback;
	BoolCallback m_sRGB_callback;
	FloatCallback m_zoom_callback;
    PixelCallback m_pixel_callback;

	Vector2i m_clicked;
};
