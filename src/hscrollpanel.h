//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>

NAMESPACE_BEGIN(nanogui)

/// Allows scrolling a widget that is too big to fit into a certain area.
class HScrollPanel : public Widget
{
public:
    HScrollPanel(Widget *parent, bool scrollbar_visible = false);

    bool scrollbar_visible(bool b) const { return m_scrollbar_visible; }
    void set_scrollbar_visible(bool b) { m_scrollbar_visible = b; }

    /// Return the current scroll amount as a value between 0 and 1. 0 means scrolled to the left and 1 to the right.
    float scroll() const { return m_scroll; }

    /// Set the scroll amount to a value between 0 and 1. 0 means scrolled to the left and 1 to the right.
    void set_scroll(float scroll) { m_scroll = scroll; }

    virtual void     perform_layout(NVGcontext *ctx) override;
    virtual Vector2i preferred_size(NVGcontext *ctx) const override;
    virtual bool     mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool     mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    virtual bool     scroll_event(const Vector2i &p, const Vector2f &rel) override;
    virtual void     draw(NVGcontext *ctx) override;

protected:
    bool  m_scrollbar_visible;
    int   m_child_preferred_width;
    float m_scroll;
    bool  m_update_layout;
};

NAMESPACE_END(nanogui)
