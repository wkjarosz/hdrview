//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/label.h>

NAMESPACE_BEGIN(nanogui)

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