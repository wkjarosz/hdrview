//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorwheel.h"
#include <nanogui/popupbutton.h>
#include <nanogui/toolbutton.h>

NAMESPACE_BEGIN(nanogui)

/**
 * \brief Based off the nanogui ColorPicker class, but enhanced to offer exposure and alpha controls.
 */
class HDRColorPicker : public PopupButton
{
public:
    using ColorCallback = std::function<void(const Color &, float)>;
    using VoidCallback  = std::function<void(void)>;
    using BoolCallback  = std::function<void(bool)>;

    enum Components : uint32_t
    {
        R_SLIDER    = ColorWheel2::TRANS_CORNER << 1,
        G_SLIDER    = R_SLIDER << 2,
        B_SLIDER    = R_SLIDER << 3,
        A_SLIDER    = R_SLIDER << 4,
        E_SLIDER    = R_SLIDER << 5,
        ALL_SLIDERS = R_SLIDER | G_SLIDER | B_SLIDER | A_SLIDER | E_SLIDER,
        R_BOX       = R_SLIDER << 6,
        G_BOX       = R_SLIDER << 7,
        B_BOX       = R_SLIDER << 8,
        A_BOX       = R_SLIDER << 9,
        E_BOX       = R_SLIDER << 10,
        ALL_BOXES   = R_BOX | G_BOX | B_BOX | A_BOX | E_BOX,
        RESET_BTN   = R_SLIDER << 11,
        EYEDROPPER  = R_SLIDER << 12,
        ALL         = ColorWheel2::ALL | ALL_SLIDERS | ALL_BOXES | RESET_BTN | EYEDROPPER
    };

    /**
     * Attaches a HDRColorPicker to the specified parent.
     *
     * \param parent
     *     The Widget to add this HDRColorPicker to.
     *
     * \param color
     *     The color initially selected by this HDRColorPicker (default: Red).
     */

    HDRColorPicker(Widget *parent, const Color &color = Color(1.f, 0.f, 0.f, 1.f), float exposure = 0.f,
                   int comp = ALL);

    /// The callback executed when the ColorWheel changes.
    std::function<void(const Color &, float)> callback() const { return m_callback; }

    /**
     * Sets the callback that is executed as the ColorWheel itself is changed.  Set
     * this callback if you need to receive updates for the ColorWheel changing
     * before the user clicks \ref nanogui::HDRColorPicker::m_pick_button or
     * \ref nanogui::HDRColorPicker::m_pick_button.
     */
    void set_callback(const ColorCallback &cb)
    {
        m_callback = cb;
        m_callback(m_color, m_exposure);
    }

    /**
     * The callback to execute when a new Color is selected on the ColorWheel
     * **and** the user clicks the \ref nanogui::HDRColorPicker::m_pick_button or
     * \ref nanogui::HDRColorPicker::m_reset_button.
     */
    ColorCallback final_callback() const { return m_final_callback; }

    /**
     * The callback to execute when a new Color is selected on the ColorWheel
     * **and** the user clicks the \ref nanogui::HDRColorPicker::m_pick_button or
     * \ref nanogui::HDRColorPicker::m_reset_button.
     */
    void set_final_callback(const ColorCallback &cb) { m_final_callback = cb; }

    void set_eyedropper_callback(const BoolCallback &cb) { m_eyedropper->set_change_callback(cb); }
    void end_eyedropper()
    {
        m_eyedropper->set_pushed(false);
        m_eyedropper->change_callback()(false);
    }

    /// Get the current color
    Color color() const { return m_color; }
    /// Set the current color
    void set_color(const Color &color);

    /// Get the exposure
    float exposure() const { return m_exposure; }
    void  set_exposure(float e);

    /// Get the color boosted by the exposure value
    Color exposed_color() const;

    /// The current caption of the \ref nanogui::HDRColorPicker::m_pick_button.
    const std::string &pick_button_caption() { return m_pick_button->caption(); }

    /// Sets the current caption of the \ref nanogui::HDRColorPicker::m_pick_button.
    void set_pick_button_caption(const std::string &caption) { m_pick_button->set_caption(caption); }

    /// The current caption of the \ref nanogui::HDRColorPicker::m_reset_button.
    const std::string &reset_button_caption() { return m_reset_button->caption(); }

    /// Sets the current caption of the \ref nanogui::HDRColorPicker::m_reset_button.
    void set_reset_button_caption(const std::string &caption) { m_reset_button->set_caption(caption); }

protected:
    /// Update all internal color and exposure values and propagate to other widgets.
    void update_all(const Color &color, float exposure);

    /// Helper function to sync some of the widget values
    VoidCallback m_sync_helper;

    /// The "fast" callback executed when the ColorWheel has changed.
    ColorCallback m_callback;

    /**
     * The callback to execute when a new Color is selected on the ColorWheel
     * **and** the user clicks the \ref nanogui::HDRColorPicker::m_pick_button or
     * \ref nanogui::HDRColorPicker::m_reset_button.
     */
    ColorCallback m_final_callback;

    /// The ColorWheel for this HDRColorPicker (the actual widget allowing selection).
    ColorWheel2 *m_color_wheel;

    /**
     * The Button used to signal that the current value on the ColorWheel is the
     * desired color to be chosen.  The default value for the caption of this
     * Button is ``"Pick"``.  You can change it using
     * \ref nanogui::HDRColorPicker::set_pick_button_caption if you need.
     *
     * The color of this Button will not affect \ref nanogui::HDRColorPicker::color
     * until the user has actively selected by clicking this pick button.
     * Similarly, the \ref nanogui::HDRColorPicker::m_callback function is only
     * called when a user selects a new Color using by clicking this Button.
     */
    Button *m_pick_button;

    /**
     * Remains the Color of the active color selection, until the user picks a
     * new Color on the ColorWheel **and** selects the
     * \ref nanogui::HDRColorPicker::m_pick_button.  The default value for the
     * caption of this Button is ``"Reset"``.  You can change it using
     * \ref nanogui::HDRColorPicker::set_reset_button_caption if you need.
     */
    Button *m_reset_button;

    ToolButton *m_eyedropper;

    Color m_color, m_previous_color;
    float m_exposure, m_previous_exposure;
};

NAMESPACE_END(nanogui)
