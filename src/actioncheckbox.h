//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This class is based off of the NanoGUI CheckBox class
//

#pragma once

#include "action.h"

NAMESPACE_BEGIN(nanogui)

/// Like #nanogui::CheckBox, but internally using an Action to maintain state
class ActionCheckBox : public ActionWidget
{
public:
    /**
     * Adds a CheckBox to the specified ``parent``.
     *
     * \param parent
     *     The Widget to add this CheckBox to.
     *
     * \param caption
     *     The caption text of the CheckBox (default ``"Untitled"``).
     *
     * \param callback
     *     If provided, the callback to execute when the CheckBox is checked or
     *     unchecked.  Default parameter function does nothing.  See
     *     \ref nanogui::CheckBox::mPushed for the difference between "pushed"
     *     and "checked".
     */
    ActionCheckBox(Widget *parent, Action *action = nullptr);

    /// Returns the caption of this ActionCheckBox.
    virtual const std::string &caption() const { return m_caption; }
    /// Sets the caption of this ActionCheckBox.
    void set_caption(const std::string &caption) { m_caption = caption; }

    /// Whether or not this ActionCheckBox is currently pushed.  See \ref nanogui::ActionCheckBox::m_pushed.
    const bool &pushed() const { return m_pushed; }
    void        set_pushed(const bool &pushed) { m_pushed = pushed; }

    /// Mouse button event processing for this check box
    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

    /// The preferred size of this CheckBox.
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;

    /// Draws this CheckBox.
    virtual void draw(NVGcontext *ctx) override;

protected:
    std::string m_caption; ///< The caption for this checkbox.

    /**
     * Internal tracking variable to distinguish between mouse click and release.
     * \ref nanogui::ActionCheckBox::m_callback is only called upon release.  See
     * \ref nanogui::ActionCheckBox::mouse_button_event for specific conditions.
     */
    bool m_pushed{false};
};

NAMESPACE_END(nanogui)
