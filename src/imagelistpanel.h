//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>
#include "fwd.h"

using namespace nanogui;

class ImageListPanel : public Widget
{
public:
	ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imgMgr, HDRImageViewer * imgViewer);

	void repopulateImageList();
	void enableDisableButtons();
	void setCurrentImage(int newIndex);
	void setReferenceImage(int newIndex);

	EBlendMode blendMode() const;
	void setBlendMode(EBlendMode mode);

	EChannel channel() const;
	void setChannel(EChannel channel);

private:
	void updateHistogram();

	HDRViewScreen * m_screen = nullptr;
	HDRImageManager * m_imageMgr = nullptr;
	HDRImageViewer * m_imageViewer = nullptr;
	Button * m_saveButton = nullptr;
	Button * m_closeButton = nullptr;
	Button * m_bringForwardButton = nullptr;
	Button * m_sendBackwardButton = nullptr;
	Widget * m_imageListWidget = nullptr;
	ComboBox * m_blendModes = nullptr;
	ComboBox * m_channels = nullptr;
	std::vector<ImageButton*> m_imageButtons;

	Button * m_linearToggle = nullptr;
	Button * m_sRGBToggle = nullptr;
	Button * m_recomputeHistogram = nullptr;
	MultiGraph * m_graph = nullptr;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};