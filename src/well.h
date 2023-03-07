//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/label.h>
#include <nanogui/widget.h>

NAMESPACE_BEGIN(nanogui)
class Well : public Widget
{
public:
    Well(Widget *parent, float radius = 3.0f, const Color &inner = Color(0, 32), const Color &outer = Color(0, 92),
         const Color &border = Color(25, 255));

    /// Return the inner well color
    const Color &inner_color() const { return m_inner_color; }
    /// Set the inner well color
    void set_inner_color(const Color &inner_color) { m_inner_color = inner_color; }

    /// Return the outer well color
    const Color &outer_color() const { return m_outer_color; }
    /// Set the outer well color
    void set_outer_color(const Color &outer_color) { m_outer_color = outer_color; }

    void draw(NVGcontext *ctx) override;

protected:
    float m_radius;
    Color m_inner_color, m_outer_color, m_border_color;
};

/// Like Label, but allows alignment
class AlignedLabel : public Label
{
public:
    /// How to align the text.
    enum class Alignment
    {
        Left,
        Center,
        Right
    };

    AlignedLabel(Widget *parent, const std::string &caption, const std::string &font = "sans", int font_size = -1);

    Alignment alignment() const { return m_alignment; }
    void      set_alignment(Alignment align) { m_alignment = align; }

    /// Draw the label
    virtual void draw(NVGcontext *ctx) override;

protected:
    Alignment m_alignment;
};

NAMESPACE_END(nanogui)