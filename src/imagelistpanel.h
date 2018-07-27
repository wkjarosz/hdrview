//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>
#include "common.h"
#include "fwd.h"

using namespace nanogui;

class ImageListPanel : public Widget
{
public:
	ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imgMgr, HDRImageViewer * imgViewer);

	void draw(NVGcontext *ctx) override;

	void repopulateImageList();
	void setCurrentImage(int newIndex);
	void setReferenceImage(int newIndex);
	bool swapImages(int index1, int index2);
	bool sendImageBackward();
	bool bringImageForward();
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


	HDRViewScreen * m_screen = nullptr;
	HDRImageManager * m_imageMgr = nullptr;
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