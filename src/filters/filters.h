//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "dropdown.h"        // for Dropdown
#include "fwd.h"             // for HDRViewScreen
#include "hdrimage.h"        // for HDRImage, HDRImage::CanvasAnchor
#include <functional>        // for function
#include <iosfwd>            // for string
#include <nanogui/nanogui.h> // for FormHelper, Vector2i, Widget, Window
#include <vector>            // for vector

template <typename T>
nanogui::Dropdown *add_dropdown(nanogui::FormHelper *gui, const std::string &label, T &variable,
                                const std::vector<std::string>       &names,
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

    dp->set_selected_callback(
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
nanogui::Widget           *create_anchor_widget(nanogui::Widget *window, HDRImage::CanvasAnchor &anchor, int bw);

std::function<void()> cut_callback(HDRImagePtr &clipboard, ImageListPanel *images_panel);
std::function<void()> copy_callback(HDRImagePtr &clipboard, ImageListPanel *images_panel);
std::function<void()> paste_callback(HDRImagePtr clipboard, ImageListPanel *images_panel);
std::function<void()> seamless_paste_callback(HDRImagePtr clipboard, ImageListPanel *images_panel);
std::function<void()> rotate_callback(bool clockwise, ImageListPanel *images_panel);
std::function<void()> flip_callback(bool horizontal, ImageListPanel *images_panel);
std::function<void()> invert_callback(ImageListPanel *images_panel);
std::function<void()> clamp_callback(ImageListPanel *images_panel);
std::function<void()> clamp_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> crop_callback(ImageListPanel *images_panel);
std::function<void()> bump_to_normal_map_callback(ImageListPanel *images_panel);
std::function<void()> irradiance_envmap_callback(ImageListPanel *images_panel);
std::function<void()> colorspace_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> exposure_gamma_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> brightness_contrast_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> filmic_tonemapping_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> hsl_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> gaussian_filter_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> box_blur_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> bilateral_filter_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> unsharp_mask_filter_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> median_filter_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> resize_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> remap_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> shift_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> canvas_size_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> free_xform_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> flatten_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> fill_callback(const nanogui::Color &nfg, ImageListPanel *images_panel);
std::function<void()> fill_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> channel_mixer_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
std::function<void()> zap_gremlins_callback(HDRViewScreen *screen, ImageListPanel *images_panel);
