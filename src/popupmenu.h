//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/popup.h>

NAMESPACE_BEGIN(nanogui)

/// A popup menu styled after the dark theme in macOS
class PopupMenu : public Popup
{
public:
    /// Create a new popup parented to a screen (first argument) and a parent window (if applicable)
    PopupMenu(Widget *parent, Window *parent_window = nullptr);

    Button *add_item(const std::string &name, int icon = 0);

    /// Invoke the associated layout generator to properly place child widgets, if any
    virtual void perform_layout(NVGcontext *ctx) override { Widget::perform_layout(ctx); }

    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

    /// Draw the popup window
    virtual void draw(NVGcontext *ctx) override;
};

/// Wrap another widget with a right-click popup menu
class PopupWrapper : public Widget
{
public:
    PopupWrapper(Widget *parent, PopupMenu *menu = nullptr);

    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

protected:
    PopupMenu *m_right_click_menu;
    bool       m_right_pushed = false;
};

NAMESPACE_END(nanogui)
