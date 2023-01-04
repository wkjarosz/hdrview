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

static const string         name{"Zap gremlins..."};
static const vector<string> modes{"Foreground color", "1-ring median"};
static int                  mode = 0;

std::function<void()> zap_gremlins_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(125, 20));

        auto window = new Dialog(screen, name);
        gui->set_window(window);

        add_dropdown(gui, "Replace with:", mode, modes);

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
                        if (mode == 0)
                        {
                            Color  nfg = screen->foreground()->color();
                            Color4 fg(nfg.r(), nfg.g(), nfg.b(), nfg.a());
                            return {make_shared<HDRImage>(img->apply_function(
                                        [fg](const Color4 &c) { return is_valid_color(c) ? c : fg; }, xpuimg->roi())),
                                    nullptr};
                        }
                        else
                        {
                            Box2i    roi      = xpuimg->roi();
                            HDRImage filtered = *img;

                            // ensure valid ROI
                            if (roi.has_volume())
                                roi.intersect(xpuimg->box());
                            else
                                roi = xpuimg->box();

                            if (!roi.has_volume())
                                return {make_shared<HDRImage>(filtered), nullptr};

                            // for every pixel in the image
                            parallel_for(roi.min.y(), roi.max.y(),
                                         [&roi, &filtered, &img](int y)
                                         {
                                             for (int x = roi.min.x(); x < roi.max.x(); x++)
                                             {
                                                 if (is_valid_color(filtered(x, y)))
                                                     continue;

                                                 int                                 num_neighbors = 0;
                                                 std::array<std::array<float, 8>, 4> neighbors;

                                                 for (int dx = -1; dx <= 1; ++dx)
                                                     for (int dy = -1; dy <= 1; ++dy)
                                                     {
                                                         if (x + dx > 0 && x + dx < img->width() - 1 && y + dy > 0 &&
                                                             y + dy < img->height() - 1 &&
                                                             is_valid_color((*img)(x + dx, y + dy)))
                                                         {
                                                             neighbors[0][num_neighbors] = (*img)(x + dx, y + dy).r;
                                                             neighbors[1][num_neighbors] = (*img)(x + dx, y + dy).g;
                                                             neighbors[2][num_neighbors] = (*img)(x + dx, y + dy).b;
                                                             neighbors[3][num_neighbors] = (*img)(x + dx, y + dy).a;

                                                             num_neighbors++;
                                                         }
                                                     }

                                                 int med = (num_neighbors - 1) / 2;

                                                 for (int c = 0; c < 4; ++c)
                                                 {
                                                     nth_element(neighbors[c].begin() + 0, neighbors[c].begin() + med,
                                                                 neighbors[c].begin() + num_neighbors);

                                                     filtered(x, y)[c] = std::isfinite(filtered(x, y)[c])
                                                                             ? filtered(x, y)[c]
                                                                             : neighbors[c][med];
                                                 }
                                             }
                                         });

                            return {make_shared<HDRImage>(filtered), nullptr};
                        }
                    });
            });

        gui->add_widget("", window->add_buttons());

        window->center();
        window->request_focus();
    };
}
