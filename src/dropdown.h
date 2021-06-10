//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "popupmenu.h"
#include <nanogui/button.h>

NAMESPACE_BEGIN(nanogui)

class Dropdown : public Button
{
public:
    /// Create an empty combo box
    Dropdown(Widget *parent);

    /// Create a new combo box with the given items
    Dropdown(Widget *parent, const std::vector<std::string> &items);

    /**
     * \brief Create a new combo box with the given items, providing both short and
     * long descriptive labels for each item
     */
    Dropdown(Widget *parent, const std::vector<std::string> &items, const std::vector<std::string> &items_short);

    /// The current index this Dropdown has selected.
    int selected_index() const { return m_selected_index; }

    /// Sets the current index this Dropdown has selected.
    void set_selected_index(int idx);

    /// The callback to execute for this Dropdown.
    std::function<void(int)> callback() const { return m_callback; }

    /// Sets the callback to execute for this Dropdown.
    void set_callback(const std::function<void(int)> &callback) { m_callback = callback; }

    /// Sets the items for this Dropdown, providing both short and long descriptive lables for each item.
    void set_items(const std::vector<std::string> &items, const std::vector<std::string> &items_short);
    /// Sets the items for this Dropdown.
    void set_items(const std::vector<std::string> &items) { set_items(items, items); }
    /// The items associated with this Dropdown.
    const std::vector<std::string> &items() const { return m_items; }
    /// The short descriptions associated with this Dropdown.
    const std::vector<std::string> &items_short() const { return m_items_short; }

    virtual Vector2i preferred_size(NVGcontext *ctx) const override;

    virtual void draw(NVGcontext *ctx) override;

    /// Handles mouse scrolling events for this Dropdown.
    virtual bool scroll_event(const Vector2i &p, const Vector2f &rel) override;

    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

protected:
    PopupMenu *m_popup;

    /// The items associated with this Dropdown.
    std::vector<std::string> m_items;

    /// The short descriptions of items associated with this Dropdown.
    std::vector<std::string> m_items_short;

    /// The callback for this Dropdown.
    std::function<void(int)> m_callback;

    /// The current index this Dropdown has selected.
    int m_selected_index;
};

NAMESPACE_END(nanogui)
