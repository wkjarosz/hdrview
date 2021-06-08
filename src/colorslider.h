//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/slider.h>

NAMESPACE_BEGIN(nanogui)

/**
 * \brief A specialization of a nanogui slider intended for color component sliders.
 *
 * Given the current color, and a color component index, displays a color gradient in
 * the slider bar that shows what the color would change to if the slider is adjusted.
 * So far supports RGBA, and exposure
 */
class ColorSlider : public Slider
{
public:
    enum ColorMode : uint8_t
    {
        RED = 0,
        GREEN,
        BLUE,
        ALPHA,
        EXPOSURE,
    };

    ColorSlider(Widget *parent, const Color &c = Color(0.8f, 1.f), ColorMode m = RED);

    const Color &color() const { return m_color; }
    void         set_color(const Color &c)
    {
        m_color = c;
        m_value = (m_mode <= ALPHA) ? m_color[(int)m_mode] : m_value;
    }

    virtual Vector2i preferred_size(NVGcontext *ctx) const override;
    virtual bool     mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    virtual bool     mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    virtual void     draw(NVGcontext *ctx) override;

protected:
    Color     m_color;
    ColorMode m_mode;
};

NAMESPACE_END(nanogui)
