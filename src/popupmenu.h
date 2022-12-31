//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/button.h>
#include <nanogui/popup.h>

NAMESPACE_BEGIN(nanogui)

class MenuItem : public Button
{
public:
    MenuItem(Widget *parent, const std::string &caption = "Untitled", int button_icon = 0);

    virtual void     draw(NVGcontext *ctx) override;
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;
};

class Separator : public MenuItem
{
public:
    Separator(Widget *parent);
    virtual void draw(NVGcontext *ctx) override;
};

/// A popup menu styled after the dark theme in macOS
class PopupMenu : public Popup
{
public:
    /// Create a new popup parented to a screen (first argument) and a parent window (if applicable)
    PopupMenu(Widget *parent, Window *parent_window = nullptr);

    /// Invoke the associated layout generator to properly place child widgets, if any
    virtual void perform_layout(NVGcontext *ctx) override { Widget::perform_layout(ctx); }

    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

    /// Draw the popup window
    virtual void draw(NVGcontext *ctx) override;

    static constexpr int menu_item_height = 20;
    static constexpr int seperator_height = 8;
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
