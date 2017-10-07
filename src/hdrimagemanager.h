//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <vector>
#include "fwd.h"
#include "common.h"
#include "glimage.h"


/*!
 * @class 	HDRImageManager hdrimagemanager.h
 * @brief	Manages a set of loaded images
 */
class HDRImageManager
{
public:

	HDRImageManager();

	// Const access to the loaded images. Modification only possible via modifyImage, undo, redo
	int numImages() const                  {return m_images.size();}
	int currentImageIndex() const          {return m_current;}
	int referenceImageIndex() const        {return m_reference;}
	ConstImagePtr currentImage() const     {return image(m_current);}
		 ImagePtr currentImage()           {return image(m_current);}
	ConstImagePtr referenceImage() const   {return image(m_reference);}
		 ImagePtr referenceImage()         {return image(m_reference);}
	ConstImagePtr image(int index) const;
		 ImagePtr image(int index);

	// Loading, saving, closing, and rearranging the images in the image stack
	void loadImages(const std::vector<std::string> & filenames);
	void saveImage(const std::string & filename, float exposure = 0.f, float gamma = 2.2f,
	               bool sRGB = true, bool dither = true);
	void closeImage(int index);
	void closeAllImages();
	void sendImageBackward();
	void bringImageForward();
	void setCurrentImageIndex(int index, bool forceCallback = false);
	void setReferenceImageIndex(int index, bool forceCallback = false);

	// Modify the image data
	void modifyImage(const ImageCommand & command);
	void modifyImage(const ImageCommandWithProgress & command);
	void undo();
	void redo();

	//
	void runRequestedCallbacks();

	// Callback functions

	/// Callback executed whenever an image starts being modified, e.g. via @ref modifyImage
	const std::function<void(int)>& imageModifyStartCallback() const { return m_imageModifyStartCallback; }
	void setImageModifyStartCallback(const std::function<void(int)> &callback) { m_imageModifyStartCallback = callback; }

	/// Callback executed whenever an image finishes being modified, e.g. via @ref modifyImage
	const std::function<void(int)>& imageModifyDoneCallback() const { return m_imageModifyDoneCallback; }
	void setImageModifyDoneCallback(const std::function<void(int)> &callback) { m_imageModifyDoneCallback = callback; }

	/// Callback executed whenever the number of images has been changed, e.g. via @ref loadImages or closeImage
	const std::function<void()>& numImagesCallback() const { return m_numImagesCallback; }
	void setNumImagesCallback(const std::function<void()> &callback) { m_numImagesCallback = callback; }

	/// Callback executed whenever the currently selected image has been changed, e.g. via @ref selectImage
	const std::function<void()>& currentImageCallback() const { return m_currentImageCallback; }
	void setCurrentImageCallback(const std::function<void()> &callback) { m_currentImageCallback = callback; }

	/// Callback executed whenever the currently selected reference image has been changed, e.g. via @ref selectReference
	const std::function<void()>& referenceImageCallback() const { return m_referenceImageCallback; }
	void setReferenceImageCallback(const std::function<void()> &callback) { m_referenceImageCallback = callback; }

private:

	std::vector<ImagePtr> m_images; ///< The loaded images
	int m_current = -1;             ///< The currently selected image
	int m_reference = -1;           ///< The currently selected reference image

	std::atomic<bool> m_imageModifyDoneRequested;

	// various callback functions
	std::function<void(int)> m_imageModifyStartCallback;
	std::function<void(int)> m_imageModifyDoneCallback;
	std::function<void()> m_numImagesCallback;
	std::function<void()> m_currentImageCallback;
	std::function<void()> m_referenceImageCallback;
};
