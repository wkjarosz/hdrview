//
// Created by Wojciech Jarosz on 9/4/17.
//

#pragma once

#include <nanogui/widget.h>
#include <vector>
#include "fwd.h"
#include "common.h"
#include "gldithertexture.h"
#include "glimage.h"

NAMESPACE_BEGIN(nanogui)

/*!
 * @class 	HDRImageViewer hdrimageviewer.h
 * @brief	Widget used to manage and display multiple HDR images.
 */
class HDRImageViewer : public Widget
{
public:
	HDRImageViewer(Widget * parent, HDRViewScreen * screen);

	void bindImage(const GLImage* image) {m_image = image;}

	// overridden Widget virtual functions
	void draw(NVGcontext* ctx) override;
	bool mouseDragEvent(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
	bool mouseMotionEvent(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
	bool scrollEvent(const Vector2i &p, const Vector2f &rel) override;

	// Getters and setters
	Vector2f positionF() const                              { return mPos.cast<float>(); }
	Vector2f sizeF() const                                  { return mSize.cast<float>(); }

	Vector2i imageSize() const                              { return m_image ? m_image->size() : Vector2i(0,0); }
	Vector2f imageSizeF() const                             { return imageSize().cast<float>(); }
	Vector2f scaledImageSizeF() const                       { return (m_zoom * imageSize().cast<float>()); }

	float scale() const                                     { return m_zoom; }

	const Vector2f& offset() const                          { return m_offset; }
	void setOffset(const Vector2f& offset)                  { m_offset = offset; }

	float zoomSensitivity() const                           { return m_zoomSensitivity; }
	void setZoomSensitivity(float zoomSensitivity)          { m_zoomSensitivity = zoomSensitivity; }

	float gridThreshold() const                             { return m_gridThreshold; }
	void setGridThreshold(float gridThreshold)              { m_gridThreshold = gridThreshold; }

	float pixelInfoThreshold() const                        { return m_pixelInfoThreshold; }
	void setPixelInfoThreshold(float pixelInfoThreshold)    { m_pixelInfoThreshold = pixelInfoThreshold; }

	/// Function indicating whether the grid is currently visible.
	bool gridVisible() const;

	/// Function indicating whether the pixel information is currently visible.
	bool pixelInfoVisible() const;

	/// Function indicating whether any of the overlays are visible.
	bool helpersVisible() const;


	// Image transformation functions.

	/// Calculates the image coordinates of the given pixel position on the widget.
	Vector2f imageCoordinateAt(const Vector2f& position) const;

	/**
	 * Calculates the image coordinates of the given pixel position on the widget.
	 * If the position provided corresponds to a coordinate outside the range of
	 * the image, the coordinates are clamped to edges of the image.
	 */
	Vector2f clampedImageCoordinateAt(const Vector2f& position) const;

	/// Calculates the position inside the widget for the given image coordinate.
	Vector2f positionForCoordinate(const Vector2f& imageCoordinate) const;

	/// Calculates the position inside the widget for the given image coordinate.
	Vector2f screenPositionForCoordinate(const Vector2f& imageCoordinate) const;

	/**
	 * Modifies the internal state of the image viewer widget so that the pixel at the provided
	 * position on the widget has the specified image coordinate. Also clamps the values of offset
	 * to the sides of the widget.
	 */
	void setImageCoordinateAt(const Vector2f& position, const Vector2f& imageCoordinate);

	/// Centers the image without affecting the scaling factor.
	void center();

	/// Centers and scales the image so that it fits inside the widget.
	void fit();

	/// Moves the offset by the specified amount. Does bound checking.
	void moveOffset(const Vector2f& delta);

	/**
	 * Changes the scale factor by the provided amount modified by the zoom sensitivity member variable.
	 * The scaling occurs such that the image coordinate under the focused position remains in
	 * the same position before and after the scaling.
	 */
	void zoomBy(int amount, const Vector2f &focusPosition);

	/// Zoom in to the next power of two
	void zoomIn();

	/// Zoom out to the previous power of two
	void zoomOut();


	float zoomLevel() const       {return m_zoomLevel;}
	void setZoomLevel(float l);

	const Vector3f & channel()  {return m_channels;}
	void setChannel(const Vector3f & c) {m_channels = c;}

	float gamma() const         {return m_gamma;}
	void setGamma(float g)      {if (m_gamma != g) {m_gamma = g; m_gammaCallback(g);}}

	float exposure() const      {return m_exposure;}
	void setExposure(float e)   {if (m_exposure != e) {m_exposure = e; m_exposureCallback(e);}}

	bool sRGB() const           {return m_sRGB;}
	void setSRGB(bool b)        {m_sRGB = b;}

	bool ditheringOn() const    {return m_dither;}
	void setDithering(bool b)   {m_dither = b;}

	bool drawGridOn() const     {return m_drawGrid;}
	void setDrawGrid(bool b)    {m_drawGrid = b;}

	bool drawValuesOn() const   {return m_drawValues;}
	void setDrawValues(bool b)  {m_drawValues = b;}

	// Callback functions

	/// Callback executed whenever the gamma value has been changed, e.g. via @ref setGamma
	const std::function<void(float)>& gammaCallback() const { return m_gammaCallback; }
	void setGammaCallback(const std::function<void(float)> &callback) { m_gammaCallback = callback; }

	/// Callback executed whenever the exposure value has been changed, e.g. via @ref setExposure
	const std::function<void(float)>& exposureCallback() const { return m_exposureCallback; }
	void setExposureCallback(const std::function<void(float)> &callback) { m_exposureCallback = callback; }

	/// Callback executed when the zoom level changes
	const std::function<void(float)>& zoomCallback() const { return m_zoomCallback; }
	void setZoomCallback(const std::function<void(float)> &callback) { m_zoomCallback = callback; }

	/// Callback executed when mouse hovers over different parts of the image, provides pixel coordinates and values
	const std::function<void(const Vector2i &, const Color4 &, const Color4 &)> pixelHoverCallback() const { return m_pixelHoverCallback; }
	void setPixelHoverCallback(const std::function<void(const Vector2i &, const Color4 &, const Color4 &)> &callback) { m_pixelHoverCallback = callback; }

private:
	// Helper drawing methods.
	void drawWidgetBorder(NVGcontext* ctx) const;
	void drawImageBorder(NVGcontext* ctx) const;
	void drawHelpers(NVGcontext* ctx) const;
	void drawPixelGrid(NVGcontext* ctx) const;
	void drawPixelInfo(NVGcontext *ctx) const;
	Vector2f centerOffset() const;

	GLDitherTexture m_ditherer;

	HDRViewScreen * m_screen = nullptr;
	const GLImage * m_image = nullptr;
	float m_exposure = 0.f,
		  m_gamma = 2.2f;
	bool m_sRGB = true,
		 m_dither = true,
		 m_drawGrid = true,
		 m_drawValues = true;


	// Image display parameters.
	float m_zoom;                           ///< The scale/zoom of the image
	float m_zoomLevel;                      ///< The zoom level
	Vector2f m_offset;                      ///< The panning offset of the
	Vector3f m_channels = Vector3f::Ones(); ///< Multiplied with the pixel values before display, allows visualizing individual color channels

	// Fine-tuning parameters.
	float m_zoomSensitivity = 1.0717734625f;

	// Image info parameters.
	float m_gridThreshold = -1;
	float m_pixelInfoThreshold = -1;

	// various callback functions
	std::function<void(float)> m_exposureCallback;
	std::function<void(float)> m_gammaCallback;
	std::function<void(float)> m_zoomCallback;
	std::function<void(const Vector2i &, const Color4 &, const Color4 &)> m_pixelHoverCallback;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

NAMESPACE_END(nanogui)