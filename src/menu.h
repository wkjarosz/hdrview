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

    std::pair<int, int> hotkey() const { return {m_modifiers, m_button}; }
    void                set_hotkey(int modifiers, int button);

    virtual void     draw(NVGcontext *ctx) override;
    Vector2i         preferred_text_size(NVGcontext *ctx) const;
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;

protected:
    int         m_button, m_modifiers;
    std::string m_hotkey;
};

class Separator : public MenuItem
{
public:
    Separator(Widget *parent);
    virtual void draw(NVGcontext *ctx) override;
};

/// The actual popup window containing the menu
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
    Vector2i compute_position() const;

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

    bool process_hotkeys(int modifiers, int key);

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
