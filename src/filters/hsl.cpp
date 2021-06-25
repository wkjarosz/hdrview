//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h"

#include "colorslider.h"
#include "colorspace.h"
#include "colorwheel.h"
#include "common.h"
#include "dialog.h"
#include "dropdown.h"
#include "editimagepanel.h"
#include "envmap.h"
#include "filmictonecurve.h"
#include "hdrcolorpicker.h"
#include "hdrimage.h"
#include "hdrviewscreen.h"
#include "hslgradient.h"
#include "imagelistpanel.h"
#include "multigraph.h"
#include "xpuimage.h"
#include <ImathMatrix.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;
using Imath::M33f;
using Imath::V2f;
using Imath::V3f;

Button *create_hsl_btn(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel)
{
    static string name       = "Hue/Saturation...";
    static float  hue        = 0.0f;
    static float  saturation = 0.0f;
    static float  lightness  = 0.0f;
    auto          b          = new Button(parent, name, FA_PALETTE);
    b->set_fixed_height(21);
    b->set_callback(
        [&, screen, images_panel]()
        {
            FormHelper *gui = new FormHelper(screen);
            gui->set_fixed_size(Vector2i(55, 20));

            Widget *spacer = nullptr;

            auto window = new Dialog(screen, name);
            gui->set_window(window);

            auto fixedRainbow   = new HSLGradient(window);
            auto dynamicRainbow = new HSLGradient(window);
            fixedRainbow->set_fixed_width(256);
            dynamicRainbow->set_fixed_width(256);

            auto cb = [dynamicRainbow]()
            {
                dynamicRainbow->set_hue_offset(hue);
                dynamicRainbow->set_saturation((saturation + 100.f) / 200.f);
                dynamicRainbow->set_lightness((lightness + 100.f) / 200.f);
            };

            create_floatbox_and_slider(gui, "Hue:", hue, -180.f, 180.f, 1.f, cb);

            create_floatbox_and_slider(gui, "Saturation:", saturation, -100.f, 100.f, 1.f, cb);

            create_floatbox_and_slider(gui, "Lightness:", lightness, -100.f, 100.f, 1.f, cb);

            spacer = new Widget(window);
            spacer->set_fixed_height(5);
            gui->add_widget("", spacer);

            gui->add_widget("", fixedRainbow);

            spacer = new Widget(window);
            spacer->set_fixed_height(5);
            gui->add_widget("", spacer);

            gui->add_widget("", dynamicRainbow);

            spacer = new Widget(window);
            spacer->set_fixed_height(15);
            gui->add_widget("", spacer);

            window->set_callback(
                [&](int cancel)
                {
                    if (cancel)
                        return;
                    images_panel->async_modify_selected(
                        [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                        {
                            return {make_shared<HDRImage>(img->apply_function(
                                        [](Color4 c)
                                        {
                                            HSLAdjust(&c[0], &c[1], &c[2], hue, (saturation + 100.f) / 100.f,
                                                      (lightness) / 100.f);
                                            return c;
                                        },
                                        xpuimg->roi())),
                                    nullptr};
                        });
                });

            gui->add_widget("", window->add_buttons());

            window->center();
            window->request_focus();
        });
    return b;
}