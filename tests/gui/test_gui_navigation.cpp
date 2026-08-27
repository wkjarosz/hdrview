/** \file test_gui_navigation.cpp
    \author Wojciech Jarosz

    Navigating between loaded images via keyboard ("Go to next/previous image", Down/Up arrow - these have no
    menu representation, they're keyboard-only) and via mouse clicks on Images-window rows.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <algorithm>
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

    t           = IM_REGISTER_TEST(engine, "navigation", "shift_click_toggles_reference_group_off");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);

        // Channel-group rows only exist in the "flat list" file-list mode, and the mode persists in the
        // app settings, so drive the mode combo rather than relying on whatever this run started with.
        // Its entries are icon-prefixed, so address them by position in the popup: 1 = flat list.
        ctx->SetRef("");
        ctx->ItemClick("//Images/##channel list mode");
        ImGuiTestItemList modes;
        ctx->GatherItems(&modes, "//$FOCUSED", -1);
        IM_CHECK(modes.GetSize() > 1);
        ctx->ItemClick(modes[1]->ID);
        ctx->Yield();

        // A group row is a depth-3 item in the ImageList child window; so are the table's own header
        // cells, and every one of these is drawn with a leading icon glyph, so neither depth nor an
        // empty-label test separates them. Match the group name instead: both fixtures are RGBA PNGs,
        // so each contributes exactly one group row named "(R,G,B,A)".
        ImGuiTestItemList all_items;
        ctx->GatherItems(&all_items, "//Images", -1);

        std::vector<ImGuiID> group_ids;
        for (const ImGuiTestItemInfo &item : all_items)
            if (item.Depth == 3 && item.Window && strstr(item.Window->Name, "ImageList") != nullptr &&
                strstr(item.DebugLabel, "(R,G,B,A)") != nullptr)
                group_ids.push_back(item.ID);
        IM_CHECK_EQ((int)group_ids.size(), 2);

        // Shift-click adopts the clicked group as this image's reference group and makes it the reference.
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick(group_ids[0]);
        ctx->KeyUp(ImGuiMod_Shift);
        IM_CHECK_EQ(hdrview()->reference_image_index(), 0);

        ConstImagePtr img = hdrview()->image(0);
        IM_CHECK(img != nullptr);
        IM_CHECK_EQ(img->reference_group, 0);

        // Shift-clicking the same group row again clears the reference. The group index has to land on
        // -1, the "no reference channel group" state that active_group_index() falls back from and that
        // update_visibility() and the Ctrl+number group shortcut also assign; any valid index left behind
        // would instead pin that group as the reference the next time this image becomes one.
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick(group_ids[0]);
        ctx->KeyUp(ImGuiMod_Shift);
        IM_CHECK_EQ(hdrview()->reference_image_index(), -1);
        IM_CHECK_EQ(img->reference_group, -1);
    };
}
