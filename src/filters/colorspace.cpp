//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h"

#include "colorspace.h"
#include "common.h"
#include "dialog.h"
#include "hdrimage.h"
#include "hdrviewscreen.h"
#include "imagelistpanel.h"
#include "xpuimage.h"
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;

Button *create_colorspace_btn(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel)
{
    static string      name = "Convert color space...";
    static EColorSpace src = LinearSRGB_CS, dst = CIEXYZ_CS;
    auto               b = new Button(parent, name, FA_PALETTE);
    b->set_fixed_height(21);
    b->set_callback(
        [&, screen, images_panel]()
        {
            FormHelper *gui = new FormHelper(screen);
            gui->set_fixed_size(Vector2i(125, 20));

            auto window = new Dialog(screen, name);
            gui->set_window(window);

            add_dropdown(gui, "Source:", src, colorSpaceNames());
            add_dropdown(gui, "Destination:", dst, colorSpaceNames());

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
                        [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                        {
                            return {
                                make_shared<HDRImage>(img->apply_function(
                                    [](const Color4 &c) { return convert_colorspace(c, dst, src); }, xpuimg->roi())),
                                nullptr};
                        });
                });

            gui->add_widget("", window->add_buttons());

            window->center();
            window->request_focus();
        });
    return b;
}