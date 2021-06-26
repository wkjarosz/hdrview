//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include "hdrcolorpicker.h"
#include "hdrimageview.h"
#include <nanogui/nanogui.h>
#include <nanogui/renderpass.h>
#include <nanogui/shader.h>
#include <nanogui/texture.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

using namespace nanogui;

class HDRViewScreen : public Screen
{
public:
    HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, std::vector<std::string> args);
    virtual ~HDRViewScreen() override;

    void                  read_settings();
    void                  write_settings();
    const nlohmann::json &settings() const { return m_settings; }
    nlohmann::json &      settings() { return m_settings; }

    // overridden virtual functions from Screen
    void draw_contents() override;
    void draw_all() override;
    bool drop_event(const std::vector<std::string> &filenames) override;
    bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    bool mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    bool keyboard_event(int key, int scancode, int action, int modifiers) override;

    ETool tool() const { return m_tool; }
    void  set_tool(ETool t);

    const HDRColorPicker *active_colorpicker() const { return m_active_colorpicker; }
    void                  set_active_colorpicker(HDRColorPicker *cp);

    const HDRColorPicker *foreground() const { return m_color_btns->foreground(); }
    HDRColorPicker *      foreground() { return m_color_btns->foreground(); }
    const HDRColorPicker *background() const { return m_color_btns->background(); }
    HDRColorPicker *      background() { return m_color_btns->background(); }

    bool load_image();
    void new_image();
    void duplicate_image();
    void save_image();
    void ask_close_image(int index);
    void ask_close_all_images();
    void clear_focus_path() { m_focus_path.clear(); }

    void push_gui_refresh() { ++m_gui_refresh; }
    void pop_gui_refresh() { --m_gui_refresh; }
    bool should_refresh_gui() const { return m_gui_refresh > 0; }

    void request_layout_update() { m_need_layout_update = true; }

    void update_caption();

private:
    void draw_widgets();
    void bring_to_focus() const;
    void show_help_window();
    void update_layout();
    bool at_side_panel_edge(const Vector2i &p);
    bool at_tool_panel_edge(const Vector2i &p);
    void resize_side_panel(int w);
    void resize_tool_panel(int w);

    Window *        m_top_panel, *m_side_panel, *m_tool_panel, *m_status_bar;
    HDRImageView *  m_image_view;
    ImageListPanel *m_images_panel;
    EditImagePanel *m_edit_panel;

    Button *            m_help_button;
    Button *            m_side_panel_button;
    Label *             m_zoom_label;
    Label *             m_status_label;
    Label *             m_path_info_label;
    Label *             m_res_info_label;
    Label *             m_color32_info_label;
    Label *             m_color8_info_label;
    Label *             m_ruler_info_label;
    Label *             m_pixel_info_label;
    Label *             m_roi_info_label;
    Label *             m_stats_label;
    DualHDRColorPicker *m_color_btns;

    VScrollPanel *        m_side_scroll_panel;
    Widget *              m_side_panel_contents;
    std::vector<Button *> m_panel_btns;

    double m_gui_animation_start;
    bool   m_animation_running = false;
    enum EAnimationGoal : uint32_t
    {
        TOP_PANEL    = 1 << 0,
        SIDE_PANELS  = 1 << 1,
        BOTTOM_PANEL = 1 << 2,
    } m_animation_goal = EAnimationGoal(TOP_PANEL | SIDE_PANELS | BOTTOM_PANEL);

    ETool               m_tool = Tool_None;
    std::vector<Tool *> m_tools;
    HDRColorPicker *    m_active_colorpicker = nullptr;

    bool m_dragging_side_panel = false;
    bool m_dragging_tool_panel = false;
    bool m_need_layout_update  = true;
    bool m_solo_mode           = false;

    std::thread       m_gui_refresh_thread;
    std::atomic<int>  m_gui_refresh    = 0;
    std::atomic<bool> m_join_requested = false;

    nlohmann::json m_settings;

    Vector2i m_roi_clicked;
};
