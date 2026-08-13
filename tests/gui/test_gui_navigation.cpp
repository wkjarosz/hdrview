/** \file test_gui_navigation.cpp
    \author Wojciech Jarosz

    Navigating between loaded images via keyboard ("Go to next/previous image", Down/Up arrow - these have no
    menu representation, they're keyboard-only) and via mouse clicks on Images-window rows.
*/

#include "app.h"
#include "test_gui_registry.h"

#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cstring>
#include <vector>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif
#ifndef HDRVIEW_GUI_TEST_IMAGE_2
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a second, distinctly-named fixture image path"
#endif

static void load_both_fixtures(ImGuiTestContext *ctx)
{
    // Defensive: clear any file filter a prior test in this run may have left active (e.g. an earlier
    // assertion failure that returned before its own cleanup ran) - otherwise num_visible_images()/the
    // Images-window row list here wouldn't reflect both freshly-loaded fixtures.
    ctx->SetRef("Images");
    ctx->ItemInputValue("##file filter", "");

    hdrview()->close_all_images();
    hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
    for (int frame = 0; frame < 120 && hdrview()->num_images() < 2; ++frame) ctx->Yield();
    IM_CHECK_EQ(hdrview()->num_images(), 2);
    IM_CHECK_EQ(hdrview()->num_visible_images(), 2);
}

void RegisterTests_Navigation(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "navigation", "keyboard_next_previous");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);
        hdrview()->set_current_image_index(0);

        // Primary assertion via direct callback invocation (white-box): "Go to next/previous image" have no
        // menu path to MenuClick, and the real key-chord dispatch is gated behind process_shortcuts()'s
        // !io.NavVisible check, which the vendored Test Engine's forced gamepad-nav backend flags make less
        // predictable to rely on as the sole check.
        hdrview()->action("Go to next image").callback();
        IM_CHECK_EQ(hdrview()->current_image_index(), 1);
        hdrview()->action("Go to previous image").callback();
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // One black-box sanity check that the real key binding still fires end to end. process_shortcuts()
        // gates every chord on !io.NavVisible, and the vendored Test Engine unconditionally forces
        // ImGuiConfigFlags_NavEnableGamepad for the process lifetime once any simulated input has run, which
        // can otherwise leave NavVisible stuck true - clear it right before the key press so this check
        // reflects the shipped app's actual (non-gamepad-nav) configuration.
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        ctx->Yield();
        ctx->KeyPress(ImGuiKey_DownArrow);
        IM_CHECK_EQ(hdrview()->current_image_index(), 1);
    };

    t           = IM_REGISTER_TEST(engine, "navigation", "mouse_click_select");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);

        // load_both_fixtures() leaves SetRef("Images") active; GatherItems()'s parent ref resolves
        // relative to that (the same quirk WindowInfo() has - see test_gui_dialogs.cpp), so a bare
        // "Images" here would actually resolve to "Images/Images". Use the explicit absolute form instead.
        //
        // The row list is rendered inside a BeginTable("ImageList", ...) (app-windows.cpp), which ImGui
        // hosts in its own child window named "Images/ImageList_<hex ID>" - the hex suffix is a per-build
        // ID hash, not something to hardcode. Each row is an unlabeled TreeNodeEx at depth 2 within that
        // window (depth 3 is the row's own nested content, e.g. its channel-group selector) - filter the
        // full gathered list down to exactly those rather than guessing a fixed path.
        ImGuiTestItemList all_items;
        ctx->GatherItems(&all_items, "//Images", -1);

        std::vector<ImGuiID> row_ids;
        for (const ImGuiTestItemInfo &item : all_items)
            if (item.Depth == 2 && item.Window && strstr(item.Window->Name, "ImageList") != nullptr)
                row_ids.push_back(item.ID);
        IM_CHECK_EQ((int)row_ids.size(), 2);

        ctx->ItemClick(row_ids[1]);
        IM_CHECK_EQ(hdrview()->current_image_index(), 1);

        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick(row_ids[0]);
        ctx->KeyUp(ImGuiMod_Shift);
        IM_CHECK_EQ(hdrview()->reference_image_index(), 0);
    };
}
