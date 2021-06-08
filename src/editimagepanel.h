//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include "well.h"
#include <memory>

using namespace nanogui;

class EditImagePanel : public Well
{
public:
    EditImagePanel(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel, HDRImageView *image_view);

    void copy();
    void paste();

    void draw(NVGcontext *ctx) override;

private:
    HDRViewScreen *       m_screen       = nullptr;
    ImageListPanel *      m_images_panel = nullptr;
    HDRImageView *        m_image_view   = nullptr;
    Button *              m_undo_btn     = nullptr;
    Button *              m_redo_btn     = nullptr;
    std::vector<Button *> m_filter_btns;

    std::shared_ptr<HDRImage> m_clipboard;
};