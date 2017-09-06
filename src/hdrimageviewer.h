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
	HDRImageViewer(HDRViewScreen * parent);

	// Const access to the loaded images. Modification only possible via modifyImage, undo, redo
	int numImages() const {return m_images.size();}
	const GLImage * image(int index) const;
	GLImage * image(int index);
	int currentImageIndex() const {return m_current;}
	const GLImage * currentImage() const;
	GLImage * currentImage();

	// Loading, saving, closing, and rearranging the images in the image stack
	bool loadImages(const std::vector<std::string> & filenames);
	void saveImage();
	void closeImage(int index);
	void sendLayerBackward();
	void bringLayerForward();
	void selectLayer(int index);

	// Modify the image data
	void modifyImage(const std::function<ImageCommandUndo*(HDRImage & img)> & command);
	void undo();
	void redo();

	void draw(NVGcontext* ctx) override;

	Vector2i topLeftImageCorner2Screen() const;
	Vector2i screenToImage(const Vector2i & p) const;
	Vector2i imageToScreen(const Vector2i & pixel) const;

	// Getters and setters

	const Vector2f & offset() const         {return m_offset;}
	void setOffset(const Vector2f &p)       {m_offset = p;}
	void moveOffset(const Vector2i &pixels) {m_offset += pixels.cast<float>() / m_zoom;}

	int zoomLevel() const       {return m_zoomLevel;}
	void setZoomLevel(int l)    {m_zoomLevel = clamp(l, -20, 20); m_zoom = powf(2.f, m_zoomLevel/2.f);}
	float zoom() const          {return m_zoom;}

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

	/// Callback executed whenever the image data has been modified, e.g. via @ref modifyImage
	const std::function<void(int)>& imageChangedCallback() const { return m_imageChangedCallback; }
	void setImageChangedCallback(const std::function<void(int)> &callback) { m_imageChangedCallback = callback; }

	/// Callback executed whenever the number of layers/images has been changed, e.g. via @ref loadImages or closeImage
	const std::function<void(void)>& numLayersCallback() const { return m_numLayersCallback; }
	void setNumLayersCallback(const std::function<void(void)> &callback) { m_numLayersCallback = callback; }

	/// Callback executed whenever the currently selected layer has been changed, e.g. via @ref selectLayer
	const std::function<void(int)>& layerSelectedCallback() const { return m_layerSelectedCallback; }
	void setLayerSelectedCallback(const std::function<void(int)> &callback) { m_layerSelectedCallback = callback; }

	/// Callback executed whenever the gamma value has been changed, e.g. via @ref setGamma
	const std::function<void(float)>& gammaCallback() const { return m_gammaCallback; }
	void setGammaCallback(const std::function<void(float)> &callback) { m_gammaCallback = callback; }

	/// Callback executed whenever the exposure value has been changed, e.g. via @ref setExposure
	const std::function<void(float)>& exposureCallback() const { return m_exposureCallback; }
	void setExposureCallback(const std::function<void(float)> &callback) { m_exposureCallback = callback; }

private:

	// drawing helper functions
	void drawPixelGrid(NVGcontext* ctx, const Matrix4f &mvp) const;
	void drawPixelLabels(NVGcontext* ctx) const;

	GLDitherTexture m_ditherer;

	HDRViewScreen * m_screen = nullptr;
	float m_exposure = 0.f,
		  m_gamma = 2.2f;
	bool m_sRGB = true,
		 m_dither = true,
		 m_drawGrid = true,
		 m_drawValues = true;

	Vector2f m_offset = Vector2f::Zero();   ///< The panning offset of the
	int m_zoomLevel = 0;                    ///< The zoom level
	float m_zoom = 1.0f;                    ///< The scale/zoom of the image
	Vector3f m_channels = Vector3f::Ones(); ///< Multiplied with the pixel values before display, allows visualizing individual color channels

	std::vector<GLImage*> m_images;         ///< The loaded images
	int m_current = -1;                     ///< The currently selected image/layer

	// various callback functions
	std::function<void(float)> m_exposureCallback;
	std::function<void(float)> m_gammaCallback;
	std::function<void(int)> m_imageChangedCallback;
	std::function<void(void)> m_numLayersCallback;
	std::function<void(int)> m_layerSelectedCallback;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

NAMESPACE_END(nanogui)