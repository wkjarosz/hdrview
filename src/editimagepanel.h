//
// Created by Wojciech Jarosz on 9/3/17.
//

#pragma once

#include <nanogui/widget.h>
#include "fwd.h"

NAMESPACE_BEGIN(nanogui)

class EditImagePanel : public Widget
{
public:
	EditImagePanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr);

	void enableDisableButtons();

private:
	HDRViewScreen * m_screen = nullptr;
	HDRImageManager * m_imageMgr = nullptr;
	Button * m_undoButton = nullptr;
	Button * m_redoButton = nullptr;
	std::vector<Button*> m_filterButtons;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

NAMESPACE_END(nanogui)