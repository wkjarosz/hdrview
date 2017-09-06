//
// Created by Wojciech Jarosz on 9/4/17.
//

#pragma once

#include <nanogui/widget.h>
#include "fwd.h"

NAMESPACE_BEGIN(nanogui)

class LayersPanel : public Widget
{
public:
	LayersPanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imgMgr);

	void repopulateLayerList();
	void enableDisableButtons();
	void selectLayer(int newIndex);

private:
	HDRViewScreen * m_screen = nullptr;
	HDRImageManager * m_imageMgr = nullptr;
	Button * m_saveButton = nullptr;
	Widget * m_layerListWidget = nullptr;
	std::vector<Button*> m_layerButtons;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

NAMESPACE_END(nanogui)