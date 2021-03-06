//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This file is adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include <nanogui/widget.h>
#include <string>

NAMESPACE_BEGIN(nanogui)

class ImageButton : public Widget
{
public:
    using IntCallback = std::function<void(int)>;

    /// How to align the text in the button.
    enum class Alignment
    {
        Left,
        Right
    };

    ImageButton(Widget *parent, const std::string &caption);

    Vector2i preferred_size(NVGcontext *ctx) const override;
    bool     mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    void     draw(NVGcontext *ctx) override;

    float progress() { return m_progress; }
    void  set_progress(float progress) { m_progress = progress; }

    Alignment alignment() const { return m_alignment; }
    void      set_alignment(Alignment align) { m_alignment = align; }

    /// Set the button's text caption/filename
    void               set_caption(const std::string &caption) { m_caption = caption; }
    const std::string &caption() const { return m_caption; }

    void   set_image_id(size_t id) { m_id = id; }
    size_t image_id() const { return m_id; }

    void set_is_modified(bool b) { m_is_modified = b; }
    bool is_modified() const { return m_is_modified; }

    bool is_current() const { return m_is_current; }
    void set_is_current(bool is_current)
    {
        m_is_current = is_current;
        if (m_is_current)
            m_is_selected = true;
    }

    bool is_selected() const { return m_is_selected; }
    void set_is_selected(bool is_selected)
    {
        m_is_selected = is_selected;
        if (!m_is_selected)
            m_is_current = false;
    }

    bool is_reference() const { return m_is_reference; }
    void set_is_reference(bool is_reference) { m_is_reference = is_reference; }

    std::string highlighted() const;
    void        set_highlight_range(size_t begin, size_t end);

    void set_hide_unhighlighted(bool h) { m_hide_unhighlighted = h; }
    bool hide_unhighlighted() const { return m_hide_unhighlighted; }

    void set_current_callback(const IntCallback &callback) { m_current_callback = callback; }
    void set_selected_callback(const IntCallback &callback) { m_selected_callback = callback; }
    void set_reference_callback(const IntCallback &callback) { m_reference_callback = callback; }

private:
    std::string m_caption;

    Alignment   m_alignment    = Alignment::Right;
    bool        m_is_modified  = false;
    bool        m_is_current   = false;
    bool        m_is_selected  = false;
    bool        m_is_reference = false;
    IntCallback m_current_callback;
    IntCallback m_selected_callback;
    IntCallback m_reference_callback;

    size_t m_id = 0;

    size_t m_highlight_begin    = 0;
    size_t m_highlight_end      = 0;
    bool   m_hide_unhighlighted = false;

    float m_progress = -1.f;
};

NAMESPACE_END(nanogui)
