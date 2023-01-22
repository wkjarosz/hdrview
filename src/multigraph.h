//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "well.h"

NAMESPACE_BEGIN(nanogui)

/**
 * \class MultiGraph multigraph.h
 *
 * \brief A generalization of nanogui's graph widget which can plot multiple graphs on top of each other
 */
class MultiGraph : public Well
{
public:
    using DragCallback = std::function<void(const Vector2f &)>;

    MultiGraph(Widget *parent, const Color &fg = Color(255, 192, 0, 128),
               const std::vector<float> &v = std::vector<float>());

    const Color &background_color() const { return m_background_color; }
    void         set_background_color(const Color &background_color) { m_background_color = background_color; }

    const Color &text_color() const { return m_text_color; }
    void         set_text_color(const Color &text_color) { m_text_color = text_color; }

    int  num_plots() const { return m_values.size(); }
    void add_plot(const Color &fg = Color(), const std::vector<float> &v = std::vector<float>())
    {
        m_values.push_back(v);
        m_foreground_colors.push_back(fg);
    }
    void pop_plot()
    {
        m_values.pop_back();
        m_foreground_colors.pop_back();
    }

    void set_no_well() { m_inner_color = m_outer_color = m_border_color = Color(0, 0); }

    bool filled() const { return m_filled; }
    void set_filled(bool b) { m_filled = b; }

    const Color &foreground_color(int plot = 0) const { return m_foreground_colors[plot]; }
    void         set_foreground_color(const Color &foreground_color, int plot = 0)
    {
        m_foreground_colors[plot] = foreground_color;
    }

    const std::vector<float> &values(int plot = 0) const { return m_values[plot]; }
    std::vector<float>       &values(int plot = 0) { return m_values[plot]; }
    void                      set_values(const std::vector<float> &values, int plot = 0) { m_values[plot] = values; }

    void set_xticks(const std::vector<float> &ticks, const std::vector<std::string> &labels);
    void set_yticks(const std::vector<float> &ticks) { m_yticks = ticks; }
    void set_left_header(const std::string &s) { m_left_header = s; }
    void set_center_header(const std::string &s) { m_center_header = s; }
    void set_right_header(const std::string &s) { m_right_header = s; }

    DragCallback drag_callback() const { return m_drag_callback; }
    void         set_drag_callback(const DragCallback &cb) { m_drag_callback = cb; }

    virtual Vector2i preferred_size(NVGcontext *ctx) const override;
    virtual void     draw(NVGcontext *ctx) override;
    virtual bool     mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    virtual bool     mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;

protected:
    Vector2f graph_coordinate_at(const Vector2f &position) const;
    float    x_position(float xfrac) const;
    float    y_position(float yfrac) const;

    Color                           m_background_color, m_text_color;
    std::vector<Color>              m_foreground_colors;
    std::vector<std::vector<float>> m_values;
    bool                            m_filled = true;
    std::string                     m_left_header, m_center_header, m_right_header;
    std::vector<float>              m_xticks, m_yticks;
    std::vector<std::string>        m_xtick_labels;

    DragCallback m_drag_callback;
};

NAMESPACE_END(nanogui)