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

    /// Returns the caption of this button.
    virtual const std::string &caption() const { return m_override_caption ? m_caption : m_action->text(); }
    /// Sets the caption of this button.
    void set_caption(const std::string &caption)
    {
        m_caption          = caption;
        m_override_caption = true;
    }
    /// Sets the caption back to the text of the associated action.
    void reset_caption() { m_override_caption = false; }

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
    bool        m_override_caption = false; ///< Whether to use the text from the action, or a custom caption.
    std::string m_caption;                  ///< The overriding caption text.

    /// The position to draw the icon at.
    IconPosition m_icon_position{IconPosition::LeftCentered};

    /// Whether or not this button is currently pressed by the mouse.
    bool m_pressed{false};

    /// The current flags of this button (see \ref nanogui::ActionButton::Flags for options).
    // int m_flags{NormalButton};

    /// The background color of this ActionButton.
    Color m_background_color{0, 0};

    /// The color of the caption text of this ActionButton.
    Color m_text_color{0, 0};
};

NAMESPACE_END(nanogui)
