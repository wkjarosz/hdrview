//
// Created by Wojciech Jarosz on 9/4/17.
//

#pragma once

#include <nanogui/widget.h>
#include "fwd.h"

NAMESPACE_BEGIN(nanogui)

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

	Slider * m_exposureSlider = nullptr;
	FloatBox<float> * m_exposureTextBox = nullptr;
	Button * m_linearToggle = nullptr;
	Button * m_resetExposure = nullptr;
	MultiGraph * m_graph = nullptr;
	float m_exposure = 1.0f;
	bool m_linear = true;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

NAMESPACE_END(nanogui)