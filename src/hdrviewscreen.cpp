//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrviewscreen.h"
#include "commandhistory.h"
#include "common.h"
#include "dialog.h"
#include "filters/filters.h" // for create_bilateral_filter_btn, create_box...
#include "hdrcolorpicker.h"
#include "hdrimageview.h"
#include "helpwindow.h"
#include "imagelistpanel.h"
#include "menu.h"
#include "tool.h"
#include "xpuimage.h"
#include <filesystem/path.h>
#include <fstream>
#include <iostream>
#include <nanogui/opengl.h>
#include <spdlog/spdlog.h>
#include <thread>

#include <spdlog/fmt/ostr.h>

#include "json.h"

using std::cout;
using std::endl;
using std::exception;
using std::make_shared;
using std::string;
using std::vector;
using json = nlohmann::json;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(nanogui::Vector2i(800, 600), "HDRView", true, false, false, true, true, true), m_clipboard(nullptr)
{
    read_settings();

    set_background(Color(0.23f, 1.0f));

    auto theme                     = new Theme(m_nvg_context);
    theme->m_standard_font_size    = 16;
    theme->m_button_font_size      = 15;
    theme->m_text_box_font_size    = 14;
    theme->m_window_corner_radius  = 4;
    theme->m_window_fill_unfocused = Color(40, 250);
    theme->m_window_fill_focused   = Color(45, 250);
    set_theme(theme);

    auto panel_theme                       = new Theme(m_nvg_context);
    panel_theme->m_standard_font_size      = 16;
    panel_theme->m_button_font_size        = 15;
    panel_theme->m_text_box_font_size      = 14;
    panel_theme->m_window_corner_radius    = 0;
    panel_theme->m_window_fill_unfocused   = Color(50, 255);
    panel_theme->m_window_fill_focused     = Color(52, 255);
    panel_theme->m_window_header_height    = 0;
    panel_theme->m_window_drop_shadow_size = 0;

    auto flat_theme                             = new Theme(m_nvg_context);
    flat_theme->m_standard_font_size            = 16;
    flat_theme->m_button_font_size              = 15;
    flat_theme->m_text_box_font_size            = 14;
    flat_theme->m_window_corner_radius          = 0;
    flat_theme->m_window_fill_unfocused         = Color(50, 255);
    flat_theme->m_window_fill_focused           = Color(52, 255);
    flat_theme->m_window_header_height          = 0;
    flat_theme->m_window_drop_shadow_size       = 0;
    flat_theme->m_button_corner_radius          = 4;
    flat_theme->m_border_light                  = flat_theme->m_transparent;
    flat_theme->m_border_dark                   = flat_theme->m_transparent;
    flat_theme->m_button_gradient_top_focused   = flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_focused   = flat_theme->m_button_gradient_top_focused;
    flat_theme->m_button_gradient_top_unfocused = flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_unfocused = flat_theme->m_transparent;
    flat_theme->m_button_gradient_top_pushed    = flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_pushed    = flat_theme->m_button_gradient_top_pushed;

    //
    // Construct the top-level widgets
    //

    m_menubar = new MenuBar(this, "");

    m_top_panel = new Window(this, "");
    m_top_panel->set_theme(panel_theme);
    m_top_panel->set_position(nanogui::Vector2i(0, 0));
    m_top_panel->set_fixed_height(30);
    m_top_panel->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 5, 5));

    m_side_panel = new Window(this, "");
    m_side_panel->set_theme(panel_theme);

    m_tool_panel = new Window(this, "");
    m_tool_panel->set_theme(panel_theme);
    m_tool_panel->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 4, 20));
    auto tool_holder = m_tool_panel->add<Widget>();
    tool_holder->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 4));

    m_image_view = new HDRImageView(this, m_settings);

    m_status_bar = new Window(this, "");
    m_status_bar->set_theme(panel_theme);
    m_status_bar->set_fixed_height(m_status_bar->theme()->m_text_box_font_size + 1);

    //
    // create status bar widgets
    //

    m_status_label = new Label(m_status_bar, "");
    m_status_label->set_font_size(theme->m_text_box_font_size);
    m_status_label->set_position(nanogui::Vector2i(6, 0));

    m_zoom_label = new Label(m_status_bar, "100% (1 : 1)");
    m_zoom_label->set_font_size(theme->m_text_box_font_size);

    //
    // create side panel widgets
    //

    vector<Widget *>    panels;
    vector<PopupMenu *> popup_menus;
    m_side_scroll_panel   = new VScrollPanel(m_side_panel);
    m_side_panel_contents = new Widget(m_side_scroll_panel);
    m_side_panel_contents->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 4, 4));
    resize_side_panel(m_settings.value("geometry", json::object()).value("side panel width", 215));

    //
    // create file/images panel
    //

    popup_menus.push_back(new PopupMenu(this, m_side_panel));
    m_panel_btns.push_back(
        m_side_panel_contents->add<PopupWrapper>(popup_menus.back())->add<Button>("File", FA_CARET_DOWN));
    m_panel_btns.back()->set_theme(flat_theme);
    m_panel_btns.back()->set_flags(Button::ToggleButton);
    m_panel_btns.back()->set_pushed(false);
    m_panel_btns.back()->set_font_size(18);
    m_panel_btns.back()->set_icon_position(Button::IconPosition::Left);

    m_images_panel = new ImageListPanel(m_side_panel_contents, this, m_image_view);
    m_images_panel->set_visible(false);
    panels.push_back(m_images_panel);

    //
    // create info panel
    //

    popup_menus.push_back(new PopupMenu(this, m_side_panel));
    m_panel_btns.push_back(
        m_side_panel_contents->add<PopupWrapper>(popup_menus.back())->add<Button>("Info", FA_CARET_RIGHT));
    m_panel_btns.back()->set_theme(flat_theme);
    m_panel_btns.back()->set_flags(Button::ToggleButton);
    m_panel_btns.back()->set_pushed(false);
    m_panel_btns.back()->set_font_size(18);
    m_panel_btns.back()->set_icon_position(Button::IconPosition::Left);

    {
        auto info_panel = new Well(m_side_panel_contents, 1, Color(150, 32), Color(0, 50));
        info_panel->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 10, 5));

        auto row = new Widget(info_panel);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        auto tb = new ToolButton(row, FA_EYE_DROPPER);
        tb->set_theme(flat_theme);
        tb->set_enabled(false);
        tb->set_icon_extra_scale(1.5f);

        (new Label(row, "R:\nG:\nB:\nA:", "sans-bold"))->set_fixed_width(15);
        m_color32_info_label = new Label(row, "");
        m_color32_info_label->set_fixed_width(50 + 24 + 5);
        m_color8_info_label = new Label(row, "");
        m_color8_info_label->set_fixed_width(50);

        // spacer
        (new Widget(info_panel))->set_fixed_height(5);

        row = new Widget(info_panel);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        tb = new ToolButton(row, FA_CROSSHAIRS);
        tb->set_theme(flat_theme);
        tb->set_enabled(false);
        tb->set_icon_extra_scale(1.5f);

        (new Label(row, "X:\nY:", "sans-bold"))->set_fixed_width(15);
        m_pixel_info_label = new Label(row, "");
        m_pixel_info_label->set_fixed_width(50);

        tb = new ToolButton(row, FA_EXPAND);
        tb->set_theme(flat_theme);
        tb->set_enabled(false);
        tb->set_icon_extra_scale(1.5f);

        (new Label(row, "W:\nH:", "sans-bold"))->set_fixed_width(20);
        m_roi_info_label = new Label(row, "");
        m_roi_info_label->set_fixed_width(50);

        // spacer
        (new Widget(info_panel))->set_fixed_height(5);

        row = new Widget(info_panel);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        tb = new ToolButton(row, FA_PERCENTAGE);
        tb->set_theme(flat_theme);
        tb->set_enabled(false);
        tb->set_icon_extra_scale(1.5f);

        (new Label(row, "Min:\nAvg:\nMax:", "sans-bold"))->set_fixed_width(30);
        m_stats_label = new Label(row, "");
        m_stats_label->set_fixed_width(130);

        // spacer
        (new Widget(info_panel))->set_fixed_height(5);

        row = new Widget(info_panel);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        tb = new ToolButton(row, FA_RULER);
        tb->set_theme(flat_theme);
        tb->set_enabled(false);
        tb->set_icon_extra_scale(1.5f);

        (new Label(row, "°:\nΔ:", "sans-bold"))->set_fixed_width(20);
        m_ruler_info_label = new Label(row, "");
        m_ruler_info_label->set_fixed_width(50);

        info_panel->set_fixed_height(4);
        panels.push_back(info_panel);
    }

    //
    // panel callbacks
    //

    m_solo_mode = m_settings.value("side panel", json::object()).value("solo mode", false);

    auto toggle_panel = [this, panels](size_t index, bool value)
    {
        // expand of collapse the specified panel
        m_panel_btns[index]->set_icon(value ? FA_CARET_DOWN : FA_CARET_RIGHT);
        m_panel_btns[index]->set_pushed(value);
        panels[index]->set_fixed_height(value ? 0 : 4);
        panels[index]->set_visible(true);

        // close other panels if solo mode is on
        if (value && m_solo_mode)
        {
            for (size_t i = 0; i < panels.size(); ++i)
            {
                if (i == index)
                    continue;
                m_panel_btns[i]->set_pushed(false);
                m_panel_btns[i]->set_icon(FA_CARET_RIGHT);
                panels[i]->set_fixed_height(4);
            }
        }

        request_layout_update();
        m_side_panel_contents->perform_layout(m_nvg_context);
    };

    for (size_t i = 0; i < m_panel_btns.size(); ++i)
        m_panel_btns[i]->set_change_callback([i, toggle_panel](bool value) { toggle_panel(i, value); });

    //
    // create the right-click menus
    //
    for (auto m : popup_menus)
    {
        m->add<MenuItem>("Solo mode")->set_flags(Button::ToggleButton);
        m->add<Separator>();
        m->add<MenuItem>("Expand all", FA_ANGLE_DOUBLE_DOWN);
        m->add<MenuItem>("Collapse all", FA_ANGLE_DOUBLE_UP);
    }

    auto visible_panels =
        m_settings.value("side panel", json::object()).value("visible panels", json::array()).get<std::vector<int>>();
    for (size_t i = 0; i < popup_menus.size(); ++i)
    {
        auto item = dynamic_cast<MenuItem *>(popup_menus[i]->child_at(0));
        item->set_pushed(m_solo_mode);
        item->set_change_callback(
            [popup_menus, toggle_panel, i, this](bool b)
            {
                m_solo_mode = b;
                // update all "Solo mode" buttons and "Expand all" buttons
                for (size_t j = 0; j < popup_menus.size(); ++j)
                {
                    auto item = dynamic_cast<MenuItem *>(popup_menus[j]->child_at(0));
                    item->set_pushed(m_solo_mode);
                    popup_menus[j]->child_at(2)->set_enabled(!m_solo_mode);
                }

                toggle_panel(i, m_solo_mode ? true : m_panel_btns[i]->pushed());
            });

        popup_menus[i]->child_at(2)->set_enabled(!m_solo_mode);
        dynamic_cast<Button *>(popup_menus[i]->child_at(2))
            ->set_callback(
                [toggle_panel, popup_menus, i, this]
                {
                    // expand this panel, and all other panels if solo mode isn't on
                    for (size_t j = 0; j < popup_menus.size(); ++j)
                    {
                        if (j == i || !m_solo_mode)
                            toggle_panel(j, true);
                    }
                });

        dynamic_cast<Button *>(popup_menus[i]->child_at(3))
            ->set_callback(
                [toggle_panel, popup_menus]
                {
                    // collapse all panels
                    for (size_t j = 0; j < popup_menus.size(); ++j) toggle_panel(j, false);
                });

        bool b = find(visible_panels.begin(), visible_panels.end(), i) != visible_panels.end();
        m_panel_btns[i]->set_pushed(b);
        toggle_panel(i, b);
    }

    //
    // create tools
    //

    m_tools.push_back(new HandTool(this, m_image_view, m_images_panel));
    m_tools.push_back(new RectangularMarquee(this, m_image_view, m_images_panel));
    m_tools.push_back(new BrushTool(this, m_image_view, m_images_panel));
    m_tools.push_back(new EraserTool(this, m_image_view, m_images_panel));
    m_tools.push_back(new CloneStampTool(this, m_image_view, m_images_panel));
    m_tools.push_back(new Eyedropper(this, m_image_view, m_images_panel));
    m_tools.push_back(new Ruler(this, m_image_view, m_images_panel));
    m_tools.push_back(new LineTool(this, m_image_view, m_images_panel));

    for (auto t : m_tools) t->create_toolbutton(tool_holder);

    m_color_btns = new DualHDRColorPicker(m_tool_panel);
    m_color_btns->set_fixed_size(Vector2i(0));
    m_color_btns->foreground()->popup()->set_anchor_offset(100);
    m_color_btns->background()->popup()->set_anchor_offset(100);
    m_color_btns->foreground()->set_side(Popup::Left);
    m_color_btns->background()->set_side(Popup::Left);
    m_color_btns->foreground()->set_color(
        m_settings.value("color picker", json::object()).value("foreground", Color(0, 255)));
    m_color_btns->background()->set_color(
        m_settings.value("color picker", json::object()).value("background", Color(255, 255)));

    m_color_btns->foreground()->set_tooltip("Set foreground color.");
    m_color_btns->background()->set_tooltip("Set background color.");
    m_color_btns->foreground()->set_eyedropper_callback(
        [this](bool pushed) { set_active_colorpicker(pushed ? m_color_btns->foreground() : nullptr); });
    m_color_btns->background()->set_eyedropper_callback(
        [this](bool pushed) { set_active_colorpicker(pushed ? m_color_btns->background() : nullptr); });

    {
        // create default options bar
        m_tools[Tool_None]->create_options_bar(m_top_panel);
        auto default_tool = m_tools[Tool_None]->options_bar();

        // marquee tool uses the default options bar
        m_tools[Tool_Rectangular_Marquee]->set_options_bar(default_tool);

        // brush tool uses its own options bar
        m_tools[Tool_Brush]->create_options_bar(m_top_panel);

        // brush tool uses its own options bar
        m_tools[Tool_Eraser]->create_options_bar(m_top_panel);

        // clone stamp uses its own options bar
        m_tools[Tool_Clone_Stamp]->create_options_bar(m_top_panel);

        // eyedropper uses its own options bar
        m_tools[Tool_Eyedropper]->create_options_bar(m_top_panel);

        // ruler uses the default options bar
        m_tools[Tool_Ruler]->set_options_bar(default_tool);

        // line tool uses the default options bar
        m_tools[Tool_Line]->create_options_bar(m_top_panel);
    }

    resize_tool_panel(m_settings.value("geometry", json::object()).value("tool panel width", 33));

    m_image_view->set_zoom_callback(
        [this](float zoom)
        {
            float real_zoom = zoom * pixel_ratio();
            int   numer     = (real_zoom < 1.0f) ? 1 : (int)round(real_zoom);
            int   denom     = (real_zoom < 1.0f) ? (int)round(1.0f / real_zoom) : 1;
            m_zoom_label->set_caption(fmt::format("{:7.2f}% ({:d} : {:d})", real_zoom * 100, numer, denom));
            request_layout_update();
        });

    m_image_view->set_pixel_callback(
        [this](const nanogui::Vector2i &index, char **out, size_t size)
        {
            auto img = m_images_panel->current_image();
            if (img)
            {
                const HDRImage &image = img->image();
                for (int ch = 0; ch < 4; ++ch)
                {
                    float value = image(index.x(), index.y())[ch];
                    snprintf(out[ch], size, "%f", value);
                }
            }
        });

    m_image_view->set_mouse_callback([this](const Vector2i &p, int button, bool down, int modifiers) -> bool
                                     { return m_tools[m_tool]->mouse_button(p, button, down, modifiers); });

    m_image_view->set_drag_callback([this](const Vector2i &p, const Vector2i &rel, int button, int modifiers) -> bool
                                    { return m_tools[m_tool]->mouse_drag(p, rel, button, modifiers); });

    m_image_view->set_motion_callback(
        [this](const Vector2i &p, const Vector2i &rel, int button, int modifiers) -> bool
        {
            if (auto xpuimg = m_images_panel->current_image())
            {
                Vector2i        pixel(m_image_view->pixel_at_position((p - m_image_view->position())));
                const HDRImage &image = xpuimg->image();

                if (image.contains(pixel.x(), pixel.y()))
                {
                    Color4 color32 = image(pixel.x(), pixel.y());
                    Color4 color8  = (color32 * pow(2.f, m_image_view->exposure()) * 255).min(255.f).max(0.f);

                    m_status_label->set_caption(fmt::format(
                        "({: 4d},{: 4d}) = ({: 6.3f}, {: 6.3f}, {: 6.3f}, {: 6.3f}) / ({: 3d}, {: 3d}, {: 3d}, {: 3d})",
                        pixel.x(), pixel.y(), color32[0], color32[1], color32[2], color32[3], (int)round(color8[0]),
                        (int)round(color8[1]), (int)round(color8[2]), (int)round(color8[3])));

                    m_color32_info_label->set_caption(fmt::format("{: 6.3f}\n{: 6.3f}\n{: 6.3f}\n{: 6.3f}", color32[0],
                                                                  color32[1], color32[2], color32[3]));

                    m_color8_info_label->set_caption(fmt::format("{: >3d}\n{: >3d}\n{: >3d}\n{: >3d}",
                                                                 (int)round(color8[0]), (int)round(color8[1]),
                                                                 (int)round(color8[2]), (int)round(color8[3])));

                    m_pixel_info_label->set_caption(fmt::format("{: >4d}\n{: >4d}", pixel.x(), pixel.y()));

                    if (m_active_colorpicker)
                        m_active_colorpicker->set_color(Color(color32[0], color32[1], color32[2], color32[3]));

                    m_status_bar->perform_layout(m_nvg_context);

                    return true;
                }
            }

            m_status_label->set_caption("");
            m_color32_info_label->set_caption("");
            m_color8_info_label->set_caption("");
            m_pixel_info_label->set_caption("");

            m_status_bar->perform_layout(m_nvg_context);

            return true;
        });

    m_image_view->set_draw_callback([this](NVGcontext *ctx) { m_tools[m_tool]->draw(ctx); });

    m_image_view->set_changed_callback(
        [this]()
        {
            if (auto img = m_images_panel->current_image())
            {
                // perform_layout();
            }
            else
            {
                m_stats_label->set_caption("");
            }
        });

    {
        auto menu = m_menubar->add_menu("File");

        auto add_item = [this, &menu](const std::string &name, int icon, const std::function<void(void)> &cb,
                                      int modifier = 0, int button = 0, bool edit = true)
        {
            auto i = menu->popup()->add<MenuItem>(name, icon);
            i->set_hotkey(modifier, button);
            i->set_callback(cb);
            m_menu_items.push_back(i);
            if (edit)
                m_edit_items.push_back(i);
        };

        add_item(
            "New...", FA_FILE, [this] { new_image(); }, SYSTEM_COMMAND_MOD, 'N', false);
        add_item(
            "Open...", FA_FOLDER_OPEN, [this] { load_image(); }, SYSTEM_COMMAND_MOD, 'O', false);
        add_item(
            "Reload", FA_REDO, [this] { m_images_panel->reload_image(m_images_panel->current_image_index()); },
            SYSTEM_COMMAND_MOD, 'R');
        add_item(
            "Reload all", FA_REDO, [this] { m_images_panel->reload_all_images(); }, SYSTEM_COMMAND_MOD | GLFW_MOD_SHIFT,
            'R');
        menu->popup()->add<Separator>();
        add_item(
            "Duplicate image", FA_CLONE, [this] { duplicate_image(); }, GLFW_MOD_ALT | GLFW_MOD_SHIFT, 'D');
        add_item(
            "Save", FA_SAVE, [this] { save_image(); }, SYSTEM_COMMAND_MOD, 'S');
        add_item(
            "Save as...", FA_SAVE, [this] { save_image_as(); }, SYSTEM_COMMAND_MOD | GLFW_MOD_SHIFT, 'S');
        menu->popup()->add<Separator>();
        add_item(
            "Close", FA_TIMES_CIRCLE, [this] { ask_close_image(m_images_panel->current_image_index()); },
            SYSTEM_COMMAND_MOD, 'W');
        add_item(
            "Close all", FA_TIMES_CIRCLE, [this] { ask_close_all_images(); }, SYSTEM_COMMAND_MOD | GLFW_MOD_SHIFT, 'W');
        menu->popup()->add<Separator>();
        add_item(
            "Quit...", FA_POWER_OFF, [this] { ask_to_quit(); }, 0, GLFW_KEY_ESCAPE, false);

        menu = m_menubar->add_menu("Edit");

        add_item(
            "Undo", FA_REPLY, [this] { m_images_panel->undo(); }, SYSTEM_COMMAND_MOD, 'Z');
        add_item(
            "Redo", FA_SHARE, [this] { m_images_panel->redo(); }, SYSTEM_COMMAND_MOD | GLFW_MOD_SHIFT, 'Z');
        menu->popup()->add<Separator>();
        add_item("Cut", FA_CUT, cut_callback(m_clipboard, m_images_panel), SYSTEM_COMMAND_MOD, 'X');
        add_item("Copy", FA_COPY, copy_callback(m_clipboard, m_images_panel), SYSTEM_COMMAND_MOD, 'C');
        add_item("Paste", FA_PASTE, paste_callback(m_clipboard, m_images_panel), SYSTEM_COMMAND_MOD, 'V');
        add_item("Seamless paste", FA_PASTE, seamless_paste_callback(m_clipboard, m_images_panel),
                 SYSTEM_COMMAND_MOD | GLFW_MOD_SHIFT, 'V');
        menu->popup()->add<Separator>();
        add_item(
            "Select entire image", FA_EXPAND,
            [this]
            {
                if (auto img = m_images_panel->current_image())
                    img->roi() = img->box();
            },
            SYSTEM_COMMAND_MOD, 'A');
        add_item(
            "Deselect", 0,
            [this]
            {
                if (auto img = m_images_panel->current_image())
                    img->roi() = Box2i();
            },
            SYSTEM_COMMAND_MOD, 'D');

        menu = m_menubar->add_menu("Transform");

        add_item("Flip horizontally", FA_ARROWS_ALT_H, flip_callback(true, m_images_panel), GLFW_MOD_ALT, 'H');
        add_item("Flip vertically", FA_ARROWS_ALT_V, flip_callback(false, m_images_panel), GLFW_MOD_ALT, 'V');
        add_item("Rotate 90° clockwise", FA_REDO, rotate_callback(true, m_images_panel), SYSTEM_COMMAND_MOD, ']');
        add_item("Rotate 90° counter clockwise", FA_UNDO, rotate_callback(false, m_images_panel), SYSTEM_COMMAND_MOD,
                 '[');
        menu->popup()->add<Separator>();
        add_item("Crop to selection", FA_CROP, crop_callback(m_images_panel), GLFW_MOD_ALT, 'C');
        add_item("Image size...", FA_EXPAND, ::resize_callback(this, m_images_panel), GLFW_MOD_ALT | SYSTEM_COMMAND_MOD,
                 'I');
        add_item("Canvas size...", FA_COMPRESS, canvas_size_callback(this, m_images_panel),
                 GLFW_MOD_ALT | SYSTEM_COMMAND_MOD, 'C');
        menu->popup()->add<Separator>();
        add_item("Shift...", FA_ARROWS_ALT, shift_callback(this, m_images_panel));
        add_item("Remap...", FA_GLOBE_AMERICAS, remap_callback(this, m_images_panel), GLFW_MOD_ALT, 'M');
        add_item("Transform...", FA_CLONE, free_xform_callback(this, m_images_panel), SYSTEM_COMMAND_MOD, 'T');

        menu = m_menubar->add_menu("Pixels");

        add_item("Fill...", FA_FILL, fill_callback(this, m_images_panel), SYSTEM_COMMAND_MOD, GLFW_KEY_BACKSPACE);
        add_item("Invert", FA_IMAGE, invert_callback(m_images_panel), SYSTEM_COMMAND_MOD, 'I');
        add_item("Clamp to [0,1]", FA_ADJUST, clamp_callback(m_images_panel));
        add_item("Flatten...", FA_CHESS_BOARD, flatten_callback(this, m_images_panel));
        add_item("Flatten with bg color", FA_CHESS_BOARD, flatten_with_bg_callback(this, m_images_panel));
        add_item("Zap gremlins...", FA_SKULL_CROSSBONES, zap_gremlins_callback(this, m_images_panel));
        menu->popup()->add<Separator>();
        add_item("Exposure/gamma...", FA_ADJUST, exposure_gamma_callback(this, m_images_panel));
        add_item("Brightness/contrast...", FA_ADJUST, brightness_contrast_callback(this, m_images_panel));
        add_item("Filmic tonemapping...", FA_ADJUST, filmic_tonemapping_callback(this, m_images_panel));
        menu->popup()->add<Separator>();
        add_item("Channel mixer...", FA_BLENDER, channel_mixer_callback(this, m_images_panel));
        add_item("Hue/saturation...", FA_PALETTE, hsl_callback(this, m_images_panel));
        add_item("Convert color space...", FA_PALETTE, colorspace_callback(this, m_images_panel));

        menu = m_menubar->add_menu("Filter");

        add_item("Gaussian blur...", FA_TINT, gaussian_filter_callback(this, m_images_panel));
        add_item("Box blur...", FA_TINT, box_blur_callback(this, m_images_panel));
        add_item("Bilateral filter...", FA_TINT, bilateral_filter_callback(this, m_images_panel));
        add_item("Unsharp mask...", FA_TINT, unsharp_mask_filter_callback(this, m_images_panel));
        add_item("Median filter...", FA_TINT, median_filter_callback(this, m_images_panel));
        add_item("Bump to normal map", FA_ARROWS_ALT_H, bump_to_normal_map_callback(m_images_panel));
        add_item("Irradiance envmap", FA_GLOBE_AMERICAS, irradiance_envmap_callback(m_images_panel));

        menu = m_menubar->add_menu("Tools");

        auto tool_hotkeys = vector<int>{' ', 'M', 'B', 'E', 'S', 'I', 'R', 'U'};
        auto tool_hotmods = vector<int>{0, 0, 0, GLFW_MOD_ALT, 0, 0, GLFW_MOD_ALT, 0};
        for (int t = 0; t < (int)Tool_Num_Tools; ++t)
        {
            m_tools[t]->create_menuitem(menu, tool_hotmods[t], tool_hotkeys[t]);
        }
        menu->popup()->add<Separator>();
        add_item(
            "Swap FG/BG colors", 0, [this] { m_color_btns->swap_colors(); }, 0, 'X', false);
        add_item(
            "Default FG/BG colors", 0, [this] { m_color_btns->set_default_colors(); }, 0, 'D', false);

        menu = m_menubar->add_menu("View");

        add_item(
            "Show top toolbar", 0, [] {}, 0, 'T', false);
        auto show_top_panel = m_menu_items.back();

        add_item(
            "Show side panels", 0, [] {}, 0, GLFW_KEY_TAB, false);
        auto show_side_panels = m_menu_items.back();

        add_item(
            "Show all panels", 0, [] {}, GLFW_MOD_SHIFT, GLFW_KEY_TAB, false);
        auto show_all_panels = m_menu_items.back();

        show_top_panel->set_flags(Button::ToggleButton);
        show_top_panel->set_pushed(true);
        show_top_panel->set_change_callback(
            [this, show_top_panel, show_side_panels, show_all_panels](bool b)
            {
                m_gui_animation_start = glfwGetTime();
                push_gui_refresh();
                m_animation_running = true;
                m_animation_goal =
                    b ? EAnimationGoal(m_animation_goal | TOP_PANEL) : EAnimationGoal(m_animation_goal & ~TOP_PANEL);
                show_all_panels->set_pushed(show_side_panels->pushed() && show_top_panel->pushed());
                request_layout_update();
            });
        show_side_panels->set_flags(Button::ToggleButton);
        show_side_panels->set_pushed(true);
        show_side_panels->set_change_callback(
            [this, show_top_panel, show_side_panels, show_all_panels](bool b)
            {
                m_gui_animation_start = glfwGetTime();
                push_gui_refresh();
                m_animation_running = true;
                m_animation_goal    = b ? EAnimationGoal(m_animation_goal | SIDE_PANELS)
                                        : EAnimationGoal(m_animation_goal & ~SIDE_PANELS);
                show_all_panels->set_pushed(show_side_panels->pushed() && show_top_panel->pushed());
                request_layout_update();
            });
        show_all_panels->set_flags(Button::ToggleButton);
        show_all_panels->set_pushed(true);
        show_all_panels->set_change_callback(
            [this, show_top_panel, show_side_panels](bool b)
            {
                m_gui_animation_start = glfwGetTime();
                push_gui_refresh();
                m_animation_running = true;
                m_animation_goal    = b ? EAnimationGoal(TOP_PANEL | SIDE_PANELS | BOTTOM_PANEL) : EAnimationGoal(0);
                show_side_panels->set_pushed(b);
                show_top_panel->set_pushed(b);
                request_layout_update();
            });
        menu->popup()->add<Separator>();
        add_item(
            "Zoom in", 0, [this] { m_image_view->zoom_in(); }, 0, '+');
        add_item(
            "Zoom out", 0, [this] { m_image_view->zoom_out(); }, 0, '-');
        add_item("Fit to screen", 0,
                 [this]
                 {
                     m_image_view->center();
                     m_image_view->fit();
                 });
        menu->popup()->add<Separator>();
        add_item(
            "Increase exposure", 0, [this] { m_image_view->set_exposure(m_image_view->exposure() + 0.25f); },
            GLFW_MOD_SHIFT, 'E', false);
        add_item(
            "Decrease exposure", 0, [this] { m_image_view->set_exposure(m_image_view->exposure() - 0.25f); }, 0, 'E',
            false);
        add_item(
            "Normalize exposure", 0,
            [this]()
            {
                m_image_view->normalize_exposure();
                m_images_panel->request_histogram_update(true);
            },
            0, 'N', false);
        menu->popup()->add<Separator>();
        add_item(
            "Increase gamma", 0, [this] { m_image_view->set_gamma(std::max(0.02f, m_image_view->gamma() + 0.02f)); },
            GLFW_MOD_SHIFT, 'G', false);
        add_item(
            "Decrease gamma", 0, [this] { m_image_view->set_gamma(std::max(0.02f, m_image_view->gamma() - 0.02f)); }, 0,
            'G', false);
        menu->popup()->add<Separator>();
        add_item(
            "Reset tonemapping", 0,
            [this]()
            {
                m_image_view->reset_tonemapping();
                m_images_panel->request_histogram_update(true);
            },
            0, '0', false);
        if (has_float_buffer())
        {
            add_item(
                "Force LDR display", 0, [] {}, SYSTEM_COMMAND_MOD, 'L', false);
            auto LDR_checkbox = m_menu_items.back();
            LDR_checkbox->set_flags(Button::ToggleButton);
            LDR_checkbox->set_pushed(m_image_view->LDR());
            LDR_checkbox->set_tooltip("Clip the display to [0,1] as if displaying a low-dynamic range image.");
            LDR_checkbox->set_change_callback([this](bool v) { m_image_view->set_LDR(v); });
        }
        menu->popup()->add<Separator>();

        add_item(
            "Help", FA_QUESTION_CIRCLE, [this] { show_help_window(); }, 0, 'H', false);

        // add menu items that are not visible in the mnu but still have keyboard shortcuts
        {
            menu = m_menubar->add_menu("Images list");
            menu->set_visible(false);

            menu->popup()->add<Separator>()->set_visible(false);
            for (int i = 0; i < 9; ++i)
            {
                auto cb = [this, i]
                {
                    auto nth = m_images_panel->nth_visible_image_index(i);
                    if (nth >= 0)
                        m_images_panel->set_current_image_index(nth);
                };
                add_item(fmt::format("Select image {}", i + 1), 0, cb, 0, GLFW_KEY_1 + i);
                m_menu_items.back()->set_visible(false);
            }
            menu->popup()->add<Separator>()->set_visible(false);
            for (int i = 0; i < std::min((int)EChannel::NUM_CHANNELS, 9); ++i)
            {
                auto cb = [this, i]
                {
                    if (i < EChannel::NUM_CHANNELS)
                        m_images_panel->set_channel(EChannel(i));
                };
                add_item(fmt::format("Select color channel {}", channel_names()[i]), 0, cb, SYSTEM_COMMAND_MOD,
                         GLFW_KEY_0 + i);
                m_menu_items.back()->set_visible(false);
            }
            menu->popup()->add<Separator>()->set_visible(false);
            for (int i = 0; i < std::min((int)EBlendMode::NUM_BLEND_MODES, 9); ++i)
            {
                auto cb = [this, i]
                {
                    if (i < EBlendMode::NUM_BLEND_MODES)
                        m_images_panel->set_blend_mode(EBlendMode(i));
                };
                add_item(fmt::format("Select blend mode {}", blend_mode_names()[i]), 0, cb, GLFW_MOD_SHIFT,
                         GLFW_KEY_1 + i);
                m_menu_items.back()->set_visible(false);
            }
            menu->popup()->add<Separator>()->set_visible(false);
            add_item(
                "Go to previous image", 0,
                [p = m_images_panel]
                { p->set_current_image_index(p->next_visible_image(p->current_image_index(), Backward)); },
                0, GLFW_KEY_DOWN);
            m_menu_items.back()->set_visible(false);
            add_item(
                "Go to next image", 0,
                [p = m_images_panel]
                { p->set_current_image_index(p->next_visible_image(p->current_image_index(), Forward)); },
                0, GLFW_KEY_UP);
            m_menu_items.back()->set_visible(false);
            menu->popup()->add<Separator>()->set_visible(false);
            add_item(
                "Expand selection to previous image", 0,
                [p = m_images_panel]
                {
                    p->select_image_index(p->next_visible_image(p->current_image_index(), Backward));
                    p->set_current_image_index(p->next_visible_image(p->current_image_index(), Backward));
                },
                SYSTEM_COMMAND_MOD, GLFW_KEY_DOWN);
            m_menu_items.back()->set_visible(false);
            add_item(
                "Expand selection to next image", 0,
                [p = m_images_panel]
                {
                    p->select_image_index(p->next_visible_image(p->current_image_index(), Forward));
                    p->set_current_image_index(p->next_visible_image(p->current_image_index(), Forward));
                },
                SYSTEM_COMMAND_MOD, GLFW_KEY_UP);
            m_menu_items.back()->set_visible(false);
            menu->popup()->add<Separator>()->set_visible(false);
            add_item(
                "Send image backward", 0,
                [this]
                {
                    m_images_panel->send_image_backward();
                    request_layout_update();
                },
                GLFW_MOD_ALT, GLFW_KEY_DOWN);
            m_menu_items.back()->set_visible(false);
            add_item(
                "Bring image forward", 0,
                [this]
                {
                    m_images_panel->bring_image_forward();
                    request_layout_update();
                },
                GLFW_MOD_ALT, GLFW_KEY_UP);
            m_menu_items.back()->set_visible(false);

            menu->popup()->add<Separator>()->set_visible(false);
            add_item(
                "Go to previous image", 0, [this] { m_images_panel->swap_current_selected_with_previous(); },
                GLFW_MOD_ALT, GLFW_KEY_TAB);
            m_menu_items.back()->set_visible(false);
            add_item(
                "Find image", 0, [this] { m_images_panel->focus_filter(); }, SYSTEM_COMMAND_MOD, 'F');
            m_menu_items.back()->set_visible(false);
        }
    }

    // set the active tool
    set_tool((ETool)std::clamp(m_tools.front()->all_tool_settings().value("active tool", (int)Tool_None),
                               (int)Tool_None, (int)Tool_Num_Tools - 1));

    m_image_view->set_exposure(exposure);
    m_image_view->set_gamma(gamma);
    m_image_view->set_sRGB(sRGB);
    m_image_view->set_dithering(dither);

    drop_event(args);

    set_size(m_settings.value("geometry", json::object()).value("size", Vector2i(800, 600)));

    Vector2i desired_pos = m_settings.value("geometry", json::object()).value("position", Vector2i(0, 0));
    glfwSetWindowPos(glfw_window(), desired_pos.x(), desired_pos.y());

    request_layout_update();
    set_resize_callback([&](nanogui::Vector2i) { request_layout_update(); });

    // make sure the menubar is on top
    move_window_to_front(m_menubar);

    set_visible(true);
    glfwSwapInterval(1);

    // Nanogui will redraw the screen for key/mouse events, but we need to manually
    // invoke redraw for things like gui animations. do this in a separate thread
    m_gui_refresh_thread = std::thread(
        [this]()
        {
            static int                refresh_count = 0;
            std::chrono::microseconds idle_quantum  = std::chrono::microseconds(1000 * 1000 / 20);
            std::chrono::microseconds anim_quantum  = std::chrono::microseconds(1000 * 1000 / 120);
            while (true)
            {
                if (m_join_requested)
                    return;
                bool anim = this->should_refresh_gui();
                std::this_thread::sleep_for(anim ? anim_quantum : idle_quantum);
                this->m_redraw = false;
                this->redraw();
                if (anim)
                    spdlog::trace("refreshing gui for animation {}", refresh_count++);
            }
        });
}

HDRViewScreen::~HDRViewScreen()
{
    m_join_requested = true;
    spdlog::trace("quitting HDRView");
    m_gui_refresh_thread.join();
}

void HDRViewScreen::ask_to_quit()
{
    auto dialog = new SimpleDialog(this, SimpleDialog::Type::Warning, "Warning!", "Do you really want to quit?", "Yes",
                                   "No", true);
    dialog->set_callback([this](int result) { this->set_visible(result != 0); });
    dialog->request_focus();
}

void HDRViewScreen::read_settings()
{
    try
    {
        string directory = config_directory();
        filesystem::create_directories(directory);
        string filename = directory + "settings.json";
        spdlog::info("Reading configuration from file {}", filename);

        std::ifstream stream(filename);
        if (!stream.good())
            throw std::runtime_error(fmt::format("Cannot open settings file: \"{}\".", filename));

        stream >> m_settings;
    }
    catch (const exception &e)
    {
        spdlog::warn("Error while reading settings file: {}", e.what());
        spdlog::info("Using default settings.");
        m_settings = json::object();
    }
}

void HDRViewScreen::write_settings()
{
    try
    {
        string directory = config_directory();
        filesystem::create_directories(directory);
        string filename = directory + "settings.json";

        spdlog::info("Saving configuration file to {}", filename);

        // open file
        std::ofstream stream(filename);
        if (!stream.good())
            throw std::runtime_error(fmt::format("Cannot open settings file: \"{}\".", filename));

        Vector2i position(0, 0);
        glfwGetWindowPos(glfw_window(), &position.x(), &position.y());
        m_settings["geometry"] = {{"tool panel width", m_tool_panel->fixed_width()},
                                  {"side panel width", m_side_panel_contents->fixed_width()},
                                  {"position", position},
                                  {"size", size()}};

        m_settings["color picker"] = {{"foreground", m_color_btns->foreground()->color()},
                                      {"background", m_color_btns->background()->color()}};

        m_settings["side panel"] = {{"solo mode", m_solo_mode}, {"visible panels", json::array()}};
        for (size_t i = 0; i < m_panel_btns.size(); ++i)
            if (m_panel_btns[i]->pushed())
                m_settings["side panel"]["visible panels"].push_back(i);

        m_image_view->write_settings(m_settings);

        for (auto t : m_tools) t->write_settings();

        m_tools.front()->all_tool_settings()["active tool"] = m_tool;

        m_settings["verbosity"] = spdlog::get_level();

        stream << std::setw(4) << m_settings << std::endl;
        stream.close();
    }
    catch (const exception &e)
    {
        spdlog::warn("Error while writing settings file: {}", e.what());
    }
}

void HDRViewScreen::set_active_colorpicker(HDRColorPicker *cp)
{
    spdlog::trace("setting colorpicker to {}", intptr_t(cp));
    if (m_images_panel->current_image())
    {
        m_active_colorpicker = cp;
        if (cp)
            push_gui_refresh();
        else
            pop_gui_refresh();

        set_tool(Tool_Eyedropper);
    }
    else
    {
        m_active_colorpicker = nullptr;
        set_tool(Tool_None);
    }
}

void HDRViewScreen::set_tool(ETool t)
{
    auto set_active = [this](int i, bool b)
    {
        auto tool = m_tools[i];
        spdlog::trace("setting {} active: {}.", tool->name(), b);
        if (tool->toolbutton())
            tool->toolbutton()->set_pushed(b);
        else
            spdlog::error("Button for {} never created.", tool->name());

        if (tool->menuitem())
            tool->menuitem()->set_pushed(b);
        else
            spdlog::error("Menu item for {} never created.", tool->name());

        if (tool->options_bar())
        {
            tool->options_bar()->set_visible(b);
            request_layout_update();
        }
        else
            spdlog::error("Options widget for {} never created.", tool->name());
    };

    m_tool = t;
    for (int i = 0; i < (int)Tool_Num_Tools; ++i) set_active(i, false);

    set_active(t, true);
}

void HDRViewScreen::update_caption()
{
    auto img = m_images_panel->current_image();
    if (img)
        set_caption(string("HDRView [") + img->filename() + (img->is_modified() ? "*" : "") + "]");
    else
        set_caption(string("HDRView"));
}

void HDRViewScreen::bring_to_focus() const { glfwFocusWindow(m_glfw_window); }

bool HDRViewScreen::drop_event(const vector<string> &filenames)
{
    try
    {
        m_images_panel->load_images(filenames);

        bring_to_focus();

        // Ensure the new image button will have the correct visibility state.
        m_images_panel->set_filter(m_images_panel->filter());

        request_layout_update();
    }
    catch (const exception &e)
    {
        new SimpleDialog(this, SimpleDialog::Type::Warning, "Error", string("Could not load:\n ") + e.what());
        return false;
    }
    return true;
}

void HDRViewScreen::ask_close_image(int)
{
    auto closeit = [this](int curr, int next)
    {
        m_images_panel->close_image();
        cout << "curr: " << m_images_panel->current_image_index() << endl;
    };

    auto curr = m_images_panel->current_image_index();
    auto next = m_images_panel->next_visible_image(curr, Forward);
    cout << "curr: " << curr << "; next: " << next << endl;
    if (auto img = m_images_panel->image(curr))
    {
        if (img->is_modified())
        {
            auto dialog = new SimpleDialog(this, SimpleDialog::Type::Warning, "Warning!",
                                           "Image has unsaved modifications. Close anyway?", "Yes", "Cancel", true);
            dialog->set_callback(
                [curr, next, closeit](int cancel)
                {
                    spdlog::trace("closing image callback {} {} {}", cancel, curr, next);
                    if (!cancel)
                        closeit(curr, next);
                });
        }
        else
            closeit(curr, next);
    }
}

void HDRViewScreen::ask_close_all_images()
{
    bool any_modified = false;
    for (int i = 0; i < m_images_panel->num_images(); ++i) any_modified |= m_images_panel->image(i)->is_modified();

    if (any_modified)
    {
        auto dialog =
            new SimpleDialog(this, SimpleDialog::Type::Warning, "Warning!",
                             "Some images have unsaved modifications. Close all images anyway?", "Yes", "Cancel", true);
        dialog->set_callback(
            [this](int close)
            {
                if (close == 0)
                    m_images_panel->close_all_images();
            });
    }
    else
        m_images_panel->close_all_images();
}

bool HDRViewScreen::load_image()
{
    vector<string> files = file_dialog({{"exr", "OpenEXR image"},
                                        {"dng", "Digital Negative raw image"},
                                        {"png", "Portable Network Graphic image"},
                                        {"pfm", "Portable FloatMap image"},
                                        {"ppm", "Portable PixMap image"},
                                        {"pnm", "Portable AnyMap image"},
                                        {"jpg", "JPEG image"},
                                        {"tga", "Truevision Targa image"},
                                        {"pic", "Softimage PIC image"},
                                        {"bmp", "Windows Bitmap image"},
                                        {"gif", "Graphics Interchange Format image"},
                                        {"hdr", "Radiance rgbE format image"},
                                        {"psd", "Photoshop document"}},
                                       false, true);

    // re-gain focus
    glfwFocusWindow(m_glfw_window);

    if (!files.empty())
        return drop_event(files);
    return false;
}

void HDRViewScreen::new_image()
{
    static int    width = 800, height = 600;
    static string name = "New image...";
    static Color  bg(0, 255);
    static float  EV = 0.f;

    FormHelper *gui = new FormHelper(this);
    gui->set_fixed_size(Vector2i(0, 20));

    auto window = new Dialog(this, name);
    gui->set_window(window);

    if (m_images_panel->current_image() && m_images_panel->current_image()->roi().has_volume())
    {
        width  = m_images_panel->current_image()->roi().size().x();
        height = m_images_panel->current_image()->roi().size().y();
    }

    {
        auto w = gui->add_variable("Width:", width);
        w->set_spinnable(true);
        w->set_min_value(1);
        w->set_units("px");
    }

    {
        auto h = gui->add_variable("Height:", height);
        h->set_spinnable(true);
        h->set_min_value(1);
        h->set_units("px");
    }

    auto spacer = new Widget(window);
    spacer->set_fixed_height(5);
    gui->add_widget("", spacer);

    bg             = background()->color();
    EV             = background()->exposure();
    auto color_btn = new HDRColorPicker(window, bg, EV);
    color_btn->popup()->set_anchor_offset(color_btn->popup()->height());
    color_btn->set_eyedropper_callback([this, color_btn](bool pushed)
                                       { set_active_colorpicker(pushed ? color_btn : nullptr); });
    gui->add_widget("Background color:", color_btn);
    color_btn->set_final_callback(
        [](const Color &c, float e)
        {
            bg = c;
            EV = e;
        });

    auto popup = color_btn->popup();
    request_layout_update();

    spacer = new Widget(window);
    spacer->set_fixed_height(15);
    gui->add_widget("", spacer);

    window->set_callback(
        [this, popup](int close)
        {
            popup->set_visible(false);
            if (close != 0)
                return;

            float gain = powf(2.f, EV);

            HDRImagePtr img = make_shared<HDRImage>(width, height, Color4(bg[0], bg[1], bg[2], bg[3]) * gain);
            m_images_panel->new_image(img);

            // Ensure the new image button will have the correct visibility state.
            m_images_panel->set_filter(m_images_panel->filter());

            request_layout_update();
        });

    gui->add_widget("", window->add_buttons());

    window->center();
    window->request_focus();
}

void HDRViewScreen::duplicate_image()
{
    HDRImagePtr clipboard;
    if (auto img = m_images_panel->current_image())
    {
        auto roi = img->roi();
        if (!roi.has_volume())
            roi = img->box();
        clipboard = make_shared<HDRImage>(roi.size().x(), roi.size().y());
        clipboard->copy_paste(img->image(), roi, 0, 0);
    }
    else
        return;

    m_images_panel->new_image(clipboard);

    bring_to_focus();

    // Ensure the new image button will have the correct visibility state.
    m_images_panel->set_filter(m_images_panel->filter());

    request_layout_update();
}

void HDRViewScreen::save_image()
{
    try
    {
        if (!m_images_panel->current_image())
            return;

        string filename = m_images_panel->current_image()->filename();

        if (!filename.empty())
            m_images_panel->save_image_as(filename, m_image_view->exposure(), m_image_view->gamma(),
                                          m_image_view->sRGB(), m_image_view->dithering_on());
    }
    catch (const exception &e)
    {
        new SimpleDialog(this, SimpleDialog::Type::Warning, "Error",
                         string("Could not save image due to an error:\n") + e.what());
    }
}

void HDRViewScreen::save_image_as()
{
    try
    {
        if (!m_images_panel->current_image())
            return;

        string filename = file_dialog(
            {
                {"exr", "OpenEXR image"},
                {"hdr", "Radiance rgbE format image"},
                {"png", "Portable Network Graphic image"},
                {"pfm", "Portable FloatMap image"},
                {"ppm", "Portable PixMap image"},
                {"pnm", "Portable AnyMap image"},
                {"jpg", "JPEG image"},
                {"jpeg", "JPEG image"},
                {"tga", "Truevision Targa image"},
                {"bmp", "Windows Bitmap image"},
            },
            true);

        // re-gain focus
        glfwFocusWindow(m_glfw_window);

        if (!filename.empty())
            m_images_panel->save_image_as(filename, m_image_view->exposure(), m_image_view->gamma(),
                                          m_image_view->sRGB(), m_image_view->dithering_on());
    }
    catch (const exception &e)
    {
        new SimpleDialog(this, SimpleDialog::Type::Warning, "Error",
                         string("Could not save image due to an error:\n") + e.what());
    }
}

void HDRViewScreen::show_help_window()
{
    auto help = new HelpWindow(this);

    m_images_panel->add_shortcuts(help);
    m_image_view->add_shortcuts(help);

    for (auto t : m_tools) t->add_shortcuts(help);

    m_menubar->add_shortcuts(help);

    help->center();

    auto close_button = new Button{help->button_panel(), "", FA_TIMES};
    close_button->set_callback(
        [help]()
        {
            if (help->callback())
                help->callback()(0);
            help->dispose();
        });

    request_layout_update();
}

Dialog *HDRViewScreen::active_dialog() const
{
    if (m_focus_path.size() > 1)
        return dynamic_cast<Dialog *>(m_focus_path[m_focus_path.size() - 2]);

    return nullptr;
}

bool HDRViewScreen::keyboard_event(int key, int scancode, int action, int modifiers)
{
    if (Screen::keyboard_event(key, scancode, action, modifiers))
        return true;

    if (action == GLFW_PRESS)
    {
        if (auto dialog = active_dialog())
        {
            spdlog::trace("Modal dialog: {}", dialog->title());

            // if the help window is open, close it with the 'H' key
            if (auto help = dynamic_cast<HelpWindow *>(dialog))
            {
                if (key == 'H')
                {
                    if (help->callback())
                        help->callback()(0);
                    dialog->dispose();
                    return true;
                }
            }

            // if any other dialog is open, confirm/cancel it with the enter/escape keys
            if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER)
            {
                if (dialog->callback())
                    dialog->callback()(key == GLFW_KEY_ENTER ? 0 : 1);
                dialog->dispose();
                return true;
            }
            return true;
        }
    }

    if (m_tools[m_tool]->keyboard(key, scancode, action, modifiers))
        return true;

    if (m_image_view->keyboard_event(key, scancode, action, modifiers))
        return true;

    if (m_images_panel->keyboard_event(key, scancode, action, modifiers))
        return true;

    if (action == GLFW_RELEASE)
        return false;

    m_menubar->process_hotkeys(modifiers, key);

    return false;
}

bool HDRViewScreen::at_side_panel_edge(const Vector2i &p)
{
    if (!(m_animation_goal & SIDE_PANELS))
        return false;
    auto w = find_widget(p);
    auto x = p.x() - m_side_panel->position().x() - m_side_panel->fixed_width();
    return x < 10 && x > -5 &&
           (w == m_side_panel || w == m_image_view || w == m_side_panel_contents || w == m_side_scroll_panel);
}

bool HDRViewScreen::at_tool_panel_edge(const Vector2i &p)
{
    if (!(m_animation_goal & SIDE_PANELS))
        return false;
    auto w = find_widget(p);
    auto x = p.x() - m_tool_panel->position().x();
    return x < 10 && x > -5 && (w == m_tool_panel || w == m_image_view);
}

bool HDRViewScreen::mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers)
{
    // make sure the menu bar is always on top
    move_window_to_front(m_menubar);

    // temporarily increase the gui refresh rate between mouse down and up events.
    // makes things like dragging smoother
    if (down)
        push_gui_refresh();
    else
        pop_gui_refresh();

    if (m_active_colorpicker && down)
    {
        spdlog::trace("ending eyedropper");
        m_active_colorpicker->end_eyedropper();
        set_tool(Tool_None);
        return true;
    }

    // close all popup menus
    if (down)
    {
        bool closed_a_menu = false;
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        {
            Widget *child = *it;
            if (child->visible() && !child->contains(p - m_pos) && dynamic_cast<PopupMenu *>(child))
            {
                child->set_visible(false);
                closed_a_menu = true;
            }
        }
        if (closed_a_menu)
            return true;
    }

    if (button == GLFW_MOUSE_BUTTON_1 && down && at_side_panel_edge(p))
    {
        m_dragging_side_panel = true;

        // prevent Screen::cursorPosCallbackEvent from calling drag_event on other widgets
        m_drag_active = false;
        m_drag_widget = nullptr;
        return true;
    }
    else
        m_dragging_side_panel = false;

    if (button == GLFW_MOUSE_BUTTON_1 && down && at_tool_panel_edge(p))
    {
        m_dragging_tool_panel = true;

        // prevent Screen::cursorPosCallbackEvent from calling drag_event on other widgets
        m_drag_active = false;
        m_drag_widget = nullptr;
        return true;
    }
    else
        m_dragging_tool_panel = false;

    bool ret = Screen::mouse_button_event(p, button, down, modifiers);

    return ret;
}

void HDRViewScreen::resize_side_panel(int w)
{
    w = std::clamp(w, 215, width() - 100);
    m_side_panel_contents->set_fixed_width(w);
    m_side_scroll_panel->set_fixed_width(w + 12);
    m_side_panel->set_fixed_width(m_side_scroll_panel->fixed_width());
    request_layout_update();
}
void HDRViewScreen::resize_tool_panel(int w)
{
    w = std::clamp(w, 33, 62);
    if (w == 62 && w != m_tool_panel->fixed_width())
        m_tool_panel->child_at(0)->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 4));

    if (m_tool_panel->fixed_width() == 62 && w != 62)
        m_tool_panel->child_at(0)->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 4));

    if (w == 62)
        for (auto c : m_tool_panel->child_at(0)->children()) c->set_fixed_size(Vector2i((w - 12) / 2));
    else
        for (auto c : m_tool_panel->child_at(0)->children()) c->set_fixed_size(Vector2i(w - 8, 0));

    m_tool_panel->set_fixed_width(w);
    request_layout_update();
}

bool HDRViewScreen::mouse_motion_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                                       int modifiers)
{
    m_side_panel->set_cursor(Cursor::Arrow);
    m_side_scroll_panel->set_cursor(Cursor::Arrow);
    m_side_panel_contents->set_cursor(Cursor::Arrow);
    m_image_view->set_cursor(Cursor::Arrow);
    m_tool_panel->set_cursor(Cursor::Arrow);

    if (!m_active_colorpicker && !active_dialog())
    {
        if (m_dragging_side_panel || at_side_panel_edge(p))
        {
            m_side_panel->set_cursor(Cursor::HResize);
            m_side_scroll_panel->set_cursor(Cursor::HResize);
            m_side_panel_contents->set_cursor(Cursor::HResize);
            m_image_view->set_cursor(Cursor::HResize);
        }

        if (m_dragging_tool_panel || at_tool_panel_edge(p))
        {
            m_tool_panel->set_cursor(Cursor::HResize);
            m_image_view->set_cursor(Cursor::HResize);
        }
    }

    if (m_dragging_side_panel)
    {
        resize_side_panel(p.x());
        return true;
    }

    if (m_dragging_tool_panel)
    {
        resize_tool_panel(width() - p.x());
        return true;
    }

    return Screen::mouse_motion_event(p, rel, button, modifiers);
}

void HDRViewScreen::update_layout()
{
    spdlog::trace("update_layout; {}", m_animation_running);
    int header_height   = m_top_panel->fixed_height();
    int sidepanel_width = m_side_panel->fixed_width();
    int toolpanel_width = m_tool_panel->fixed_width();
    int footer_height   = m_status_bar->fixed_height();

    static int header_shift    = 0;
    static int sidepanel_shift = 0;
    static int toolpanel_shift = 0;
    static int footer_shift    = 0;

    if (m_animation_running)
    {
        const double duration = 0.2;
        double       elapsed  = glfwGetTime() - m_gui_animation_start;
        // stop the animation after 2 seconds
        if (elapsed > duration)
        {
            pop_gui_refresh();
            m_animation_running = false;
            sidepanel_shift     = (m_animation_goal & SIDE_PANELS) ? 0 : -sidepanel_width;
            toolpanel_shift     = (m_animation_goal & SIDE_PANELS) ? 0 : toolpanel_width;
            header_shift        = (m_animation_goal & TOP_PANEL) ? 0 : -header_height;
            footer_shift        = (m_animation_goal & BOTTOM_PANEL) ? 0 : footer_height;
        }
        // animate the location of the panels
        else
        {
            // only animate the sidepanel if it isn't already at the goal position
            if (((m_animation_goal & SIDE_PANELS) && sidepanel_shift != 0) ||
                (!(m_animation_goal & SIDE_PANELS) && sidepanel_shift != -sidepanel_width))
            {
                double start    = (m_animation_goal & SIDE_PANELS) ? double(-sidepanel_width) : 0.0;
                double end      = (m_animation_goal & SIDE_PANELS) ? 0.0 : double(-sidepanel_width);
                sidepanel_shift = static_cast<int>(round(lerp(start, end, smoothstep(0.0, duration, elapsed))));

                start           = (m_animation_goal & SIDE_PANELS) ? double(toolpanel_width) : 0.0;
                end             = (m_animation_goal & SIDE_PANELS) ? 0.0 : double(toolpanel_width);
                toolpanel_shift = static_cast<int>(round(lerp(start, end, smoothstep(0.0, duration, elapsed))));
            }
            // only animate the header if it isn't already at the goal position
            if (((m_animation_goal & TOP_PANEL) && header_shift != 0) ||
                (!(m_animation_goal & TOP_PANEL) && header_shift != -header_height))
            {
                double start = (m_animation_goal & TOP_PANEL) ? double(-header_height) : 0.0;
                double end   = (m_animation_goal & TOP_PANEL) ? 0.0 : double(-header_height);
                header_shift = static_cast<int>(round(lerp(start, end, smoothstep(0.0, duration, elapsed))));
            }

            // only animate the footer if it isn't already at the goal position
            if (((m_animation_goal & BOTTOM_PANEL) && footer_shift != 0) ||
                (!(m_animation_goal & BOTTOM_PANEL) && footer_shift != footer_height))
            {
                double start = (m_animation_goal & BOTTOM_PANEL) ? double(footer_height) : 0.0;
                double end   = (m_animation_goal & BOTTOM_PANEL) ? 0.0 : double(footer_height);
                footer_shift = static_cast<int>(round(lerp(start, end, smoothstep(0.0, duration, elapsed))));
            }
        }
    }

    int menu_height = m_menubar->height();

    m_menubar->set_fixed_width(width());

    m_top_panel->set_position(nanogui::Vector2i(0, header_shift + menu_height));
    m_tools[m_tool]->update_width(width());

    int middle_height = height() - menu_height - header_height - footer_height - header_shift + footer_shift;
    int middle_width  = width() - toolpanel_width + toolpanel_shift;

    m_side_panel->set_position(nanogui::Vector2i(sidepanel_shift, header_shift + menu_height + header_height));
    m_side_panel->set_fixed_height(middle_height);

    m_tool_panel->set_position(nanogui::Vector2i(middle_width, header_shift + menu_height + header_height));
    m_tool_panel->set_fixed_height(middle_height);

    m_image_view->set_position(
        nanogui::Vector2i(sidepanel_shift + sidepanel_width, header_shift + menu_height + header_height));
    m_image_view->set_fixed_width(width() - sidepanel_shift - sidepanel_width - toolpanel_width + toolpanel_shift);
    m_image_view->set_fixed_height(middle_height);

    m_status_bar->set_position(nanogui::Vector2i(0, header_shift + menu_height + header_height + middle_height));
    m_status_bar->set_fixed_width(width());

    int lh = std::min(middle_height, m_side_panel_contents->preferred_size(m_nvg_context).y());
    m_side_scroll_panel->set_fixed_height(lh);

    int zoomWidth = m_zoom_label->preferred_size(m_nvg_context).x();
    m_zoom_label->set_width(zoomWidth);
    m_zoom_label->set_position(nanogui::Vector2i(width() - zoomWidth - 6, 0));

    perform_layout();

    if (!m_dragging_side_panel && !m_dragging_tool_panel)
    {
        // With a changed layout the relative position of the mouse
        // within children changes and therefore should get updated.
        // nanogui does not handle this for us.
        double x, y;
        glfwGetCursorPos(m_glfw_window, &x, &y);
        cursor_pos_callback_event(x, y);
    }
}

void HDRViewScreen::draw_all()
{
    spdlog::trace("draw; m_animation_running: {}; {}", m_animation_running, m_redraw);
    if (m_redraw)
    {
        m_redraw = false;

        draw_setup(); // in Screen
        draw_contents();
        draw_widgets();
        draw_teardown(); // in Screen
    }
}

void HDRViewScreen::draw_contents()
{
    clear();

    m_images_panel->run_requested_callbacks();

    bool can_modify = false;

    if (auto img = m_images_panel->current_image())
    {
        img->check_async_result();
        img->upload_to_GPU();

        if (!img->is_null() && img->histograms() && img->histograms()->ready() && img->histograms()->get())
        {
            auto lazyHist = img->histograms();
            m_stats_label->set_caption(fmt::format("{:3.3g}\n{:3.3g}\n{:3.3g}", lazyHist->get()->minimum,
                                                   lazyHist->get()->average, lazyHist->get()->maximum));
        }
        else
            m_stats_label->set_caption("");

        m_roi_info_label->set_caption(
            img->roi().has_volume() ? fmt::format("{: 4d}\n{: 4d}", img->roi().size().x(), img->roi().size().y()) : "");

        auto   ruler      = dynamic_cast<Ruler *>(m_tools[Tool_Ruler]);
        string angle_str  = isnan(ruler->angle()) ? "" : fmt::format("{:3.2f} °", ruler->angle());
        string length_str = isnan(ruler->distance()) ? "" : fmt::format("{:.1f} px", ruler->distance());
        m_ruler_info_label->set_caption(angle_str + "\n" + length_str);

        can_modify = img->can_modify();
    }

    for (auto i : m_edit_items) i->set_enabled(can_modify);

    if (m_need_layout_update || m_animation_running)
    {
        update_layout();
        // redraw();
        m_need_layout_update = false;
    }
}

void HDRViewScreen::draw_widgets()
{
    nvgBeginFrame(m_nvg_context, m_size[0], m_size[1], m_pixel_ratio);

    draw(m_nvg_context);

    // copied from nanogui::Screen
    // FIXME: prevent tooltips from running off right edge of screen.
    double elapsed = glfwGetTime() - m_last_interaction;
    if (elapsed > 0.5f)
    {
        // Draw tooltips
        const Widget *widget = find_widget(m_mouse_pos);
        if (widget && !widget->tooltip().empty())
        {
            int tooltip_width = 150;

            float bounds[4];
            nvgFontFace(m_nvg_context, "sans");
            nvgFontSize(m_nvg_context, 15.0f);
            nvgTextAlign(m_nvg_context, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgTextLineHeight(m_nvg_context, 1.1f);
            Vector2i pos = widget->absolute_position() + Vector2i(widget->width() / 2, widget->height() + 10);

            nvgTextBounds(m_nvg_context, pos.x(), pos.y(), widget->tooltip().c_str(), nullptr, bounds);

            int w = (bounds[2] - bounds[0]) / 2;
            if (w > tooltip_width / 2)
            {
                nvgTextAlign(m_nvg_context, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
                nvgTextBoxBounds(m_nvg_context, pos.x(), pos.y(), tooltip_width, widget->tooltip().c_str(), nullptr,
                                 bounds);

                w = (bounds[2] - bounds[0]) / 2;
            }
            int shift = 0;

            if (pos.x() - w - 8 < 0)
            {
                // Keep tooltips on screen
                shift = pos.x() - w - 8;
                pos.x() -= shift;
                bounds[0] -= shift;
                bounds[2] -= shift;
            }
            else if (pos.x() + w + 8 > width())
            {
                // Keep tooltips on screen
                shift = pos.x() + w + 8 - width();
                pos.x() -= shift;
                bounds[0] -= shift;
                bounds[2] -= shift;
            }

            nvgGlobalAlpha(m_nvg_context, std::min(1.0, 2 * (elapsed - 0.5f)) * 0.8);

            nvgBeginPath(m_nvg_context);
            nvgFillColor(m_nvg_context, Color(0, 255));
            nvgRoundedRect(m_nvg_context, bounds[0] - 4 - w, bounds[1] - 4, (int)(bounds[2] - bounds[0]) + 8,
                           (int)(bounds[3] - bounds[1]) + 8, 3);

            int px = (int)((bounds[2] + bounds[0]) / 2) - w + shift;
            nvgMoveTo(m_nvg_context, px, bounds[1] - 10);
            nvgLineTo(m_nvg_context, px + 7, bounds[1] + 1);
            nvgLineTo(m_nvg_context, px - 7, bounds[1] + 1);
            nvgFill(m_nvg_context);

            nvgFillColor(m_nvg_context, Color(255, 255));
            nvgFontBlur(m_nvg_context, 0.0f);
            nvgTextBox(m_nvg_context, pos.x() - w, pos.y(), tooltip_width, widget->tooltip().c_str(), nullptr);
        }
    }

    nvgEndFrame(m_nvg_context);
}
