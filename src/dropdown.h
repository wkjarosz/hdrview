//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "popupmenu.h"
#include <nanogui/button.h>

NAMESPACE_BEGIN(nanogui)

class Dropdown : public MenuItem
{
public:
    enum Mode : int
    {
        ComboBox,
        Menu,
        Submenu
    };

    /// Create an empty combo box
    Dropdown(Widget *parent, Mode mode = ComboBox, const std::string &caption = "Untitled");

    /**
     * \brief Create a new combo box with the given items, providing long names and optionally short names and icons for
     * each item
     */
    Dropdown(Widget *parent, const std::vector<std::string> &items, const std::vector<int> &icons = {},
             Mode mode = ComboBox, const std::string &caption = "Untitled");

    /// The current index this Dropdown has selected.
    int selected_index() const { return m_selected_index; }

    /// Sets the current index this Dropdown has selected.
    void set_selected_index(int idx);

    /// The callback to execute for this Dropdown.
    std::function<void(int)> selected_callback() const { return m_selected_callback; }

    /// Sets the callback to execute for this Dropdown.
    void set_selected_callback(const std::function<void(int)> &callback) { m_selected_callback = callback; }

    PopupMenu       *popup() { return m_popup; }
    const PopupMenu *popup() const { return m_popup; }

    /// Sets the items for this Dropdown, providing names and optionally icons for each item
    void set_items(const std::vector<std::string> &items, const std::vector<int> &icons = {});

    virtual Vector2i preferred_size(NVGcontext *ctx) const override;

    virtual void draw(NVGcontext *ctx) override;

    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

    /// Returns the idx-th item in the menu
    MenuItem *item(int idx) const;

protected:
    Vector2i compute_position() const;

    PopupMenu *m_popup;

    /// The callback for this Dropdown.
    std::function<void(int)> m_selected_callback;

    /// The current index this Dropdown has selected.
    int m_selected_index;

    Mode m_mode = ComboBox;
};

NAMESPACE_END(nanogui)
