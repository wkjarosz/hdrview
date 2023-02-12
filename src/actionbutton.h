//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This class is based off of the NanoGUI ActionButton class
//

#pragma once

#include "action.h"
#include <nanogui/widget.h>

NAMESPACE_BEGIN(nanogui)
/**
    Like #nanogui::Button, but internally using an Action to maintain state
*/
class ActionButton : public ActionWidget
{
public:
    /// Flags to specify the button behavior (can be combined with binary OR)
    enum Flags
    {
        NormalButton = (1 << 0), ///< A normal button.
        ToggleButton = (1 << 1), ///< A toggle button.
    };

    /// The available icon positions.
    enum class IconPosition
    {
        Left,          ///< ActionButton icon on the far left.
        LeftCentered,  ///< ActionButton icon on the left, centered (depends on caption text length).
        RightCentered, ///< ActionButton icon on the right, centered (depends on caption text length).
        Right          ///< ActionButton icon on the far right.
    };

    /**
     * \brief Creates a button attached to the specified parent.
     *
     * \param parent
     *     The #nanogui::Widget this ActionButton will be attached to.
     *
     * \param action
     *     The #nanogui::Action this ActionButton is associated with
     */
    ActionButton(Widget *parent, Action *action = nullptr);

    /// Returns the caption of this ActionButton.
    const std::string &caption() const { return m_action->text(); }

    /// Returns the background color of this ActionButton.
    const Color &background_color() const { return m_background_color; }
    /// Sets the background color of this ActionButton.
    void set_background_color(const Color &background_color) { m_background_color = background_color; }

    /// Returns the text color of the caption of this ActionButton.
    const Color &text_color() const { return m_text_color; }
    /// Sets the text color of the caption of this ActionButton.
    void set_text_color(const Color &text_color) { m_text_color = text_color; }

    /// Returns the icon of this ActionButton.
    int icon() const { return m_action->icon(); }

    /// The current flags of this ActionButton (see \ref nanogui::ActionButton::Flags for options).
    int flags() const { return m_flags; }
    /// Sets the flags of this ActionButton (see \ref nanogui::ActionButton::Flags for options).
    void set_flags(int button_flags) { m_flags = button_flags; }

    /// The position of the icon for this ActionButton.
    IconPosition icon_position() const { return m_icon_position; }
    /// Sets the position of the icon for this ActionButton.
    void set_icon_position(IconPosition icon_position) { m_icon_position = icon_position; }

    /// The preferred size of this ActionButton.
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;
    /// The callback that is called when any type of mouse button event is issued to this ActionButton.
    virtual bool mouse_enter_event(const Vector2i &p, bool enter) override;
    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    /// Responsible for drawing the ActionButton.
    virtual void draw(NVGcontext *ctx) override;

protected:
    /// The position to draw the icon at.
    IconPosition m_icon_position;

    /// The current flags of this button (see \ref nanogui::ActionButton::Flags for options).
    int m_flags;

    /// The background color of this ActionButton.
    Color m_background_color;

    /// The color of the caption text of this ActionButton.
    Color m_text_color;
};

NAMESPACE_END(nanogui)
