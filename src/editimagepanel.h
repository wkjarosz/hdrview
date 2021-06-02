//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>
#include <memory>
#include "fwd.h"

using namespace nanogui;

class EditImagePanel : public Widget
{
public:
	EditImagePanel(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel);

	void copy();
	void paste();

	void draw(NVGcontext *ctx) override;

private:
	HDRViewScreen * m_screen = nullptr;
    ImageListPanel * m_images_panel = nullptr;
	Button * m_undo_btn = nullptr;
	Button * m_redo_btn = nullptr;
	std::vector<Button*> m_filter_btns;

	std::shared_ptr<HDRImage> m_clipboard;
};