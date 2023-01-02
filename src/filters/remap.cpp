//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "filters/filters.h"

#include "common.h"
#include "dialog.h"
#include "envmap.h"
#include "hdrimage.h"
#include "hdrviewscreen.h"
#include "imagelistpanel.h"
#include "xpuimage.h"
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;

static const string         name{"Remap..."};
static EEnvMappingUVMode    from = ANGULAR_MAP, to = ANGULAR_MAP;
static HDRImage::Sampler    sampler = HDRImage::BILINEAR;
static int                  width = 128, height = 128;
static bool                 autoAspect    = true;
static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
static int                  samples        = 1;
static const float          auto_aspects[] = {1.f, 1.f, 2.f, 2.f, 0.75f, 1.f};

std::function<void()> remap_callback(HDRViewScreen *screen, ImageListPanel *images_panel)
{
    return [&, screen, images_panel]()
    {
        FormHelper *gui = new FormHelper(screen);
        gui->set_fixed_size(Vector2i(135, 20));

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

        auto recompute_w = []()
        {
            if (autoAspect)
                width = max(1, (int)round(height * auto_aspects[to]));
        };
        auto recompute_h = []()
        {
            if (autoAspect)
                height = max(1, (int)round(width / auto_aspects[to]));
        };

        w->set_callback(
            [h, recompute_h](int w)
            {
                width = w;
                recompute_h();
                h->set_value(height);
            });

        h->set_callback(
            [w, recompute_w](int h)
            {
                height = h;
                recompute_w();
                w->set_value(width);
            });

        auto auto_aspect_checkbox = gui->add_variable("Auto aspect ratio:", autoAspect, true);

        const std::function<void(const EEnvMappingUVMode &)> cb = [gui, recompute_w](const EEnvMappingUVMode &)
        {
            recompute_w();
            gui->refresh();
        };

        auto src = add_dropdown(gui, "Source map:", from, envMappingNames(), cb);
        auto dst = add_dropdown(gui, "Target map:", to, envMappingNames(), cb);

        auto spacer = new Widget(window);
        spacer->set_fixed_height(5);
        gui->add_widget("", spacer);

        auto btn = new Button(window, "Swap source/target", FA_EXCHANGE_ALT);
        btn->set_callback(
            [gui, recompute_w, recompute_h, src, dst]()
            {
                std::swap(from, to);
                recompute_w();
                recompute_h();
                gui->refresh();
                EEnvMappingUVMode src_v = from, src_current = (EEnvMappingUVMode)src->selected_index();
                EEnvMappingUVMode dst_v = to, dst_current = (EEnvMappingUVMode)dst->selected_index();

                if (src_v != src_current)
                    src->set_selected_index((int)src_v);

                if (dst_v != dst_current)
                    dst->set_selected_index((int)dst_v);
            });
        btn->set_fixed_size(gui->fixed_size());
        gui->add_widget(" ", btn);

        auto_aspect_checkbox->set_callback(
            [w, recompute_w](bool preserve)
            {
                autoAspect = preserve;
                recompute_w();
                w->set_value(width);
            });

        recompute_w();
        gui->refresh();

        add_dropdown(gui, "Sampler:", sampler, HDRImage::sampler_names());
        add_dropdown(gui, "Border mode X:", border_mode_x, HDRImage::border_mode_names());
        add_dropdown(gui, "Border mode Y:", border_mode_y, HDRImage::border_mode_names());

        w = gui->add_variable("Super-samples:", samples);
        w->set_spinnable(true);
        w->set_min_value(1);

        screen->request_layout_update();

        spacer = new Widget(window);
        spacer->set_fixed_height(15);
        gui->add_widget("", spacer);

        window->set_callback(
            [&](int cancel)
            {
                if (cancel)
                    return;

                auto warp = [](const Vector2f &uv) { return convertEnvMappingUV(from, to, uv); };

                images_panel->async_modify_selected(
                    [&](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                        AtomicProgress &progress) -> ImageCommandResult
                    {
                        return {make_shared<HDRImage>(img->resampled(width, height, progress, warp, samples, sampler,
                                                                     border_mode_x, border_mode_y)),
                                nullptr};
                    });
            });

        gui->add_widget("", window->add_buttons());

        window->center();
        window->request_focus();
    };
}
