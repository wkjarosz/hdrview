//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <nanogui/button.h>
#include <nanogui/popup.h>

/**
    Represents a key press optionally combined with one or more modifier keys.
    A Shortcut also stores a human-readable #text string describing the key combination for use by UI elements.
*/
struct Shortcut
{
    int         modifiers, key; ///< The GLFW modifiers (shift, command, etc) and key used to execute this shortcut
    std::string text;           ///< Human-readable string auto-generated from the above

    /// Construct a shortcut from a GLFW modifier and key code combination
    Shortcut(int m = 0, int k = 0);

    bool operator==(const Shortcut &rhs) const { return modifiers == rhs.modifiers && key == rhs.key; }
    bool operator!=(const Shortcut &rhs) const { return modifiers != rhs.modifiers || key != rhs.key; }
    bool operator<(const Shortcut &rhs) const
    {
        return modifiers < rhs.modifiers || (modifiers == rhs.modifiers && key < rhs.key);
    }

    //! Takes a fmt-format string and replaces any instances of {CMD} and {ALT} with CMD and ALT.
    static std::string key_string(const std::string &text);
};

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
class MenuItem : public Button
{
public:
    MenuItem(Widget *parent, const std::string &caption = "Untitled", int button_icon = 0,
             const std::vector<Shortcut> &s = {{0, 0}});

    size_t                       num_shortcuts() const { return m_shortcuts.size(); }
    const Shortcut              &shortcut(size_t i = 0) const { return m_shortcuts.at(i); }
    const std::vector<Shortcut> &shortcuts() const { return m_shortcuts; }

    bool mouse_enter_event(const Vector2i &p, bool enter) override;

    /// Whether or not this MenuItem is currently highlighted.
    bool highlighted() const { return m_highlighted; }
    /// Sets whether or not this MenuItem is currently highlighted.
    void set_highlighted(bool highlight, bool unhighlight_others = false, bool run_callbacks = false);

    /// Return the highlight callback
    std::function<void(bool)> highlight_callback() const { return m_highlight_callback; }
    /// Set the highlight callback
    void set_highlight_callback(const std::function<void(bool)> &callback) { m_highlight_callback = callback; }

    virtual void     draw(NVGcontext *ctx) override;
    Vector2i         preferred_text_size(NVGcontext *ctx) const;
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;

protected:
    std::vector<Shortcut> m_shortcuts;

    /// Whether or not this MenuItem is currently highlighted.
    bool m_highlighted = false;

    /// The callback issued for all types of buttons.
    std::function<void(bool)> m_highlight_callback;
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

    /// Return the menu item specified by menu_path.
    MenuItem *find_item(const std::vector<std::string> &menu_path, bool throw_on_fail = true) const;

    bool process_shortcuts(int modifiers, int key);

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
