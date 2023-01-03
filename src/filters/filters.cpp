//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h"
#include "hdrviewscreen.h"
#include "imagelistpanel.h"

#include <nanogui/nanogui.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace nanogui;

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

std::function<void()> invert_callback(ImageListPanel *images_panel)
{
    return [images_panel]
    {
        images_panel->async_modify_selected(
            [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto roi = xpuimg->roi();
                return {make_shared<HDRImage>(img->inverted(roi)),
                        make_shared<LambdaUndo>([roi](HDRImagePtr &img2) { *img2 = img2->inverted(roi); })};
            });
    };
}

std::function<void()> clamp_callback(ImageListPanel *images_panel)
{
    return [images_panel]
    {
        images_panel->async_modify_selected(
            [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                return {
                    make_shared<HDRImage>(img->apply_function(
                        [](const Color4 &c) { return Color4(clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a)); },
                        xpuimg->roi())),
                    nullptr};
            });
    };
}

std::function<void()> clamp_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [screen, images_panel]
    {
        images_panel->async_modify_selected(
            [screen](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                Color  nbg = screen->background()->color();
                Color4 bg(nbg.r(), nbg.g(), nbg.b(), nbg.a());
                return {make_shared<HDRImage>(
                            img->apply_function([&bg](const Color4 &c) { return c.over(bg); }, xpuimg->roi())),
                        nullptr};
            });
    };
}

std::function<void()> crop_callback(ImageListPanel *images_panel)
{
    return [images_panel]
    {
        images_panel->async_modify_selected(
            [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto roi = xpuimg->roi();
                if (!roi.has_volume())
                    roi = img->box();
                HDRImage result(roi.size().x(), roi.size().y());
                result.copy_paste(*img, roi, 0, 0);
                xpuimg->roi() = Box2i();
                return {make_shared<HDRImage>(result), nullptr};
            });
    };
}

std::function<void()> bump_to_normal_map_callback(ImageListPanel *images_panel)
{
    return [images_panel]
    {
        images_panel->async_modify_selected(
            [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
               AtomicProgress &progress) -> ImageCommandResult
            {
                return {make_shared<HDRImage>(
                            img->bump_to_normal_map(1.f, progress, HDRImage::EDGE, HDRImage::EDGE, xpuimg->roi())),
                        nullptr};
            });
    };
}

std::function<void()> irradiance_envmap_callback(ImageListPanel *images_panel)
{
    return [images_panel]
    {
        images_panel->async_modify_selected(
            [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                AtomicProgress &progress) -> ImageCommandResult {
                return {make_shared<HDRImage>(img->irradiance_envmap(progress)), nullptr};
            });
    };
}

std::function<void()> cut_callback(HDRImagePtr &clipboard, ImageListPanel *images_panel)
{
    return [&clipboard, images_panel]
    {
        auto img = images_panel->current_image();
        if (!img)
            return;

        auto roi = img->roi();
        if (!roi.has_volume())
            roi = img->box();

        clipboard = make_shared<HDRImage>(roi.size().x(), roi.size().y());
        clipboard->copy_paste(img->image(), roi, 0, 0, true);

        images_panel->async_modify_current(
            [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                return {make_shared<HDRImage>(img->apply_function(
                            [](const Color4 &c) { return Color4(c.r, c.g, c.b, 0.f); }, xpuimg->roi())),
                        nullptr};
            });
    };
}

std::function<void()> copy_callback(HDRImagePtr &clipboard, ImageListPanel *images_panel)
{
    return [&clipboard, images_panel]
    {
        auto img = images_panel->current_image();
        if (!img)
            return;

        auto roi = img->roi();
        if (!roi.has_volume())
            roi = img->box();

        clipboard = make_shared<HDRImage>(roi.size().x(), roi.size().y());
        clipboard->copy_paste(img->image(), roi, 0, 0, true);
    };
}

std::function<void()> paste_callback(HDRImagePtr &clipboard, ImageListPanel *images_panel)
{
    return [&clipboard, images_panel]
    {
        auto img = images_panel->current_image();

        if (!img)
            return;

        auto roi = img->roi();
        if (!roi.has_volume())
            roi = img->box();

        images_panel->async_modify_current(
            [&clipboard, roi](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                auto result = make_shared<HDRImage>(*img);
                result->copy_paste(*clipboard, Box2i(), roi.min.x(), roi.min.y());
                return {result, nullptr};
            });
    };
}

std::function<void()> seamless_paste_callback(HDRImagePtr &clipboard, ImageListPanel *images_panel)
{
    return [&clipboard, images_panel]
    {
        auto img = images_panel->current_image();

        if (!img)
            return;

        auto roi = img->roi();
        if (!roi.has_volume())
            roi = img->box();

        images_panel->async_modify_current(
            [clipboard, roi](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                             AtomicProgress &progress) -> ImageCommandResult
            {
                auto result = make_shared<HDRImage>(*img);
                result->seamless_copy_paste(progress, *clipboard, Box2i(), roi.min.x(), roi.min.y());
                return {result, nullptr};
            });
    };
}

std::function<void()> rotate_callback(bool clockwise, ImageListPanel *images_panel)
{
    return [clockwise, images_panel]
    {
        images_panel->async_modify_selected(
            [clockwise](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                return {make_shared<HDRImage>(clockwise ? img->rotated_90_cw() : img->rotated_90_ccw()),
                        make_shared<LambdaUndo>([clockwise](HDRImagePtr &img2)
                                                { *img2 = clockwise ? img2->rotated_90_ccw() : img2->rotated_90_cw(); },
                                                [clockwise](HDRImagePtr &img2) {
                                                    *img2 = clockwise ? img2->rotated_90_cw() : img2->rotated_90_ccw();
                                                })};
            });
    };
}

std::function<void()> flip_callback(bool horizontal, ImageListPanel *images_panel)
{
    return [horizontal, images_panel]
    {
        images_panel->async_modify_selected(
            [horizontal](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
            {
                return {make_shared<HDRImage>(horizontal ? img->flipped_horizontal() : img->flipped_vertical()),
                        make_shared<LambdaUndo>(
                            [horizontal](HDRImagePtr &img2)
                            { *img2 = horizontal ? img2->flipped_horizontal() : img2->flipped_vertical(); })};
            });
    };
}
