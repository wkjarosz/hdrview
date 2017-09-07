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
	void sendImageBackward();
	void bringImageForward();
	void selectImage(int index, bool forceCallback = false);

	// Modify the image data
	void modifyImage(const std::function<ImageCommandUndo*(HDRImage & img)> & command);
	void undo();
	void redo();

	// Callback functions

	/// Callback executed whenever the image data has been modified, e.g. via @ref modifyImage
	const std::function<void(int)>& imageChangedCallback() const { return m_imageChangedCallback; }
	void setImageChangedCallback(const std::function<void(int)> &callback) { m_imageChangedCallback = callback; }

	/// Callback executed whenever the number of images has been changed, e.g. via @ref loadImages or closeImage
	const std::function<void(void)>& numImagesCallback() const { return m_numImagesCallback; }
	void setNumImagesCallback(const std::function<void(void)> &callback) { m_numImagesCallback = callback; }

	/// Callback executed whenever the currently selected image has been changed, e.g. via @ref selectImage
	const std::function<void(int)>& imageSelectedCallback() const { return m_imageSelectedCallback; }
	void setImageSelectedCallback(const std::function<void(int)> &callback) { m_imageSelectedCallback = callback; }

private:

	std::vector<GLImage*> m_images;         ///< The loaded images
	int m_current = -1;                     ///< The currently selected image

	// various callback functions
	std::function<void(int)> m_imageChangedCallback;
	std::function<void(void)> m_numImagesCallback;
	std::function<void(int)> m_imageSelectedCallback;
};
