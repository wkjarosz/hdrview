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
#include "multigraph.h"
#include "xpuimage.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace local
{
static const string name{"Exposure/Gamma..."};
static float        exposure = 0.0f;
static float        gamma    = 1.0f;
static float        offset   = 0.0f;
} // namespace local

std::function<void()> exposure_gamma_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(55, 20));

        auto window = new Dialog(screen, local::name);
        gui->set_window(window);

        // graph
        auto graph = new MultiGraph(window, Color(255, 255, 255, 30));
        graph->add_plot(Color(255, 255, 255, 200));
        graph->add_plot(Color(255, 255, 255, 50));
        graph->set_fixed_size(Vector2i(200, 200));
        graph->set_filled(false);
        graph->set_no_well();
        graph->set_values(linspaced(257, 0.0f, 1.0f), 0);
        graph->set_values({0.5f, 0.5f}, 2);
        int numTicks = 5;
        // create the x tick marks
        auto xTicks = linspaced(numTicks, 0.0f, 1.0f);
        // create the x tick labels
        vector<string> xTickLabels(numTicks);
        for (int i = 0; i < numTicks; ++i) xTickLabels[i] = fmt::format("{:.2f}", xTicks[i]);
        graph->set_xticks(xTicks, xTickLabels);
        graph->set_yticks(xTicks);
        gui->add_widget("", graph);

        auto graphCb = [graph]()
        {
            auto lCurve = linspaced(257, 0.0f, 1.0f);
            for (auto &&i : lCurve) i = pow(pow(2.0f, local::exposure) * i + local::offset, 1.0f / local::gamma);
            graph->set_values(lCurve, 1);
        };

        graphCb();

        create_floatbox_and_slider(gui, "Exposure:", local::exposure, -10.f, 10.f, 0.1f, graphCb);

        create_floatbox_and_slider(gui, "Offset:", local::offset, -1.f, 1.f, 0.01f, graphCb);

        create_floatbox_and_slider(gui, "Gamma:", local::gamma, 0.0001f, 10.f, 0.1f, graphCb);

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
                        spdlog::debug("{}; {}; {}", local::exposure, local::offset, local::gamma);
                        return {make_shared<HDRImage>(img->apply_function(
                                    [](const Color4 &c)
                                    {
                                        return (Color4(pow(2.0f, local::exposure), 1.f) * c +
                                                Color4(local::offset, 0.f))
                                            .pow(Color4(1.0f / local::gamma, 1.f));
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