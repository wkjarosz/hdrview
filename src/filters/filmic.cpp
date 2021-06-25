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

Button *create_filmic_tonemapping_btn(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel)
{
    static string                           name = "Filmic tonemapping...";
    static FilmicToneCurve::FullCurve       fCurve;
    static FilmicToneCurve::CurveParamsUser params;
    static float                            vizFstops   = 1.f;
    static const auto                       activeColor = Color(255, 255, 255, 200);
    auto                                    b           = new Button(parent, name, FA_ADJUST);
    b->set_fixed_height(21);
    b->set_callback(
        [&, screen, images_panel]()
        {
            FormHelper *gui = new FormHelper(screen);
            gui->set_fixed_size(Vector2i(55, 20));

            auto window = new Dialog(screen, name);
            gui->set_window(window);

            // graph
            MultiGraph *graph = new MultiGraph(window, Color(255, 255, 255, 30));
            graph->add_plot(activeColor);
            graph->set_fixed_size(Vector2i(200, 200));
            graph->set_filled(false);
            graph->set_no_well();
            gui->add_widget("", graph);

            auto graphCb = [graph]()
            {
                float                              range = pow(2.f, vizFstops);
                FilmicToneCurve::CurveParamsDirect directParams;
                FilmicToneCurve::calcDirectParamsFromUser(directParams, params);
                FilmicToneCurve::createCurve(fCurve, directParams);

                graph->set_values(linspaced(257, 0.0f, range), 0);
                auto lCurve = linspaced(257, 0.0f, range);
                for (auto &&i : lCurve) i = fCurve.eval(i);
                graph->set_values(lCurve, 1);

                int numTicks = 5;
                // create the x tick marks
                auto xTicks = linspaced(numTicks, 0.0f, 1.0f);
                // create the x tick labels
                vector<string> xTickLabels(numTicks);
                for (int i = 0; i < numTicks; ++i) xTickLabels[i] = fmt::format("{:.2f}", range * xTicks[i]);
                graph->set_xticks(xTicks, xTickLabels);
                graph->set_yticks(linspaced(3, 0.0f, 1.0f));
            };

            graphCb();

            create_floatbox_and_slider(gui, "Graph F-stops:", vizFstops, 0.f, 10.f, 0.1f, graphCb);

            create_floatbox_and_slider(gui, "Toe strength:", params.toeStrength, 0.f, 1.f, 0.01f, graphCb);

            create_floatbox_and_slider(gui, "Toe length:", params.toeLength, 0.f, 1.f, 0.01f, graphCb);

            create_floatbox_and_slider(gui, "Shoulder strength:", params.shoulderStrength, 0.f, 10.f, 0.1f, graphCb);

            create_floatbox_and_slider(gui, "Shoulder length:", params.shoulderLength, 0.f, 1.f, 0.01f, graphCb);

            create_floatbox_and_slider(gui, "Shoulder angle:", params.shoulderAngle, 0.f, 1.f, 0.01f, graphCb);

            create_floatbox_and_slider(gui, "Gamma:", params.gamma, 0.f, 5.f, 0.01f, graphCb);

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
                            return {make_shared<HDRImage>(img->apply_function(
                                        [](const Color4 &c)
                                        { return Color4(fCurve.eval(c.r), fCurve.eval(c.g), fCurve.eval(c.b), c.a); },
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