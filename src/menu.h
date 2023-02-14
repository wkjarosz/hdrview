//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "action.h"
#include "actionbutton.h"
#include "fwd.h"
#include <nanogui/button.h>
#include <nanogui/popup.h>

NAMESPACE_BEGIN(nanogui)

/**
    A #MenuItem can have one or more keyboard #Shortcuts which can be used to run the callback associated with the item.
    These callbacks are run by #MenuBar::process_shortcuts for all #MenuItems associated with a #MenuBar.

    If an item has more than one shortcut, the first one is the default one that is shown on the drawn UI (for instance,
    along the right side of a dropdown menu). Since each shortcut can only represent a single key (plus modifiers), it
    is sometimes useful to associate multiple keyboard shortcuts with the same menu item (e.g. to allow zooming with the
    '+' key on the number row of the keyboard, as well as the '+' on the number pad).

    These additional shortcuts are not currently visible directly in the UI. In the future, the plan is to also allow
    the drawn UI to display alternate shortcuts based on what modifiers are currently being pressed (e.g. show "Close"
    when only the command key is pressed, but "Close all" when the shift key is also pressed).
*/
class MenuItem : public ActionButton
{
public:
    MenuItem(Widget *parent, Action *action = nullptr);

    size_t          num_shortcuts() const { return m_action->shortcuts().size(); }
    const Shortcut &shortcut(size_t i = 0) const { return m_action->shortcuts().at(i); }
    // void            add_shortcut(const Shortcut &s);

    virtual void     draw(NVGcontext *ctx) override;
    Vector2i         preferred_text_size(NVGcontext *ctx) const;
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;
};

class Separator : public MenuItem
{
public:
    Separator(Widget *parent);
    virtual void draw(NVGcontext *ctx) override;
};

/// The popup window containing the menu
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

/// A ComboBox or Menubar menu
class Dropdown : public MenuItem
{
public:
    enum Mode : int
    {
        ComboBox,
        Menu
    };

    /// Create an empty combo box
    Dropdown(Widget *parent, Mode mode = ComboBox, const std::string &caption = "Untitled", Action *action = nullptr);

    /// Create a new combo box with the given items, providing names and icons for each item
    Dropdown(Widget *parent, const std::vector<std::string> &items, const std::vector<int> &icons = {},
             Mode mode = ComboBox, const std::string &caption = "Untitled", Action *action = nullptr);

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

    /// Sets the items for this Dropdown from an array of actions
    void set_items(const ActionGroup &items);

    virtual Vector2i preferred_size(NVGcontext *ctx) const override;

    virtual void draw(NVGcontext *ctx) override;

    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

    /// Returns the idx-th item in the menu
    MenuItem *item(int idx) const;

protected:
    void update_popup_geometry() const;

    PopupMenu *m_popup;

    /// The callback for this Dropdown.
    std::function<void(int)> m_selected_callback;

    /// The current index this Dropdown has selected.
    int m_selected_index;

    Mode m_mode = ComboBox;
};

/// A horizontal menu bar containing a row of Dropdown menu items and responsible for handling their hotkeys
class MenuBar : public Window
{
public:
    MenuBar(Widget *parent, const std::string &title = "Untitled");

    Dropdown *add_menu(const std::string &name);

    bool process_shortcuts(int modifiers, int key);
    void add_shortcuts(HelpWindow *w);

    virtual bool mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
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
