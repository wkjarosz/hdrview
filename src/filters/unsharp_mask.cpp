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

Button *create_unsharp_mask_filter_btn(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel)
{
    static float                sigma = 1.0f, strength = 1.0f;
    static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
    static string               name = "Unsharp mask...";
    auto                        b    = new Button(parent, name, FA_TINT);
    b->set_fixed_height(21);
    b->set_callback(
        [&, screen, images_panel]()
        {
            FormHelper *gui = new FormHelper(screen);
            gui->set_fixed_size(Vector2i(75, 20));

            auto window = new Dialog(screen, name);
            gui->set_window(window);

            auto w = gui->add_variable("Sigma:", sigma);
            w->set_spinnable(true);
            w->set_min_value(0.0f);
            w = gui->add_variable("Strength:", strength);
            w->set_spinnable(true);
            w->set_min_value(0.0f);

            add_dropdown(gui, "Border mode X:", border_mode_x, HDRImage::border_mode_names());
            add_dropdown(gui, "Border mode Y:", border_mode_y, HDRImage::border_mode_names());

            screen->request_layout_update();

            auto spacer = new Widget(window);
            spacer->set_fixed_height(15);
            gui->add_widget("", spacer);

            window->set_callback(
                [&](int cancel)
                {
                    if (cancel)
                        return;

                    images_panel->async_modify_selected(
                        [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                            AtomicProgress &progress) -> ImageCommandResult
                        {
                            return {make_shared<HDRImage>(img->unsharp_masked(sigma, strength, progress, border_mode_x,
                                                                              border_mode_y, xpuimg->roi())),
                                    nullptr};
                        });
                });

            gui->add_widget("", window->add_buttons());

            window->center();
            window->request_focus();
        });
    return b;
}