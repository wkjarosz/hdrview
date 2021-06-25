//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "common.h"             // for linspaced, lerp, clamp01, brightness...
#include "dialog.h"             // for Dialog
#include "filters/filters.h"    // for create_floatbox_and_slider, add_drop...
#include "hdrimage.h"           // for HDRImage
#include "hdrviewscreen.h"      // for HDRViewScreen
#include "imagelistpanel.h"     // for ImageListPanel
#include "multigraph.h"         // for MultiGraph
#include <cmath>                // for tan, M_PI_2
#include <commandhistory.h>     // for ImageCommandResult
#include <functional>           // for function
#include <fwd.h>                // for EChannel, CIE_CHROMATICITY, ConstHDR...
#include <memory>               // for shared_ptr, make_shared
#include <nanogui/button.h>     // for Button
#include <nanogui/formhelper.h> // for FormHelper, FormWidget
#include <nanogui/icons.h>      // for FA_ADJUST
#include <nanogui/vector.h>     // for Color, Vector2f, Vector2i
#include <nanogui/widget.h>     // for Widget
#include <spdlog/fmt/fmt.h>     // for format
#include <type_traits>          // for remove_extent_t
#include <utility>              // for pair
#include <vector>               // for vector, __vector_base<>::value_type

using namespace std;

Button *create_brightness_constract_btn(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel)
{
    static string     name          = "Brightness/Contrast...";
    static float      brightness    = 0.0f;
    static float      contrast      = 0.0f;
    static bool       linear        = false;
    static const auto activeColor   = Color(255, 255, 255, 200);
    static const auto inactiveColor = Color(255, 255, 255, 25);
    static enum EChannel
    {
        RGB = 0,
        LUMINANCE,
        CHROMATICITY
    } channel                      = RGB;
    static ::EChannel channelMap[] = {::EChannel::RGB, ::EChannel::LUMINANCE, ::EChannel::CIE_CHROMATICITY};
    auto              b            = new Button(parent, name, FA_ADJUST);
    b->set_fixed_height(21);
    b->set_callback(
        [&, screen, images_panel]()
        {
            FormHelper *gui = new FormHelper(screen);
            gui->set_fixed_size(Vector2i(100, 20));

            auto window = new Dialog(screen, name);
            gui->set_window(window);

            // graph
            auto graph = new MultiGraph(window, Color(255, 255, 255, 30));
            graph->add_plot(inactiveColor);
            graph->add_plot(activeColor);
            graph->add_plot(Color(255, 255, 255, 50));
            graph->set_fixed_size(Vector2i(200, 200));
            graph->set_filled(false);
            graph->set_no_well();
            graph->set_values(linspaced(257, 0.0f, 1.0f), 0);
            graph->set_values({0.5f, 0.5f}, 3);
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
                float slope    = float(std::tan(lerp(0.0, M_PI_2, contrast / 2.0 + 0.5)));
                float midpoint = (1.f - brightness) / 2.f;
                float bias     = (brightness + 1.f) / 2.f;
                auto  lCurve   = linspaced(257, 0.0f, 1.0f);
                for (auto &&i : lCurve) i = brightnessContrastL(i, slope, midpoint);
                lCurve.back() = 1;
                graph->set_values(lCurve, 1);

                auto nlCurve = linspaced(257, 0.0f, 1.0f);
                for (auto &&i : nlCurve) i = brightnessContrastNL(i, slope, bias);

                nlCurve.back() = 1;
                graph->set_values(nlCurve, 2);
            };

            graphCb();

            // brightness
            string help = "Shift the 50% gray midpoint.\n\n"
                          "Setting brightness > 0 boosts a previously darker value to 50%, "
                          "while brightness < 0 dims a previously brighter value to 50%.";

            auto bCb = create_floatbox_and_slider(gui, "Brightness:", brightness, -1.f, 1.f, 0.01f, graphCb, help);

            help     = "Change the slope/gradient at the new 50% midpoint.";
            auto cCb = create_floatbox_and_slider(gui, "Contrast:", contrast, -1.f, 1.f, 0.01f, graphCb, help);

            auto lCheck = gui->add_variable("Linear:", linear, true);
            add_dropdown(gui, "Channel:", channel, {"RGB", "Luminance", "Chromaticity"});

            lCheck->set_callback(
                [graph](bool b)
                {
                    linear = b;
                    graph->set_foreground_color(linear ? activeColor : inactiveColor, 1);
                    graph->set_foreground_color(linear ? inactiveColor : activeColor, 2);
                });

            graph->set_drag_callback(
                [bCb, cCb](const Vector2f &frac)
                {
                    bCb(lerp(1.f, -1.f, clamp01(frac.x())));
                    cCb(lerp(-1.f, 1.f, clamp01(frac.y())));
                });

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
                            return {make_shared<HDRImage>(img->brightness_contrast(brightness, contrast, linear,
                                                                                   channelMap[channel], xpuimg->roi())),
                                    nullptr};
                        });
                });

            gui->add_widget("", window->add_buttons());

            window->center();
            window->request_focus();
        });
    return b;
}