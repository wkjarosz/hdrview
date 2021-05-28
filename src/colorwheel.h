//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>

NAMESPACE_BEGIN(nanogui)

/**
 * \brief Based off the nanogui ColorWheel class, but enhanced to offer:
 *        alpha controls, and preset buttons for white, black, opaque, and transparent. 
 */
class ColorWheel2 : public Widget {
public:

    enum Components : uint32_t
    {
        WHEEL       = 1 << 0,

        PATCH       = 1 << 1,

        WHITE       = 1 << 2,
        BLACK       = 1 << 3,
        OPAQUE      = 1 << 4,
        TRANS       = 1 << 5,
        ALL_CORNERS = WHITE | BLACK | OPAQUE | TRANS,

        ALL         = WHEEL | PATCH | ALL_CORNERS
    };

    /**
     * Adds a ColorWheel to the specified parent.
     *
     * \param parent
     *     The Widget to add this ColorWheel to.
     *
     * \param color
     *     The initial color of the ColorWheel (default: Red).
     * 
     * \param comp
     *     The components to display (bitwise or of \ref nanogui::ColorWheel2::Components)
     */
    ColorWheel2(Widget *parent, const Color& color = Color(1.0f, 0.0f, 0.0f, 1.0f), int comp = ALL);

    /// The callback to execute when a user changes the ColorWheel value.
    std::function<void(const Color &)> callback() const                  { return m_callback; }

    /// Sets the callback to execute when a user changes the ColorWheel value.
    void set_callback(const std::function<void(const Color &)> &callback) { m_callback = callback; }

    /// The current Color this ColorWheel has selected.
    Color color() const;

    /// Sets the current Color this ColorWheel has selected.
    void set_color(const Color& color);

    /// The preferred size of this ColorWheel.
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;

    /// Draws the ColorWheel.
    virtual void draw(NVGcontext *ctx) override;

    /// Handles mouse button click events for the ColorWheel.
    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

    /// Handles mouse drag events for the ColorWheel.
    virtual bool mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;

    /// Handle a mouse motion event (default implementation: propagate to children)
    virtual bool mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
private:
    // Used to describe where the mouse is interacting
    enum Region {
        None        = 0,
        InnerPatch  = 1 << 1,
        HueCircle   = 1 << 2,
        TLCircle    = 1 << 3,
        BLCircle    = 1 << 4,
        BRCircle    = 1 << 5,
        TRCircle    = 1 << 6,
        Circles     = TLCircle | BLCircle | BRCircle | TRCircle,
        All         = InnerPatch | HueCircle | Circles
    };

    // Converts a specified hue (with saturation = value = 1) to RGB space.
    Color hue2rgb(float h) const;

    // Manipulates the positioning of the different regions of the ColorWheel.
    Region adjust_position(const Vector2i &p, Region considered_regions = All, bool adjust = true);

protected:
    /// The current hue in the HSV color model.
    float m_hue;

    /**
     * The 'V' (value) component of the HSV color model.  See implementation \ref
     * nanogui::ColorWheel::color for its usage.  Valid values are in the range
     * ``[0, 1]``.
     */
    float m_value;

    /**
     * The 'S' (satration) component of the HSV color model.  See implementation
     * \ref nanogui::ColorWheel::color for its usage.  Valid values are in the
     * range ``[0, 1]``.
     */
    float m_saturation;

    float m_alpha;

    /// The current region the mouse is interacting with.
    Region m_drag_region;

    /// The current callback to execute when the color value has changed.
    std::function<void(const Color &)> m_callback;

    int m_visible_componets;
};

NAMESPACE_END(nanogui)
