/** \file test_gui_view.cpp
    \author Wojciech Jarosz

    View-menu actions: zoom, flip, tonemap reset, exposure stepping, and pixel-grid/pixel-value toggles.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

static void ensure_image_loaded(ImGuiTestContext *ctx)
{
    if (hdrview()->num_images() == 0)
    {
        hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
    }
    IM_CHECK(hdrview()->num_images() > 0);
}

void RegisterTests_View(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "view", "zoom_round_trip");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        ensure_image_loaded(ctx);
        ctx->SetRef("##MainMenuBar");

        ctx->MenuClick("View/100%");
        IM_CHECK_EQ(hdrview()->zoom_level(), 0.f);

        ctx->MenuClick("View/Zoom in");
        IM_CHECK(hdrview()->zoom_level() > 0.f);
        float zoomed_in = hdrview()->zoom_level();

        ctx->MenuClick("View/Zoom out");
        IM_CHECK(hdrview()->zoom_level() < zoomed_in);

        ctx->MenuClick("View/100%");
        IM_CHECK_EQ(hdrview()->zoom_level(), 0.f);
    };

    // Flipping mirrors the image inside the rectangle it already occupies, so the two menu items toggle
    // their state without the image moving or resizing on screen. The transforms behind that rectangle are
    // exercised in their own right by the "viewport" tests.
    t           = IM_REGISTER_TEST(engine, "view", "flip_toggle");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        ensure_image_loaded(ctx);
        bool orig_h = *hdrview()->action("Flip horizontally").p_selected;
        bool orig_v = *hdrview()->action("Flip vertically").p_selected;

        const Box2i dw        = hdrview()->current_image()->display_window;
        auto        on_screen = [&dw] {
            return Box2f{hdrview()->vp_pos_at_pixel(float2{dw.min}), hdrview()->vp_pos_at_pixel(float2{dw.max})}
                .make_valid();
        };
        const Box2f before = on_screen();

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Flip horizontally");
        IM_CHECK_EQ(*hdrview()->action("Flip horizontally").p_selected, !orig_h);
        IM_CHECK(la::maxelem(la::abs(on_screen().min - before.min)) < 1e-2f);
        IM_CHECK(la::maxelem(la::abs(on_screen().max - before.max)) < 1e-2f);

        ctx->MenuClick("View/Flip vertically");
        IM_CHECK_EQ(*hdrview()->action("Flip vertically").p_selected, !orig_v);
        IM_CHECK(la::maxelem(la::abs(on_screen().min - before.min)) < 1e-2f);
        IM_CHECK(la::maxelem(la::abs(on_screen().max - before.max)) < 1e-2f);

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
