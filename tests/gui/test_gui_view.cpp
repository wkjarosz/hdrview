/** \file test_gui_view.cpp
    \author Wojciech Jarosz

    View-menu actions: zoom, the toggles, the exposure and gamma steps, the tonemap reset, and the exposure slider.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include <cmath>
#include <string>
#include <vector>

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#ifndef HDRVIEW_GUI_TEST_IMAGE_2
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a small fixture image path"
#endif

/// Starts from the small fixture alone, whatever an earlier test left loaded.
static void ensure_image_loaded(ImGuiTestContext *ctx)
{
    reset_images(ctx);
    IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE_2}), 1);
}

void RegisterTests_View(ImGuiTestEngine *engine)
{
    // "Zoom in"/"Zoom out" step between adjacent powers of two, from fractional zooms as well as exact
    // ones: fitting to the window and the scroll wheel both leave the zoom between two stops
    ImGuiTest *t = IM_REGISTER_TEST(engine, "view", "zoom_steps_between_powers_of_two");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        ensure_image_loaded(ctx);
        ctx->SetRef("##MainMenuBar");

        ctx->MenuClick("View/100%");
        IM_CHECK_EQ(hdrview()->zoom_level(), 0.f);
        const float one_to_one = hdrview()->zoom();

        // the menu round trip, driven the way a user drives it
        ctx->MenuClick("View/Zoom in");
        IM_CHECK_EQ(hdrview()->zoom(), 2.f * one_to_one);
        ctx->MenuClick("View/Zoom out");
        IM_CHECK_EQ(hdrview()->zoom(), one_to_one);
        ctx->MenuClick("View/Zoom out");
        IM_CHECK_EQ(hdrview()->zoom(), 0.5f * one_to_one);
        ctx->MenuClick("View/100%");
        IM_CHECK_EQ(hdrview()->zoom_level(), 0.f);

        // every start, exact or not, lands on the stop next to it, and one step back returns to the
        // interval it came from
        for (float from : {0.25f, 0.5f, 1.f, 2.f, 4.f, 0.75f, 1.5f, 2.3f, 3.f, 6.9f})
        {
            IM_CHECK_SILENT(from >= HDRViewApp::MIN_ZOOM && from <= HDRViewApp::MAX_ZOOM);
            const float above = std::exp2(std::floor(std::log2(from)) + 1.f);
            const float below = std::exp2(std::ceil(std::log2(from)) - 1.f);

            hdrview()->set_zoom(from);
            hdrview()->zoom_in();
            IM_CHECK_EQ(hdrview()->zoom(), above);
            hdrview()->zoom_out();
            IM_CHECK_EQ(hdrview()->zoom(), 0.5f * above);

            hdrview()->set_zoom(from);
            hdrview()->zoom_out();
            IM_CHECK_EQ(hdrview()->zoom(), below);
            hdrview()->zoom_in();
            IM_CHECK_EQ(hdrview()->zoom(), 2.f * below);
        }

        ctx->MenuClick("View/100%");
    };

    /*
        Every toggle the View menu offers flips its own setting and no other, and a second click puts it back; each
        stepper moves its own setting by its step, and its counterpart takes the step back; and Reset tonemapping puts
        the tonemapping back however it was left. The whole state is compared each time, so an item wired to the wrong
        setting fails as surely as one wired to none.
    */
    t           = IM_REGISTER_TEST(engine, "view", "each view menu item changes its own setting and no other");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        ensure_image_loaded(ctx);
        auto *app = hdrview();

        std::vector<std::string> toggles{"Auto fit display window", "Auto fit data window",
                                         "Auto fit selection",      "Mirror view horizontally",
                                         "Mirror view vertically",  "Draw pixel grid",
                                         "Draw pixel values",       "Draw data window",
                                         "Draw display window",     "Dither"};
        // offered, and enabled, only on a display that supports HDR
        if (app->action("Clamp to LDR").enabled())
            toggles.push_back("Clamp to LDR");

        // the fits exclude one another, so each is turned on from none; and fitting a selection needs one
        for (int i = 0; i < 3; ++i) *app->action(toggles[i]).p_selected = false;
        app->set_selection(Box2i{app->current_image()->data_window.min, app->current_image()->data_window.min + 8});
        app->tonemap()  = Tonemap_Gamma;
        app->exposure() = app->exposure_live() = 0.f;
        app->gamma() = app->gamma_live() = 1.f;
        OnExit restore{[ctx]
                       {
                           reset_view_controls(ctx);
                           hdrview()->tonemap() = Tonemap_Gamma;
                       }};

        struct State
        {
            std::vector<bool> on;
            float             exposure, offset, gamma;
            int               tonemap;
        };
        auto state = [&]
        {
            State s{{}, app->exposure(), app->offset(), app->gamma(), int(app->tonemap())};
            for (const auto &name : toggles) s.on.push_back(*app->action(name).p_selected);
            return s;
        };
        auto check = [&](const State &want)
        {
            const State s = state();
            IM_CHECK(s.on == want.on);
            IM_CHECK_LT(std::abs(s.exposure - want.exposure), 1e-6f);
            IM_CHECK_LT(std::abs(s.offset - want.offset), 1e-6f);
            IM_CHECK_LT(std::abs(s.gamma - want.gamma), 1e-6f);
            IM_CHECK_EQ(s.tonemap, want.tonemap);
        };
        ctx->SetRef("##MainMenuBar");

        for (size_t i = 0; i < toggles.size(); ++i)
        {
            ctx->LogInfo("%s", toggles[i].c_str());
            IM_CHECK(app->action(toggles[i]).enabled());
            const State before  = state();
            State       flipped = before;
            flipped.on[i]       = !flipped.on[i];

            ctx->MenuClick(("View/" + toggles[i]).c_str());
            check(flipped);
            ctx->MenuClick(("View/" + toggles[i]).c_str());
            check(before);
        }

        // the steppers, each undone by its counterpart; the gamma steps in the Gamma tonemap, where it is a gamma
        struct Step
        {
            const char *up, *down;
            float State::*field;
            float         by;
        };
        for (const Step &step : {Step{"View/Increase exposure", "View/Decrease exposure", &State::exposure, 0.25f},
                                 Step{"View/Increase gamma\\/Next colormap", "View/Decrease gamma\\/Previous colormap",
                                      &State::gamma, 0.02f}})
        {
            ctx->LogInfo("%s", step.up);
            const State before = state();
            State       up     = before;
            up.*step.field += step.by;
            ctx->MenuClick(step.up);
            check(up);
            ctx->MenuClick(step.down);
            check(before);
        }

        // and the reset, from everything it resets moved, leaving the toggles alone
        const State before = state();
        app->exposure()    = 1.5f;
        app->offset()      = 0.3f;
        app->gamma()       = 2.2f;
        app->tonemap()     = Tonemap_FalseColor;
        ctx->MenuClick("View/Reset tonemapping");
        check(State{before.on, 0.f, 0.f, 1.f, int(Tonemap_Gamma)});
    };

    t           = IM_REGISTER_TEST(engine, "view", "the exposure slider sets the exposure");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        ensure_image_loaded(ctx);
        OnExit restore{[ctx] { reset_view_controls(ctx); }};

        // the exposure slider lives in the top edge toolbar, its own floating window whose internal name is
        // baked into Hello ImGui (docking_details.cpp: "##" + EdgeToolbarTypeName(Top) + "_2123243").
        // Addressing it directly lets Test Engine bring it to front before interacting with the item.
        const float target_exposure = 2.5f;
        ctx->SetRef("##Top_2123243");
        ctx->ItemInputValue("##ExposureSlider", target_exposure);
        IM_CHECK_EQ(hdrview()->exposure(), target_exposure);
    };
}
