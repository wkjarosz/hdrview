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

function<void(float)> create_floatbox_and_slider(FormHelper *gui, string name, float &variable, float mn, float mx,
                                                 float step, function<void(void)> cb, string help)
{
    auto box = gui->add_variable(name, variable);
    box->set_spinnable(true);
    box->number_format("%3.2f");
    box->set_value_increment(step);
    box->set_min_max_values(mn, mx);
    box->set_fixed_width(65);
    box->set_tooltip(help);

    auto slider = new Slider(gui->window());
    slider->set_value(variable);
    slider->set_range({mn, mx});
    slider->set_tooltip(help);
    gui->add_widget("", slider);

    auto f_cb = [box, slider, cb, &variable](float v)
    {
        variable = v;
        box->set_value(v);
        slider->set_value(v);
        cb();
    };
    slider->set_callback(f_cb);
    box->set_callback(f_cb);
    return f_cb;
}

Widget *create_anchor_widget(Widget *window, HDRImage::CanvasAnchor &anchor, int bw)
{
    auto row = new Widget(window);
    int  pad = 2;
    row->set_layout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, pad));
    vector<Button *> button_group;

    int icons[] = {FA_PLUS, FA_GRIP_LINES, FA_PLUS, FA_GRIP_LINES_VERTICAL, FA_PLUS, FA_GRIP_LINES_VERTICAL,
                   FA_PLUS, FA_GRIP_LINES, FA_PLUS};

    for (size_t i = 0; i < sizeof(icons) / sizeof(icons[0]); ++i)
    {
        Button *btn = new Button(row, "", icons[i]);

        btn->set_flags(Button::RadioButton);
        btn->set_fixed_size(Vector2i(bw, bw));
        btn->set_pushed(i == (size_t)anchor);
        btn->set_change_callback(
            [i, &anchor](bool b)
            {
                if (b)
                    anchor = (HDRImage::CanvasAnchor)i;
            });
        button_group.push_back(btn);
    }

    row->set_fixed_size(Vector2i(3 * bw + 2 * pad, 3 * bw + 2 * pad));
    return row;
}