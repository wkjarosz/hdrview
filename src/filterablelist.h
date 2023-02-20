//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "common.h"
#include "fwd.h"
#include "searchbox.h"
#include "well.h"
#include "xpuimage.h"
#include <vector>

using namespace nanogui;

/**
 * @brief Manages a list of open items along with associated widgets.
 *
 * An item can have four states:
 *   * deselected
 *   * selected
 *   * current
 *   * reference
 *
 * Multiple items can be selected, but only one item can be current, and only one can be reference.
 * If an item is current, it is automatically selected.
 */
class FilterableList : public Well
{
public:
    using IntCallback  = std::function<void(int)>;
    using VoidCallback = std::function<void(void)>;

    FilterableList(Widget *parent);

    void draw(NVGcontext *ctx) override;

    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;

    // Access to the list items.
    int  current_index() const { return m_current; }
    int  reference_index() const { return m_reference; }
    bool is_selected(int index) const;

    bool set_current_index(int new_index, bool force_callback = false);
    bool set_reference_index(int new_index);
    bool select_index(int index);
    bool swap_current_selected_with_previous() { return is_valid(m_previous) ? set_current_index(m_previous) : false; }
    bool move_item_to(int index1, int index2);
    bool swap_items(int index1, int index2);
    bool send_item_backward();
    bool bring_item_forward();

    // Loading, saving, closing, and rearranging the items in the list
    bool remove_current_item();
    void clear_items();

    bool nth_item_is_visible(int n) const;
    int  next_visible_item(int index, EDirection direction) const;
    int  nth_visible_item_index(int n) const;

private:
    bool is_valid(int index) const { return index >= 0 && index < child_count(); }

    // std::vector<int> m_items;          ///< The loaded items
    int m_current   = -1; ///< The currently selected item
    int m_reference = -1; ///< The currently selected reference item
    int m_previous  = -1; ///< The previously selected item

    bool              m_dragging_item = false;
    Widget           *m_dragged_item  = nullptr;
    nanogui::Vector2i m_dragging_start_pos;
};