//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h" // for create_fill_btn

#include "color.h"          // for Color4
#include "colorslider.h"    // for ColorSlider, ColorSlider::ColorMode
#include "commandhistory.h" // for ImageCommandResult
#include "dialog.h"         // for Dialog
#include "fwd.h"            // for ConstHDRImagePtr, ConstXPUImagePtr
#include "hdrimage.h"       // for HDRImage
#include "hdrviewscreen.h"  // for HDRViewScreen
#include "imagelistpanel.h" // for ImageListPanel
#include <array>            // for array
#include <functional>       // for __base
#include <iosfwd>           // for string
#include <memory>           // for shared_ptr, make_shared
#include <nanogui/nanogui.h>
#include <stddef.h> // for size_t
#include <utility>  // for pair
#include <vector>   // for vector

using namespace std;

Button *create_fill_btn(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel)
{
    static string              name    = "Fill...";
    static std::array<bool, 4> enabled = {true, true, true, true};
    // static Color value(0.8f);
    static Color value(0.0f, 1.f);
    auto         b = new Button(parent, name, FA_FILL);
    b->set_fixed_height(21);
    b->set_callback(
        [&, screen, images_panel]()
        {
            FormHelper *gui = new FormHelper(screen);
            gui->set_fixed_size(Vector2i(200, 20));

            auto window = new Dialog(screen, name);
            gui->set_window(window);

            auto row    = new Widget(window);
            auto layout = new GridLayout(Orientation::Horizontal, 4, Alignment::Middle, 0, 5);
            layout->set_col_alignment({Alignment::Maximum, Alignment::Fill, Alignment::Fill, Alignment::Minimum});
            row->set_layout(layout);

            string                         names[] = {"Red :", "Green : ", "Blue :", "Alpha :"};
            std::vector<ColorSlider *>     sliders;
            std::vector<FloatBox<float> *> float_boxes;
            for (int i = 0; i < 4; ++i)
            {
                new Label(row, names[i], "sans-bold");

                auto slider = new ColorSlider(row, value, ColorSlider::ColorMode(i));
                slider->set_color(value);
                slider->set_value(value[i]);
                slider->set_range({0.f, 1.f});
                slider->set_fixed_width(250);
                slider->set_enabled(enabled[i]);

                auto box = new FloatBox(row, value[i]);
                box->set_spinnable(true);
                box->number_format("%3.2f");
                box->set_min_max_values(0.f, 1.f);
                box->set_fixed_width(50);
                box->set_enabled(enabled[i]);
                box->set_units("");
                box->set_alignment(TextBox::Alignment::Right);

                sliders.push_back(slider);
                float_boxes.push_back(box);

                (new CheckBox(row, "",
                              [&, i, box, slider](const bool &b)
                              {
                                  enabled[i] = b;
                                  box->set_enabled(b);
                                  slider->set_enabled(b);
                              }))
                    ->set_checked(enabled[i]);
            }

            for (size_t i = 0; i < sliders.size(); ++i)
            {
                auto cb = [i, float_boxes, sliders](float v)
                {
                    value[i] = v;
                    float_boxes[i]->set_value(v);
                    sliders[i]->set_value(v);
                    for (auto slider : sliders) slider->set_color(value);
                };

                sliders[i]->set_callback(cb);
                float_boxes[i]->set_callback(cb);
            }

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
                        [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                        {
                            return {make_shared<HDRImage>(img->apply_function(
                                        [&](const Color4 &c)
                                        {
                                            return Color4(enabled[0] ? value[0] : c[0], enabled[1] ? value[1] : c[1],
                                                          enabled[2] ? value[2] : c[2], enabled[3] ? value[3] : c[3]);
                                        },
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
