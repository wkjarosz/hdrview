/** \file test_gui_view.cpp
    \author Wojciech Jarosz

    View-menu actions: zoom, flip, tonemap reset, exposure stepping, and pixel-grid/pixel-value toggles.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include <cmath>

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

static void ensure_image_loaded(ImGuiTestContext *ctx)
{
    if (hdrview()->num_images() == 0)
    {
        hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
        wait_for_loads(ctx);
    }
    IM_CHECK(hdrview()->num_images() > 0);
}

void RegisterTests_View(ImGuiTestEngine *engine)
{
    // "Zoom in"/"Zoom out" step between adjacent powers of two, and back. Both directions are checked from
    // fractional zooms as well as exact ones: fitting to the window and the scroll wheel both leave the zoom
    // between two stops, and from there neither direction may skip the stop it is standing next to.
    ImGuiTest *t = IM_REGISTER_TEST(engine, "view", "zoom_steps_between_powers_of_two");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        ensure_image_loaded(ctx);
        ctx->SetRef("##MainMenuBar");

        ctx->MenuClick("View/100%");
        IM_CHECK_EQ(hdrview()->zoom_level(), 0.f);
        const float one_to_one = hdrview()->zoom();

        // The menu round trip, driven the way a user drives it.
        ctx->MenuClick("View/Zoom in");
        IM_CHECK_EQ(hdrview()->zoom(), 2.f * one_to_one);
        ctx->MenuClick("View/Zoom out");
        IM_CHECK_EQ(hdrview()->zoom(), one_to_one);
        ctx->MenuClick("View/Zoom out");
        IM_CHECK_EQ(hdrview()->zoom(), 0.5f * one_to_one);
        ctx->MenuClick("View/100%");
        IM_CHECK_EQ(hdrview()->zoom_level(), 0.f);

        // Every start, exact or not, lands on the stop next to it -- and one step back returns to the
        // interval it came from.
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

    // That the two menu items toggle the two axes. Where the image then lands is the "viewport" tests'
    // subject: they state the placement against a specification rather than against another part of the
    // transform, and there is only one flip state for a menu click and an action-pointer write to reach.
    t           = IM_REGISTER_TEST(engine, "view", "flip_toggle");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        ensure_image_loaded(ctx);
        bool orig_h = *hdrview()->action("Flip horizontally").p_selected;
        bool orig_v = *hdrview()->action("Flip vertically").p_selected;

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Flip horizontally");
        IM_CHECK_EQ(*hdrview()->action("Flip horizontally").p_selected, !orig_h);
        ctx->MenuClick("View/Flip vertically");
        IM_CHECK_EQ(*hdrview()->action("Flip vertically").p_selected, !orig_v);

        // restore
        ctx->MenuClick("View/Flip horizontally");
        ctx->MenuClick("View/Flip vertically");
        IM_CHECK_EQ(*hdrview()->action("Flip horizontally").p_selected, orig_h);
        IM_CHECK_EQ(*hdrview()->action("Flip vertically").p_selected, orig_v);
    };

    t           = IM_REGISTER_TEST(engine, "view", "reset_tonemapping");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        hdrview()->exposure() = 1.5f;
        hdrview()->offset()   = 0.3f;
        hdrview()->gamma()    = 2.2f;
        hdrview()->tonemap()  = Tonemap_FalseColor;

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Reset tonemapping");

        IM_CHECK_EQ(hdrview()->exposure(), 0.f);
        IM_CHECK_EQ(hdrview()->offset(), 0.f);
        IM_CHECK_EQ(hdrview()->gamma(), 1.f);
        IM_CHECK(hdrview()->tonemap() == Tonemap_Gamma);
    };

    t           = IM_REGISTER_TEST(engine, "view", "exposure_increment");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Reset tonemapping");

        ctx->MenuClick("View/Increase exposure");
        IM_CHECK_EQ(hdrview()->exposure(), 0.25f);

        ctx->MenuClick("View/Decrease exposure");
        ctx->MenuClick("View/Decrease exposure");
        IM_CHECK_EQ(hdrview()->exposure(), -0.25f);

        ctx->MenuClick("View/Reset tonemapping");
    };

    t           = IM_REGISTER_TEST(engine, "view", "draw_grid_and_pixel_values_toggle");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        bool orig_grid = hdrview()->draw_grid_on();
        bool orig_pix  = hdrview()->draw_pixel_info_on();

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Draw pixel grid");
        IM_CHECK_EQ(hdrview()->draw_grid_on(), !orig_grid);
        ctx->MenuClick("View/Draw pixel values");
        IM_CHECK_EQ(hdrview()->draw_pixel_info_on(), !orig_pix);

        // restore
        ctx->MenuClick("View/Draw pixel grid");
        ctx->MenuClick("View/Draw pixel values");
        IM_CHECK_EQ(hdrview()->draw_grid_on(), orig_grid);
        IM_CHECK_EQ(hdrview()->draw_pixel_info_on(), orig_pix);
    };
}
