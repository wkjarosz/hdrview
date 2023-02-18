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
#include <ImathMatrix.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;
using Imath::M33f;
using Imath::V2f;
using Imath::V3f;

static const string           name{"Transform..."};
static float                  translate_x = 0, translate_y = 0;
static float                  scale_x = 100.0f, scale_y = 100.0f;
static bool                   uniform_scale = true;
static float                  angle         = 0.0f;
static bool                   cw            = false;
static float                  shear_x = 0, shear_y = 0;
static HDRImage::Sampler      sampler       = HDRImage::BILINEAR;
static HDRImage::BorderMode   border_mode_x = HDRImage::REPEAT, border_mode_y = HDRImage::REPEAT;
static HDRImage::CanvasAnchor anchor  = HDRImage::MIDDLE_CENTER;
static int                    samples = 1;

std::function<void()> free_xform_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(0, 20));

        auto window = new Dialog(screen, name);
        gui->set_window(window);

        auto row = new Widget(window);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        auto x = new FloatBox<float>(row, translate_x);
        x->set_spinnable(true);
        x->set_enabled(true);
        x->set_editable(true);
        x->set_font_size(gui->widget_font_size());
        x->set_fixed_size(Vector2i(65 + 12, gui->fixed_size().y()));
        x->set_alignment(TextBox::Alignment::Right);
        x->set_units("px");
        x->set_callback([](float v) { translate_x = v; });
        x->set_tooltip("Set horizontal translation.");

        auto y = new FloatBox<float>(row, translate_y);
        y->set_spinnable(true);
        y->set_enabled(true);
        y->set_editable(true);
        y->set_font_size(gui->widget_font_size());
        y->set_fixed_size(Vector2i(65 + 13, gui->fixed_size().y()));
        y->set_alignment(TextBox::Alignment::Right);
        y->set_units("px");
        y->set_callback([](float v) { translate_y = v; });
        y->set_tooltip("Set vertical translation.");

        gui->add_widget("Translate:", row);

        auto spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        row = new Widget(window);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        auto w    = new FloatBox<float>(row, scale_x);
        auto link = new ToolButton(row, FA_LINK);
        auto h    = new FloatBox<float>(row, scale_y);

        w->set_spinnable(true);
        w->set_enabled(true);
        w->set_editable(true);
        w->set_font_size(gui->widget_font_size());
        w->set_fixed_size(Vector2i(65, gui->fixed_size().y()));
        w->set_alignment(TextBox::Alignment::Right);
        w->set_units("%");
        w->set_tooltip("Set horizontal scale.");
        w->set_callback(
            [h](float v)
            {
                scale_x = v;
                if (uniform_scale)
                    scale_y = scale_x;
                h->set_value(scale_y);
            });

        link->set_fixed_size(Vector2i(20, 20));
        link->set_pushed(uniform_scale);
        link->set_tooltip("Lock the X and Y scale factors to maintain aspect ratio.");
        link->set_change_callback(
            [w, h](bool b)
            {
                uniform_scale = b;
                if (uniform_scale)
                    scale_x = scale_y;
                w->set_value(scale_x);
                h->set_value(scale_y);
            });

        h->set_spinnable(true);
        h->set_enabled(true);
        h->set_editable(true);
        h->set_font_size(gui->widget_font_size());
        h->set_fixed_size(Vector2i(65, gui->fixed_size().y()));
        h->set_alignment(TextBox::Alignment::Right);
        h->set_units("%");
        h->set_tooltip("Set vertical scale.");
        h->set_callback(
            [w](float v)
            {
                scale_y = v;
                if (uniform_scale)
                    scale_x = scale_y;
                w->set_value(scale_x);
            });

        gui->add_widget("Scale:", row);

        spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        row = new Widget(window);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        auto a = new FloatBox<float>(row, angle);
        a->set_spinnable(true);
        a->set_enabled(true);
        a->set_editable(true);
        a->set_font_size(gui->widget_font_size());
        a->set_fixed_size(Vector2i(160 - 2 * 25, gui->fixed_size().y()));
        a->set_alignment(TextBox::Alignment::Right);
        a->set_units("°");
        a->set_tooltip("Set rotation angle in degrees.");
        a->set_callback([](float v) { angle = v; });

        auto ccww = new Button(row, "", FA_UNDO);
        ccww->set_fixed_size(Vector2i(20, 20));
        ccww->set_flags(Button::Flags::RadioButton);
        ccww->set_pushed(!cw);
        ccww->set_tooltip("Rotate in the counter-clockwise direction.");
        ccww->set_change_callback([](bool b) { cw = !b; });

        auto cww = new Button(row, "", FA_REDO);
        cww->set_fixed_size(Vector2i(20, 20));
        cww->set_flags(Button::Flags::RadioButton);
        cww->set_pushed(cw);
        cww->set_tooltip("Rotate in the clockwise direction.");
        cww->set_change_callback([](bool b) { cw = b; });

        gui->add_widget("Rotate:", row);

        spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        row = new Widget(window);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

        auto shx = new FloatBox<float>(row, shear_x);
        shx->set_spinnable(true);
        shx->set_enabled(true);
        shx->set_editable(true);
        shx->set_font_size(gui->widget_font_size());
        shx->set_fixed_size(Vector2i(65 + 12, gui->fixed_size().y()));
        shx->set_alignment(TextBox::Alignment::Right);
        shx->set_units("°");
        shx->set_tooltip("Set horizontal skew/shear in degrees.");
        shx->set_callback([](float v) { shear_x = v; });

        auto shy = new FloatBox<float>(row, shear_y);
        shy->set_spinnable(true);
        shy->set_enabled(true);
        shy->set_editable(true);
        shy->set_font_size(gui->widget_font_size());
        shy->set_fixed_size(Vector2i(65 + 13, gui->fixed_size().y()));
        shy->set_alignment(TextBox::Alignment::Right);
        shy->set_units("°");
        shy->set_tooltip("Set vertical skew/shear in degrees.");
        shy->set_callback([](float v) { shear_y = v; });

        gui->add_widget("Shear:", row);

        spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        gui->add_widget("Reference point:", create_anchor_widget(window, anchor, gui->fixed_size().y()));

        spacer = new Widget(window);
        spacer->set_fixed_height(10);
        gui->add_widget("", spacer);

        add_dropdown(gui, "Sampler:", sampler, HDRImage::sampler_names());
        add_dropdown(gui, "Border mode X:", border_mode_x, HDRImage::border_mode_names());
        add_dropdown(gui, "Border mode Y:", border_mode_y, HDRImage::border_mode_names());

        auto s = gui->add_variable("Super-samples:", samples);
        s->set_spinnable(true);
        s->set_min_value(1);

        screen->request_layout_update();

        spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [images_panel](int cancel)
            {
                if (cancel)
                    return;

                images_panel->async_modify_selected(
                    [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                        AtomicProgress &progress) -> ImageCommandResult
                    {
                        Imath::M33f t;
                        Imath::V2f  origin(0.f, 0.f);

                        // find top-left corner
                        switch (anchor)
                        {
                        case HDRImage::TOP_RIGHT:
                        case HDRImage::MIDDLE_RIGHT:
                        case HDRImage::BOTTOM_RIGHT: origin.x = 1.f; break;

                        case HDRImage::TOP_CENTER:
                        case HDRImage::MIDDLE_CENTER:
                        case HDRImage::BOTTOM_CENTER: origin.x = 0.5f; break;

                        case HDRImage::TOP_LEFT:
                        case HDRImage::MIDDLE_LEFT:
                        case HDRImage::BOTTOM_LEFT:
                        default: origin.x = 0.f; break;
                        }
                        switch (anchor)
                        {
                        case HDRImage::BOTTOM_LEFT:
                        case HDRImage::BOTTOM_CENTER:
                        case HDRImage::BOTTOM_RIGHT: origin.y = 1.f; break;

                        case HDRImage::MIDDLE_LEFT:
                        case HDRImage::MIDDLE_CENTER:
                        case HDRImage::MIDDLE_RIGHT: origin.y = 0.5f; break;

                        case HDRImage::TOP_LEFT:
                        case HDRImage::TOP_CENTER:
                        case HDRImage::TOP_RIGHT:
                        default: origin.y = 0.f; break;
                        }

                        t.translate(origin);
                        t.scale(V2f(1.f / img->width(), 1.f / img->height()));
                        t.translate(V2f(translate_x, translate_y));
                        t = M33f().setRotation(cw ? angle / 180.f * M_PI : -angle / 180.f * M_PI) * t;
                        // t.rotate(cw ? angle/180.f * M_PI : -angle/180.f * M_PI);
                        t.shear(V2f(tan(shear_x / 180.f * M_PI), tan(shear_y / 180.f * M_PI)));
                        t.scale(V2f(scale_x, scale_y) * .01f);
                        t.scale(V2f(img->width(), img->height()));
                        t.translate(-origin);
                        t.invert();

                        // t.translate(origin);
                        // t.scale(V2f(1.f/img->width(), 1.f/img->height()));
                        // t.scale(V2f(1.f/scale_x, 1.f/scale_y)*100.f);
                        // t.shear(V2f(-tan(shear_x/180.f * M_PI), -tan(shear_y/180.f * M_PI)));
                        // t = M33f().setRotation(cw ? -angle/180.f * M_PI : angle/180.f * M_PI) * t;
                        // t.translate(V2f(-translate_x, -translate_y));
                        // t.scale(V2f(img->width(), img->height()));
                        // t.translate(-origin);

                        function<Vector2f(const Vector2f &)> warp = [t](const Vector2f &uv)
                        {
                            V2f uvh(uv.x(), uv.y());
                            V2f res_h;
                            t.multVecMatrix(uvh, res_h);
                            return Vector2f(res_h.x, res_h.y);
                        };
                        return {make_shared<HDRImage>(img->resampled(img->width(), img->height(), progress, warp,
                                                                     samples, sampler, border_mode_x, border_mode_y)),
                                nullptr};
                    });
            });

        gui->add_widget("", window->add_buttons("OK", "Cancel"));

        window->center();
        window->request_focus();
    };
}
