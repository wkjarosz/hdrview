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

	void repopulate_image_list();

	// Const access to the loaded images. Modification only possible via modify_image, undo, redo
	int num_images() const                  {return int(m_images.size());}
	int current_image_index() const          {return m_current;}
	int reference_image_index() const        {return m_reference;}
	ConstImagePtr current_image() const     {return image(m_current);}
	     ImagePtr current_image()           {return image(m_current);}
	ConstImagePtr reference_image() const   {return image(m_reference);}
	     ImagePtr reference_image()         {return image(m_reference);}
	ConstImagePtr image(int index) const;
	     ImagePtr image(int index);

	bool set_current_image_index(int newIndex, bool forceCallback = false);
	bool set_reference_image_index(int newIndex);
	bool swap_current_selected_with_previous() {printf("current: %d; previous: %d\n", m_current, m_previous);return isValid(m_previous) ? set_current_image_index(m_previous) : false;}
	bool swap_images(int index1, int index2);
	bool send_image_backward();
	bool bring_image_forward();

	// Loading, saving, closing, and rearranging the images in the image stack
	void load_images(const std::vector<std::string> & filenames);
	bool save_image(const std::string & filename, float exposure = 0.f, float gamma = 2.2f,
				   bool sRGB = true, bool dither = true);
	bool close_image();
	void close_all_images();

	// Modify the image data
	void modify_image(const ImageCommand & command);
	void modify_image(const ImageCommandWithProgress & command);
	void undo();
	void redo();

	//
	void run_requested_callbacks();


	void request_buttons_update();
	void request_histogram_update(bool force = false);

	EBlendMode blend_mode() const;
	void set_blend_mode(EBlendMode mode);

	EChannel channel() const;
	void set_channel(EChannel channel);

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
	bool isValid(int index) const {return index >= 0 && index < num_images();}

	std::vector<ImagePtr> m_images; ///< The loaded images
	int m_current = -1;             ///< The currently selected image
	int m_reference = -1;           ///< The currently selected reference image

	int m_previous = -1;			///< The previously selected image

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
};