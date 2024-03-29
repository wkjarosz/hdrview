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
    HDRViewScreen(bool capability_10bit, bool capability_EDR, const nlohmann::json &settings,
                  std::vector<std::string> args);
    virtual ~HDRViewScreen() override;

    void ask_to_quit();

    void                  write_settings();
    const nlohmann::json &settings() const { return m_settings; }
    nlohmann::json       &settings() { return m_settings; }

    // overridden virtual functions from Screen
    void draw_contents() override;
    void draw_all() override;
    bool drop_event(const std::vector<std::string> &filenames) override;
    bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    bool mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    bool keyboard_event(int key, int scancode, int action, int modifiers) override;

    ETool tool() const { return m_tool; }
    void  set_tool(ETool t, bool show = true);

    MenuBar       *menubar() { return m_menubar; }
    const MenuBar *menubar() const { return m_menubar; }

    const HDRColorPicker *active_colorpicker() const { return m_active_colorpicker; }
    void                  set_active_colorpicker(HDRColorPicker *cp);

    const HDRColorPicker *foreground() const { return m_color_btns->foreground(); }
    HDRColorPicker       *foreground() { return m_color_btns->foreground(); }
    const HDRColorPicker *background() const { return m_color_btns->background(); }
    HDRColorPicker       *background() { return m_color_btns->background(); }

    bool load_image();
    void new_image();
    void duplicate_image();
    void save_image_as();
    void save_image();
    void ask_close_image(int index);
    void ask_close_all_images();
    void remove_from_recent_files(const std::string &filename);
    void repopulate_recent_files_menu()
    {
        if (m_repopulate_recent_files_menu)
            m_repopulate_recent_files_menu();
    }
    void clear_focus_path() { m_focus_path.clear(); }

    void push_gui_refresh() { ++m_gui_refresh; }
    void pop_gui_refresh() { --m_gui_refresh; }
    bool should_refresh_gui() const { return m_gui_refresh > 0; }

    void request_layout_update() { m_need_layout_update = true; }

    void update_caption();

private:
    void            create_menubar();
    void            draw_widgets();
    void            bring_to_focus() const;
    void            show_help_window();
    void            update_layout();
    bool            at_side_panel_edge(const Vector2i &p);
    bool            at_tool_panel_edge(const Vector2i &p);
    void            resize_side_panel(int w);
    void            resize_tool_panel(int w);
    Dialog         *active_dialog() const;
    nlohmann::json &recent_files();

    Window         *m_top_panel = nullptr, *m_side_panel = nullptr, *m_tool_panel = nullptr, *m_status_bar = nullptr;
    HDRImageView   *m_image_view   = nullptr;
    ImageListPanel *m_images_panel = nullptr;
    MenuBar        *m_menubar      = nullptr;

    Label              *m_zoom_label         = nullptr;
    Label              *m_status_label       = nullptr;
    Label              *m_color32_info_label = nullptr;
    Label              *m_color8_info_label  = nullptr;
    Label              *m_ruler_info_label   = nullptr;
    Label              *m_pixel_info_label   = nullptr;
    Label              *m_roi_info_label     = nullptr;
    AlignedLabel       *m_stats_label1       = nullptr;
    AlignedLabel       *m_stats_label2       = nullptr;
    AlignedLabel       *m_stats_label3       = nullptr;
    DualHDRColorPicker *m_color_btns         = nullptr;

    VScrollPanel                         *m_side_scroll_panel   = nullptr;
    Widget                               *m_side_panel_contents = nullptr;
    std::vector<Button *>                 m_panel_btns;
    std::vector<MenuItem *>               m_menu_items;   /// All menu items
    std::vector<std::vector<std::string>> m_menu_aliases; /// Alternate names for menu item commands
    std::vector<MenuItem *>               m_edit_items;   /// Menu items that are enabled iff we have an editable image

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
    HDRColorPicker     *m_active_colorpicker = nullptr;

    bool m_dragging_side_panel = false;
    bool m_dragging_tool_panel = false;
    bool m_need_layout_update  = true;
    bool m_solo_mode           = false;
    bool m_capability_EDR      = false;
    bool m_capability_10bit    = false;

    std::thread       m_gui_refresh_thread;
    std::atomic<int>  m_gui_refresh    = 0;
    std::atomic<bool> m_join_requested = false;

    std::function<void(void)> m_repopulate_recent_files_menu;

    nlohmann::json m_settings;

    Vector2i m_roi_clicked;

    HDRImagePtr m_clipboard;
};
