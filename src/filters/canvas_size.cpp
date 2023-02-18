//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h" // for create_anchor_widget, create_canvas_...

#include "color.h"           // for Color4
#include "commandhistory.h"  // for ImageCommandResult
#include "dialog.h"          // for Dialog
#include "hdrcolorpicker.h"  // for HDRColorPicker
#include "hdrimage.h"        // for HDRImage, HDRImage::CanvasAnchor
#include "hdrviewscreen.h"   // for HDRViewScreen
#include "imagelistpanel.h"  // for ImageListPanel
#include <cmath>             // for pow
#include <functional>        // for __base
#include <fwd.h>             // for ConstHDRImagePtr, ConstXPUImagePtr
#include <memory>            // for shared_ptr, make_shared, shared_ptr<...
#include <nanogui/nanogui.h> // for widgets
#include <sstream>           // for basic_stringbuf<>::int_type, basic_s...
#include <utility>           // for pair

using namespace std;

static const string           name{"Canvas size..."};
static int                    width = 128, height = 128;
static Color                  bg       = Color(0, 0);
static float                  EV       = 0.f;
static HDRImage::CanvasAnchor anchor   = HDRImage::MIDDLE_CENTER;
static bool                   relative = false;

std::function<void()> canvas_size_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(75, 20));

        auto window = new Dialog(screen, name);
        gui->set_window(window);

        width  = images_panel->current_image()->width();
        auto w = gui->add_variable("Width:", width);
        w->set_spinnable(true);
        w->set_min_value(1);
        w->set_units("px");

        height = images_panel->current_image()->height();
        auto h = gui->add_variable("Height:", height);
        h->set_spinnable(true);
        h->set_min_value(1);
        h->set_units("px");

        relative = false;
        auto r   = gui->add_variable("Relative:", relative, true);
        r->set_callback(
            [w, h, images_panel](bool rel)
            {
                if (rel)
                {
                    w->set_min_value(-images_panel->current_image()->width() + 1);
                    h->set_min_value(-images_panel->current_image()->height() + 1);
                    width  = w->value() - images_panel->current_image()->width();
                    height = h->value() - images_panel->current_image()->height();
                    w->set_value(width);
                    h->set_value(height);
                }
                else
                {
                    w->set_min_value(1);
                    h->set_min_value(1);
                    width  = w->value() + images_panel->current_image()->width();
                    height = h->value() + images_panel->current_image()->height();
                    w->set_value(width);
                    h->set_value(height);
                }
                relative = rel;
            });

        auto spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        gui->add_widget("Anchor:", create_anchor_widget(window, anchor, gui->fixed_size().y()));

        spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        bg             = screen->background()->color();
        EV             = screen->background()->exposure();
        auto color_btn = new HDRColorPicker(window, bg, EV);
        color_btn->popup()->set_anchor_offset(color_btn->popup()->height());
        color_btn->set_eyedropper_callback([screen, color_btn](bool pushed)
                                           { screen->set_active_colorpicker(pushed ? color_btn : nullptr); });
        gui->add_widget("Background color:", color_btn);
        color_btn->set_final_callback(
            [](const Color &c, float e)
            {
                bg = c;
                EV = e;
            });

        auto popup = color_btn->popup();
        screen->request_layout_update();

        spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [images_panel, popup](int cancel)
            {
                popup->set_visible(false);

                if (cancel)
                    return;

                images_panel->async_modify_selected(
                    [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                    {
                        int newW = relative ? width + img->width() : width;
                        int newH = relative ? height + img->height() : height;

                        float  gain = pow(2.f, EV);
                        Color4 c(bg.r() * gain, bg.g() * gain, bg.b() * gain, bg.a());

                        return {make_shared<HDRImage>(img->resized_canvas(newW, newH, anchor, c)), nullptr};
                    });
            });

        gui->add_widget("", window->add_buttons("OK", "Cancel"));

        window->center();
        window->request_focus();
    };
}
