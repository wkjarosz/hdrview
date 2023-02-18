//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h" // for add_dropdown, create_channel_mixer_btn

#include "color.h"          // for Color4
#include "colorslider.h"    // for ColorSlider, ColorSlider::ColorMode
#include "commandhistory.h" // for ImageCommandResult
#include "dialog.h"         // for Dialog
#include "fwd.h"            // for ConstHDRImagePtr, ConstXPUImagePtr
#include "hdrimage.h"       // for HDRImage
#include "hdrviewscreen.h"  // for HDRViewScreen
#include "imagelistpanel.h" // for ImageListPanel
#include <array>            // for array, array<>::value_type
#include <exception>        // for exception
#include <functional>       // for __base, function
#include <iosfwd>           // for string
#include <memory>           // for shared_ptr, make_shared
#include <spdlog/fmt/fmt.h> // for format_to
#include <spdlog/spdlog.h>  // for trace
#include <stddef.h>         // for size_t
#include <utility>          // for pair
#include <vector>           // for vector

using namespace std;

namespace local
{
static const string         name{"Channel mixer..."};
static std::array<Color, 4> weights{Color(1.f, 0.f, 0.f, 1.f), Color(0.f, 1.f, 0.f, 1.f), Color(0.f, 0.f, 1.f, 1.f),
                                    Color(1.f / 3.f, 1.f)};
static enum ETheChannel
{
    RED = 0,
    GREEN,
    BLUE,
    GRAY
} channel              = ETheChannel::RED;
static bool monochrome = false;
static bool normalize  = false;
} // namespace local

std::function<void()> channel_mixer_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(75, 20));

        auto window = new Dialog(screen, local::name);
        gui->set_window(window);

        auto dropdown = add_dropdown(gui, "Output Channel:", local::channel, {"Red", "Green", "Blue", "Gray"});

        auto m_check = gui->add_variable("Monochrome:", local::monochrome);

        screen->request_layout_update();

        auto spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        auto panel = new Widget(window);
        panel->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 0));
        gui->add_widget("", panel);

        std::vector<Widget *> color_panels;
        for (size_t c = 0; c < local::weights.size(); ++c)
        {
            color_panels.push_back(new Widget(panel));
            auto agrid = new AdvancedGridLayout({0, 20, 0}, {});
            agrid->set_margin(0);
            agrid->set_col_stretch(1, 1);
            color_panels[c]->set_layout(agrid);

            std::array<std::string, 3>     names{"Red :", "Green : ", "Blue :"};
            std::vector<ColorSlider *>     sliders;
            std::vector<FloatBox<float> *> float_boxes;
            for (size_t i = 0; i < names.size(); ++i)
            {
                auto l = new Label(color_panels[c], names[i]);
                float_boxes.push_back(new FloatBox<float>(color_panels[c], local::weights[c][i] * 100.f));
                sliders.push_back(new ColorSlider(color_panels[c], local::weights[c], ColorSlider::ColorMode(i)));

                agrid->append_row(0);
                agrid->set_anchor(l, AdvancedGridLayout::Anchor(0, agrid->row_count() - 1));
                agrid->set_anchor(float_boxes[i], AdvancedGridLayout::Anchor(2, agrid->row_count() - 1));

                agrid->append_row(0);
                agrid->set_anchor(sliders[i], AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

                sliders[i]->set_range({-2.f, 2.f});
                sliders[i]->set_fixed_width(250);
                Color color(0.f, 1.f);
                color[i] = local::weights[c][i];
                sliders[i]->set_color(color);
                sliders[i]->set_value(local::weights[c][i]);

                float_boxes[i]->set_spinnable(false);
                float_boxes[i]->set_editable(true);
                float_boxes[i]->number_format("%+3.2f");
                float_boxes[i]->set_min_max_values(-200.f, 200.f);
                float_boxes[i]->set_fixed_width(75);
                float_boxes[i]->set_units("%");
                float_boxes[i]->set_alignment(TextBox::Alignment::Right);
                float_boxes[i]->set_value(local::weights[c][i] * 100.f);

                sliders[i]->set_callback(
                    [c, i, float_boxes, sliders](float v)
                    {
                        local::weights[c][i] = v;
                        float_boxes[i]->set_value(v * 100.f);
                    });
                float_boxes[i]->set_callback(
                    [c, i, float_boxes, sliders](float v)
                    {
                        local::weights[c][i] = v / 100.f;
                        sliders[i]->set_value(v / 100.f);
                    });
            }

            color_panels[c]->set_visible(false);
        }

        color_panels[local::channel]->set_visible(true);

        dropdown->set_enabled(!local::monochrome);

        dropdown->set_selected_callback(
            [color_panels, screen](int i)
            {
                local::channel = (local::ETheChannel)i;
                for (size_t c = 0; c < local::weights.size(); ++c) color_panels[c]->set_visible(c == (size_t)i);
                screen->request_layout_update();
            });

        m_check->set_callback(
            [dropdown](bool b)
            {
                if (b)
                {
                    dropdown->set_selected_index(3);
                    dropdown->selected_callback()(3);
                    dropdown->set_enabled(false);
                }
                else
                    dropdown->set_enabled(true);
                local::monochrome = b;
            });

        gui->add_variable("Normalize:", local::normalize);

        spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [&](int cancel)
            {
                spdlog::trace("monochrome {}, {}", local::monochrome, local::normalize);
                if (cancel)
                    return;

                images_panel->async_modify_selected(
                    [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                    {
                        // compute normalized weights
                        auto n_weights = local::weights;
                        for (size_t c = 0; c < n_weights.size(); ++c)
                        {
                            n_weights[c].a() = 0.f;
                            n_weights[c] /= local::normalize ? dot(n_weights[c], Color(1.f, 1.f)) : 1.f;
                        }

                        if (local::monochrome)
                        {
                            return {
                                make_shared<HDRImage>(img->apply_function(
                                    [&](const Color4 &c) { return Color4(dot((Color)c, n_weights[local::GRAY]), c.a); },
                                    xpuimg->roi())),
                                nullptr};
                        }
                        else
                        {
                            return {make_shared<HDRImage>(img->apply_function(
                                        [&](const Color4 &c)
                                        {
                                            Color  c2(c);
                                            Color4 result(dot(c2, n_weights[local::RED]),
                                                          dot(c2, n_weights[local::GREEN]),
                                                          dot(c2, n_weights[local::BLUE]), c2.a());
                                            return result;
                                        },
                                        xpuimg->roi())),
                                    nullptr};
                        }
                    });
            });

        gui->add_widget("", window->add_buttons("OK", "Cancel"));

        window->center();
        window->request_focus();
    };
}
