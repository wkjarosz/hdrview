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

static const string name{"Resize..."};
static int          width = 128, height = 128;
static bool         aspect = true;

std::function<void()> resize_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{

    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(0, 20));

        auto window = new Dialog(screen, name);
        gui->set_window(window);

        auto row = new Widget(window);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        width  = images_panel->current_image()->width();
        height = images_panel->current_image()->height();

        auto w    = new IntBox<int>(row, width);
        auto link = new ToolButton(row, FA_LINK);
        auto h    = new IntBox<int>(row, height);

        w->set_spinnable(true);
        w->set_enabled(true);
        w->set_editable(true);
        w->set_min_value(1);
        w->set_font_size(gui->widget_font_size());
        w->set_fixed_size(Vector2i(80, gui->fixed_size().y()));
        w->set_alignment(TextBox::Alignment::Right);
        w->set_units("px");

        link->set_fixed_size(Vector2i(20, 20));
        link->set_pushed(aspect);

        h->set_spinnable(true);
        h->set_enabled(true);
        h->set_editable(true);
        h->set_min_value(1);
        h->set_font_size(gui->widget_font_size());
        h->set_fixed_size(Vector2i(80, gui->fixed_size().y()));
        h->set_alignment(TextBox::Alignment::Right);
        h->set_units("px");

        link->set_change_callback(
            [w, images_panel](bool preserve)
            {
                if (preserve)
                {
                    float aspect =
                        images_panel->current_image()->width() / (float)images_panel->current_image()->height();
                    width = max(1, (int)round(height * aspect));
                    w->set_value(width);
                }
                aspect = preserve;
            });

        w->set_callback(
            [h, link, images_panel](int w)
            {
                width = w;
                if (link->pushed())
                {
                    float aspect =
                        images_panel->current_image()->width() / (float)images_panel->current_image()->height();
                    height = max(1, (int)round(w / aspect));
                    h->set_value(height);
                }
            });

        h->set_callback(
            [w, link, images_panel](int h)
            {
                height = h;
                if (link->pushed())
                {
                    float aspect =
                        images_panel->current_image()->width() / (float)images_panel->current_image()->height();
                    width = max(1, (int)round(height * aspect));
                    w->set_value(width);
                }
            });

        gui->add_widget("", row);

        auto spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [&](int cancel)
            {
                if (cancel)
                    return;

                images_panel->async_modify_selected(
                    [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult {
                        return {make_shared<HDRImage>(img->resized(width, height)), nullptr};
                    });
            });

        gui->add_widget("", window->add_buttons());

        window->center();
        window->request_focus();
    };
}
