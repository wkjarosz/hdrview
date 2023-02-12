//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>

NAMESPACE_BEGIN(nanogui)

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

    //! Takes a fmt-format string and replaces any instances of {CMD} and {ALT} with CMD an ALT.
    static std::string key_string(const std::string &text);
};

class Action;

struct ActionGroup : public Object
{
    std::vector<Action *> actions;

    /// Add an action to this group; returns whether the action was already in the group.
    bool add(Action *action)
    {
        // check if the action is already in the group
        for (auto a : actions)
            if (a == action)
                return true;

        actions.push_back(action);
        return false;
    }
};

/// Actions allows using different (and multiple) Widgets to perform the same command and remain in sync.
/**
   Sometimes the same command may be presented or triggered by the user in multiple ways (e.g. as a menu item, a
   keyboard shortcut, and a toolbar button). An Action maintains the common state and callback functions that allows
   these widgets to remain in sync and to perform the command using the same code, regardless of the user interface
   used.
*/
class Action : public Object
{
public:
    /// Create an action with some text, and optionally an icon, action group, and keyboard shortcut.
    /**
        The text is used by associated Widgets, e.g. as the caption for buttons.
        By default, the text is also used as the Widget's tooltip, unless set separately by #set_tooltip().

        Set the action's group to group, and if it is not `nullptr`, then automatically insert it into the group.
    */
    Action(const std::string &text, int icon = 0, ActionGroup *group = nullptr,
           const std::vector<Shortcut> &shortcuts = {{0, 0}});

    /// Returns the text of this Action (used for e.g. the caption of Buttons).
    const std::string &text() const { return m_text; }
    /// Sets the text of this Action ( (used for e.g. the caption of Buttons).
    void set_text(const std::string &text) { m_text = text; }

    const std::string &tooltip() const { return m_tooltip; }
    void               set_tooltip(const std::string &tooltip) { m_tooltip = tooltip; }

    /// Returns the icon of this Action. See #nanogui::Action::m_icon.
    int icon() const { return m_icon; }
    /// Sets the icon of this Button. See #nanogui::Action::m_icon.
    void set_icon(int icon) { m_icon = icon; }

    /// Return the action group
    ActionGroup *group() { return m_group; }
    /// Return the action group
    const ActionGroup *group() const { return m_group; }
    /// Set this action's group to group and add this action to the group's list of actions.
    /**
        Checkable actions within a group are mutually exclusive (i.e. only a single action in a group can be #checked())

        If group is a nullptr, a new, empty ActionGroup will be created.
    */
    void set_group(ActionGroup *group)
    {
        m_group = group ? group : new ActionGroup{};
        m_group->add(this);
    }

    /// Return the list of keyboard shortcuts for this action
    const std::vector<Shortcut> &shortcuts() const { return m_shortcuts; }
    /// Return the list of keyboard shortcuts for this action
    std::vector<Shortcut> &shortcuts() { return m_shortcuts; }

    /// Whether this Action can be checked/unchecked
    bool checkable() const { return m_checkable; }
    /// Whether or not this Action is currently checked.
    bool checked() const { return m_checked; }
    /// Sets whether or not this Action is currently checked.
    void set_checked(bool checked)
    {
        if (m_checkable)
            m_checked = checked;
    }

    /// Trigger the action (run the associated callback, and update the state)
    void trigger();

    /// Return the function that is called when the action is triggered by the user
    std::function<void()> triggered_callback() const { return m_triggered_callback; }
    /// Set the function that is called when the action is triggered; for example when clicking a button,
    /// or pressing the keyboard shortcut
    void set_triggered_callback(const std::function<void()> &callback) { m_triggered_callback = callback; }

    /// Return the function to call whenever a checkable action changes its #checked() state
    std::function<void(bool)> toggled_callback() const { return m_toggled_callback; }
    /// Set the function to call whenever a checkable action changes its #checked() state
    void set_toggled_callback(const std::function<void(bool)> &callback) { m_toggled_callback = callback; }

protected:
    /// The text to use for Widgets using this Action, e.g. for Button::caption().
    std::string m_text;

    /// The tooltip to use for Widgets using this Action.
    std::string m_tooltip;

    /**
        \brief The icon of this Action (``0`` means no icon).

        \rst
        The icon to (optionally) display with any Widget associated with this Action.  If not ``0``, may either be a
        picture icon, or one of the icons enumerated in :ref:`file_nanogui_entypo.h`.  The kind of icon (image or
        Entypo) is determined by the functions :func:`nanogui::nvgIsImageIcon` and its reciprocal counterpart
        :func:`nanogui::nvgIsFontIcon`.
        \endrst
     */
    int m_icon = 0;

    /// The group this action belongs to.
    ActionGroup *m_group = nullptr;

    /// A list of (potentially several) keyboard shortcuts to trigger this action.
    std::vector<Shortcut> m_shortcuts;

    /// Whether or not this Action can be checked/toggled.
    bool m_checkable = false;

    /// Whether or not this Action is currently checked or unchecked.
    bool m_checked = false;

    /// The callback issued for all types of buttons.
    std::function<void()> m_triggered_callback;

    /// The function to execute when #nanogui::Action::m_checked changes.
    std::function<void(bool)> m_toggled_callback;
};

/// Base class for Widgets that maintain a shared Action state.
class ActionWidget : public Widget
{
public:
    /// Creates an actionable widget attached to the specified parent and action.
    ActionWidget(Widget *parent, Action *action = nullptr) : Widget(parent) { set_action(action); }

    /// Returns the action associated with this widget.
    Action *action() { return m_action; }
    /// Returns the action associated with this widget.
    const Action *action() const { return m_action; }
    /// Set the action associated with the widget, or create a new action if a nullptr is passed.
    void set_action(Action *action = nullptr) { m_action = action ? action : new Action{"Untitled"}; }

    /// Convenience function that returns the action's triggered callback
    std::function<void()> triggered_callback() const { return m_action->triggered_callback(); }
    /// Convenience function that sets the action's triggered callback
    void set_triggered_callback(const std::function<void()> &callback) { m_action->set_triggered_callback(callback); }

    /// Convenience function that returns the action's toggled callback (for #nanogui::Action::checkable() actions).
    std::function<void(bool)> toggled_callback() const { return m_action->toggled_callback(); }
    /// Convenience function that sets the action's toggled callback
    void set_toggled_callback(const std::function<void(bool)> &callback) { m_action->set_toggled_callback(callback); }

protected:
    /// The action associated with this ActionButton
    Action *m_action = nullptr;
};

NAMESPACE_END(nanogui)