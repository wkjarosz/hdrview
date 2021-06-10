//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include "hdrimageview.h"
#include <nanogui/nanogui.h>
#include <nanogui/renderpass.h>
#include <nanogui/shader.h>
#include <nanogui/texture.h>
#include <thread>
#include <vector>

using namespace nanogui;

class HDRViewScreen : public Screen
{
public:
    enum ETool : uint32_t
    {
        Tool_None = 0,
        Tool_Rectangular_Marquee,
        Tool_Num_Tools
    };

    HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, std::vector<std::string> args);
    ~HDRViewScreen() override;

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
    void toggle_help_window();
    void update_layout();
    bool at_side_panel_edge(const Vector2i &p);

    Window *        m_top_panel, *m_side_panel, *m_tool_panel, *m_status_bar;
    HDRImageView *  m_image_view;
    ImageListPanel *m_images_panel;
    EditImagePanel *m_edit_panel;

    Button *     m_help_button;
    Button *     m_side_panel_button;
    HelpWindow * m_help_window = nullptr;
    Label *      m_zoom_label;
    Label *      m_status_label;
    Label *      m_path_info_label;
    Label *      m_res_info_label;
    Label *      m_color32_info_label;
    Label *      m_color8_info_label;
    Label *      m_pixel_info_label;
    Label *      m_roi_info_label;
    Label *      m_stats_label;
    PopupButton *m_info_btn;

    VScrollPanel *m_side_scroll_panel;
    Widget *      m_side_panel_contents;

    double m_gui_animation_start;
    bool   m_animation_running = false;
    enum EAnimationGoal : uint32_t
    {
        TOP_PANEL    = 1 << 0,
        SIDE_PANEL   = 1 << 1,
        BOTTOM_PANEL = 1 << 2,
    } m_animation_goal = EAnimationGoal(TOP_PANEL | SIDE_PANEL | BOTTOM_PANEL);

    ETool                     m_tool = Tool_None;
    std::vector<ToolButton *> m_toolbuttons;
    HDRColorPicker *          m_active_colorpicker = nullptr;

    MessageDialog *m_ok_to_quit_dialog = nullptr;

    bool m_dragging_side_panel = false;
    bool m_need_layout_update  = true;

    std::thread      m_gui_refresh_thread;
    std::atomic<int> m_gui_refresh = 0;
};
