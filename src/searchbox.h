//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// Adapted from Nanogui's TextBox class
//

#pragma once

#include <cstdio>
#include <nanogui/textbox.h>
#include <sstream>

NAMESPACE_BEGIN(nanogui)

/// Similar to nanogui's TextBox, but adapted to be better suited for interactively entering search/filter text
class SearchBox : public TextBox
{
public:
    SearchBox(Widget *parent, const std::string &value = "Untitled");

    const std::string &temporary_value() const { return m_value_temp; }
    void               set_temporary_value(const std::string &value) { m_value_temp = value; }

    /// The callback to execute when the temporary value of this SearchBox has changed.
    std::function<bool(const std::string &)> temporary_callback() const { return m_temporary_callback; }

    /// Sets the callback to execute when the temporary value of this SearchBox has changed.
    void set_temporary_callback(const std::function<bool(const std::string &)> &callback)
    {
        m_temporary_callback = callback;
    }

    bool committed() { return m_committed; }

    virtual bool focus_event(bool focused) override;
    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) override;
    virtual bool keyboard_character_event(unsigned int codepoint) override;

    virtual void draw(NVGcontext *ctx) override;

protected:
    std::function<bool(const std::string &)> m_temporary_callback;
};

NAMESPACE_END(nanogui)
