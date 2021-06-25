//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "dropdown.h"
#include "hdrimage.h"
#include <nanogui/formhelper.h>
#include <nanogui/widget.h>

template <typename T>
nanogui::Dropdown *add_dropdown(nanogui::FormHelper *gui, const std::string &label, T &variable,
                                const std::vector<std::string> &      names,
                                const std::function<void(const T &)> &cb = std::function<void(const T &)>())
{
    auto spacer = new nanogui::Widget(gui->window());
    spacer->set_fixed_height(5);
    gui->add_widget("", spacer);

    auto dp = new nanogui::Dropdown(gui->window(), names);
    dp->set_selected_index((int)variable);
    nanogui::Vector2i fs = dp->fixed_size();
    dp->set_fixed_size({fs.x() != 0 ? fs.x() : gui->fixed_size().x(), fs.y() != 0 ? fs.y() : gui->fixed_size().y()});
    gui->add_widget(label, dp);

    dp->set_callback(
        [&variable, cb](int i)
        {
            variable = (T)i;
            if (cb)
                cb((T)i);
        });

    return dp;
}

std::function<void(float)> create_floatbox_and_slider(nanogui::FormHelper *gui, std::string name, float &variable,
                                                      float mn, float mx, float step, std::function<void(void)> cb,
                                                      std::string help = "");
nanogui::Widget *          create_anchor_widget(nanogui::Widget *window, HDRImage::CanvasAnchor &anchor, int bw);
nanogui::Button *create_colorspace_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_exposure_gamma_btn(nanogui::Widget *parent, HDRViewScreen *screen,
                                           ImageListPanel *images_panel);
nanogui::Button *create_brightness_constract_btn(nanogui::Widget *parent, HDRViewScreen *screen,
                                                 ImageListPanel *images_panel);
nanogui::Button *create_filmic_tonemapping_btn(nanogui::Widget *parent, HDRViewScreen *screen,
                                               ImageListPanel *images_panel);
nanogui::Button *create_hsl_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_gaussian_filter_btn(nanogui::Widget *parent, HDRViewScreen *screen,
                                            ImageListPanel *images_panel);
nanogui::Button *create_box_filter_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_bilateral_filter_btn(nanogui::Widget *parent, HDRViewScreen *screen,
                                             ImageListPanel *images_panel);
nanogui::Button *create_unsharp_mask_filter_btn(nanogui::Widget *parent, HDRViewScreen *screen,
                                                ImageListPanel *images_panel);
nanogui::Button *create_median_filter_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_resize_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_remap_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_shift_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_canvas_size_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_free_xform_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_flatten_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_fill_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
nanogui::Button *create_channel_mixer_btn(nanogui::Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel);
