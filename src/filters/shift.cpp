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

static const string         name{"Shift..."};
static HDRImage::Sampler    sampler       = HDRImage::BILINEAR;
static HDRImage::BorderMode border_mode_x = HDRImage::REPEAT, border_mode_y = HDRImage::REPEAT;
static float                dx = 0.f, dy = 0.f;

std::function<void()> shift_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(125, 20));

        auto window = new Dialog(screen, name);
        gui->set_window(window);

        auto w = gui->add_variable("X offset:", dx);
        w->set_spinnable(true);
        w->set_units("px");

        w = gui->add_variable("Y offset:", dy);
        w->set_spinnable(true);
        w->set_units("px");

        add_dropdown(gui, "Sampler:", sampler, HDRImage::sampler_names());
        add_dropdown(gui, "Border mode X:", border_mode_x, HDRImage::border_mode_names());
        add_dropdown(gui, "Border mode Y:", border_mode_y, HDRImage::border_mode_names());

        screen->request_layout_update();

        auto spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [images_panel](int cancel)
            {
                if (cancel)
                    return;
                images_panel->async_modify_selected(
                    [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                       AtomicProgress &progress) -> ImageCommandResult
                    {
                        // by default use a no-op passthrough warp function
                        function<Vector2f(const Vector2f &)> shift = [img](const Vector2f &uv)
                        { return (uv + Vector2f(dx / img->width(), dy / img->height())); };
                        return {make_shared<HDRImage>(img->resampled(img->width(), img->height(), progress, shift, 1,
                                                                     sampler, border_mode_x, border_mode_y)),
                                nullptr};
                    });
            });

        gui->add_widget("", window->add_buttons("OK", "Cancel"));

        window->center();
        window->request_focus();
    };
}
