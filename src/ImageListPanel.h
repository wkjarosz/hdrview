//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>
#include <vector>
#include "Common.h"
#include "GLImage.h"
#include "Fwd.h"

using namespace nanogui;

class ImageListPanel : public Widget
{
public:
	ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageViewer * imgViewer);

	void draw(NVGcontext *ctx) override;

	void repopulateImageList();

	// Const access to the loaded images. Modification only possible via modifyImage, undo, redo
	int numImages() const                  {return int(m_images.size());}
	int currentImageIndex() const          {return m_current;}
	int referenceImageIndex() const        {return m_reference;}
	ConstImagePtr currentImage() const     {return image(m_current);}
	ImagePtr currentImage()           {return image(m_current);}
	ConstImagePtr referenceImage() const   {return image(m_reference);}
	ImagePtr referenceImage()         {return image(m_reference);}
	ConstImagePtr image(int index) const;
	ImagePtr image(int index);

	bool setCurrentImageIndex(int newIndex, bool forceCallback = false);
	bool setReferenceImageIndex(int newIndex);
	bool swapImages(int index1, int index2);
	bool sendImageBackward();
	bool bringImageForward();

	// Loading, saving, closing, and rearranging the images in the image stack
	void loadImages(const std::vector<std::string> & filenames);
	bool saveImage(const std::string & filename, float exposure = 0.f, float gamma = 2.2f,
				   bool sRGB = true, bool dither = true);
	bool closeImage();
	void closeAllImages();

	// Modify the image data
	void modifyImage(const ImageCommand & command);
	void modifyImage(const ImageCommandWithProgress & command);
	void undo();
	void redo();

	//
	void runRequestedCallbacks();


	void requestButtonsUpdate();
	void requestHistogramUpdate(bool force = false);

	EBlendMode blendMode() const;
	void setBlendMode(EBlendMode mode);

	EChannel channel() const;
	void setChannel(EChannel channel);

	bool nthImageIsVisible(int n) const;
	int nextVisibleImage(int index, EDirection direction) const;
	int nthVisibleImageIndex(int n) const;

	bool useRegex() const;
	void setUseRegex(bool value);

	bool setFilter(const std::string& filter);
	std::string filter() const;
	void focusFilter();


private:
	void updateButtons();
	void enableDisableButtons();
	void updateHistogram();
	void updateFilter();
	bool isValid(int index) const {return index >= 0 && index < numImages();}

	std::vector<ImagePtr> m_images; ///< The loaded images
	int m_current = -1;             ///< The currently selected image
	int m_reference = -1;           ///< The currently selected reference image

	std::atomic<bool> m_imageModifyDoneRequested;

	// various callback functions
	std::function<void(int)> m_imageModifyDoneCallback;
	std::function<void()> m_numImagesCallback;
	std::function<void()> m_currentImageCallback;
	std::function<void()> m_referenceImageCallback;


	HDRViewScreen * m_screen = nullptr;
	HDRImageViewer * m_imageViewer = nullptr;
	Button * m_saveButton = nullptr;
	Button * m_closeButton = nullptr;
	Button * m_bringForwardButton = nullptr;
	Button * m_sendBackwardButton = nullptr;
	TextBox * m_filter = nullptr;
	Button* m_eraseButton = nullptr;
	Button* m_regexButton = nullptr;
	Button * m_useShortButton = nullptr;
	Widget * m_imageListWidget = nullptr;
	ComboBox * m_blendModes = nullptr;
	ComboBox * m_channels = nullptr;
	std::vector<ImageButton*> m_imageButtons;

	ComboBox * m_xAxisScale = nullptr,
			 * m_yAxisScale = nullptr;
	MultiGraph * m_graph = nullptr;
	bool m_histogramDirty = false;
	bool m_histogramUpdateRequested = false;
	bool m_updateFilterRequested = true;
	bool m_buttonsUpdateRequested = true;
	double m_histogramRequestTime;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};