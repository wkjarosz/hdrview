//
// Created by Wojciech Jarosz on 9/4/17.
//

#pragma once

#include <vector>
#include "fwd.h"
#include "common.h"
#include "glimage.h"

using namespace nanogui;

/*!
 * @class 	HDRImageManager hdrimagemanager.h
 * @brief	Manages a set of loaded images
 */
class HDRImageManager
{
public:
	HDRImageManager();

	// Const access to the loaded images. Modification only possible via modifyImage, undo, redo
	int numImages() const {return m_images.size();}
	int currentImageIndex() const {return m_current;}
	const GLImage * currentImage() const;
	      GLImage * currentImage();
	const GLImage * image(int index) const;
		  GLImage * image(int index);

	// Loading, saving, closing, and rearranging the images in the image stack
	void loadImages(const std::vector<std::string> & filenames);
	void saveImage(const std::string & filename, float exposure = 0.f, float gamma = 2.2f,
	               bool sRGB = true, bool dither = true);
	void closeImage(int index);
	void sendLayerBackward();
	void bringLayerForward();
	void selectLayer(int index, bool forceCallback = false);

	// Modify the image data
	void modifyImage(const std::function<ImageCommandUndo*(HDRImage & img)> & command);
	void undo();
	void redo();

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

private:

	std::vector<GLImage*> m_images;         ///< The loaded images
	int m_current = -1;                     ///< The currently selected image/layer

	// various callback functions
	std::function<void(int)> m_imageChangedCallback;
	std::function<void(void)> m_numLayersCallback;
	std::function<void(int)> m_layerSelectedCallback;
};
