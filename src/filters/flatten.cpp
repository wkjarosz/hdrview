//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h"

#include "common.h"
#include "dialog.h"
#include "hdrcolorpicker.h"
#include "hdrimage.h"
#include "hdrviewscreen.h"
#include "imagelistpanel.h"
#include "xpuimage.h"
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;

static const string name{"Flatten..."};
static Color        bg = Color(0, 255);
static float        EV = 0.f;

std::function<void()> flatten_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(75, 20));

        auto window = new Dialog(screen, name);
        gui->set_window(window);

        bg             = screen->background()->color();
        EV             = screen->background()->exposure();
        auto color_btn = new HDRColorPicker(window, bg, EV);
        color_btn->popup()->set_anchor_offset(color_btn->popup()->height());
        color_btn->set_eyedropper_callback([screen, color_btn](bool pushed)
                                           { screen->set_active_colorpicker(pushed ? color_btn : nullptr); });
        gui->add_widget("Background color:", color_btn);
        color_btn->set_final_callback(
            [](const Color &c, float e)
            {
                bg = c;
                EV = e;
            });

        auto popup = color_btn->popup();
        screen->request_layout_update();

        auto spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [&, popup](int cancel)
            {
                popup->set_visible(false);

                if (cancel)
                    return;

                images_panel->async_modify_selected(
                    [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                    {
                        return {make_shared<HDRImage>(img->apply_function(
                                    [](const Color4 &c)
                                    {
                                        float alpha = c.a + bg.a() * (1.f - c.a);
                                        float gain  = pow(2.f, EV);
                                        return Color4(Color3(c.r, c.g, c.b) * gain * c.a +
                                                          Color3(bg.r(), bg.g(), bg.b()) * gain * bg.a() * (1.f - c.a),
                                                      alpha);
                                    },
                                    xpuimg->roi())),
                                nullptr};
                    });
            });

        gui->add_widget("", window->add_buttons());

        window->center();
        window->request_focus();
    };
}
