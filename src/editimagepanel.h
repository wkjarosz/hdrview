//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>
#include "fwd.h"

using namespace nanogui;

class EditImagePanel : public Widget
{
public:
	EditImagePanel(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel);

	void draw(NVGcontext *ctx) override;

private:
	HDRViewScreen * m_screen = nullptr;
    ImageListPanel * m_imagesPanel = nullptr;
	Button * m_undoButton = nullptr;
	Button * m_redoButton = nullptr;
	std::vector<Button*> m_filterButtons;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};