//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h"

#include "common.h"
#include "dialog.h"
#include "hdrimage.h"
#include "hdrviewscreen.h"
#include "imagelistpanel.h"
#include "xpuimage.h"
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;

static const string         name{"Gaussian blur..."};
static float                width = 1.0f, height = 1.0f;
static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
static bool                 exact = false;

std::function<void()> gaussian_filter_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(75, 20));

        auto window = new Dialog(screen, name);
        gui->set_window(window);

        auto w = gui->add_variable("Width:", width);
        w->set_spinnable(true);
        w->set_min_value(0.0f);
        w->set_value_increment(5.f);
        w->set_units("px");
        w = gui->add_variable("Height:", height);
        w->set_spinnable(true);
        w->set_min_value(0.0f);
        w->set_value_increment(5.f);
        w->set_units("px");

        add_dropdown(gui, "Border mode X:", border_mode_x, HDRImage::border_mode_names());
        add_dropdown(gui, "Border mode Y:", border_mode_y, HDRImage::border_mode_names());

        gui->add_variable("Exact (slow!):", exact, true);

        screen->request_layout_update();

        auto spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [&](int cancel)
            {
                if (cancel)
                    return;

                for (int i = 0; i < images_panel->num_images(); ++i)
                    spdlog::trace("image: {} is {}", i, images_panel->is_selected(i));

                images_panel->async_modify_selected(
                    [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                        AtomicProgress &progress) -> ImageCommandResult
                    {
                        auto roi = xpuimg->roi();
                        return {make_shared<HDRImage>(
                                    exact ? img->gaussian_blurred(width, height, progress, border_mode_x, border_mode_y,
                                                                  6.f, 6.f, roi)
                                          : img->fast_gaussian_blurred(width, height, progress, border_mode_x,
                                                                       border_mode_y, roi)),
                                nullptr};
                    });
            });

        gui->add_widget("", window->add_buttons("OK", "Cancel"));

        window->center();
        window->request_focus();
    };
}
