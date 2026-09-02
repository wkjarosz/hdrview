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

#include "test_gui_support.h"

using namespace hdrview_test;

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

    // Closed outright rather than through close_all_images(), which prompts when an earlier test left an
    // edited image behind and would then leave both fixtures loaded on top of it.
    reset_images(ctx);
    hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
    wait_for_loads(ctx);
    IM_CHECK_EQ(hdrview()->num_images(), 2);
    IM_CHECK_EQ(hdrview()->num_visible_images(), 2);
}

//! The Images window's image rows, in list order.
/*!
    The rows are rendered inside a BeginTable("ImageList", ...) (app-windows.cpp), which ImGui hosts in its
    own child window named "Images/ImageList_<hex ID>" -- the hex suffix is a per-build ID hash, not
    something to hardcode. Each image row is an unlabeled TreeNodeEx at depth 2 within that window (depth 3
    is the row's own nested content, e.g. its channel-group rows), so gather broadly and filter.
*/
static std::vector<ImGuiID> gather_image_rows(ImGuiTestContext *ctx)
{
    // GatherItems()'s parent ref resolves relative to whatever SetRef() is still active (the same quirk
    // WindowInfo() has), so a bare "Images" here could resolve to "Images/Images". Use the absolute form.
    ImGuiTestItemList all_items;
    ctx->GatherItems(&all_items, "//Images", -1);

    std::vector<ImGuiID> row_ids;
    for (const ImGuiTestItemInfo &item : all_items)
        if (item.Depth == 2 && item.Window && strstr(item.Window->Name, "ImageList") != nullptr)
            row_ids.push_back(item.ID);
    return row_ids;
}

//! Whether image \p i has any channel group in the multi-selection.
static bool image_is_selected(int i)
{
    ConstImagePtr img = hdrview()->image(i);
    return img && img->is_selected();
}

//! Switches the Images window to its flat channel-group list, which is where group rows exist at all.
/*!
    The mode persists in the app settings, so drive the combo rather than relying on whatever this run
    started with. Its entries are icon-prefixed, so address them by position in the popup: 1 = flat list.
*/
static void use_flat_list_mode(ImGuiTestContext *ctx)
{
    ctx->SetRef("");
    ctx->ItemClick("//Images/##channel list mode");
    ImGuiTestItemList modes;
    ctx->GatherItems(&modes, "//$FOCUSED", -1);
    IM_CHECK_SILENT(modes.GetSize() > 1);
    ctx->ItemClick(modes[1]->ID);
    ctx->Yield(2);
}

//! The channel-group rows named "(R,G,B,A)", one per fixture.
/*!
    A group row is a depth-3 item in the ImageList child window; so are the table's own header cells, and
    every one of these is drawn with a leading icon glyph, so neither depth nor an empty-label test
    separates them. Match the group name instead: both fixtures are RGBA PNGs, so each contributes exactly
    one such row.
*/
static std::vector<ImGuiID> gather_rgba_group_rows(ImGuiTestContext *ctx)
{
    ImGuiTestItemList all_items;
    ctx->GatherItems(&all_items, "//Images", -1);

    std::vector<ImGuiID> ids;
    for (const ImGuiTestItemInfo &item : all_items)
        if (item.Depth == 3 && item.Window && strstr(item.Window->Name, "ImageList") != nullptr &&
            strstr(item.DebugLabel, "(R,G,B,A)") != nullptr)
            ids.push_back(item.ID);
    return ids;
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

        std::vector<ImGuiID> row_ids = gather_image_rows(ctx);
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

    // The whole click state machine in one walk: a plain click either collapses the selection or merely
    // moves current through it, ctrl/cmd toggles, and neither is allowed to leave nothing selected or to
    // leave current outside the selection.
    t           = IM_REGISTER_TEST(engine, "navigation", "ctrl_click_multi_selection");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);

        std::vector<ImGuiID> row_ids = gather_image_rows(ctx);
        IM_CHECK_EQ((int)row_ids.size(), 2);

        // A plain click on a row outside the selection collapses the selection onto it.
        ctx->ItemClick(row_ids[1]);
        ctx->ItemClick(row_ids[0]);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
        IM_CHECK(image_is_selected(0));
        IM_CHECK(!image_is_selected(1));

        // Ctrl/cmd adds to the selection without moving current.
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(row_ids[1]);
        ctx->KeyUp(ImGuiMod_Ctrl);
        IM_CHECK(image_is_selected(0));
        IM_CHECK(image_is_selected(1));
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // A plain click inside the selection keeps it, and only moves current.
        ctx->ItemClick(row_ids[1]);
        IM_CHECK_EQ(hdrview()->current_image_index(), 1);
        IM_CHECK(image_is_selected(0));
        IM_CHECK(image_is_selected(1));

        // Taking the current row out of the selection hands current to what is left of it.
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(row_ids[1]);
        ctx->KeyUp(ImGuiMod_Ctrl);
        IM_CHECK(!image_is_selected(1));
        IM_CHECK(image_is_selected(0));
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // And the last selected row refuses to leave: an empty selection would leave every edit with
        // nothing to act on and no way back except clicking something.
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(row_ids[0]);
        ctx->KeyUp(ImGuiMod_Ctrl);
        IM_CHECK(image_is_selected(0));
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "navigation", "ctrl_shift_click_selects_a_range");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        ctx->SetRef("Images");
        ctx->ItemInputValue("##file filter", "");
        hdrview()->close_all_images();

        // Three rows rather than two, so a range has a middle for a chord that only selected its ends to
        // fall through.
        hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2, HDRVIEW_GUI_TEST_IMAGE});
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_visible_images(), 3);

        std::vector<ImGuiID> row_ids = gather_image_rows(ctx);
        IM_CHECK_EQ((int)row_ids.size(), 3);

        ctx->ItemClick(row_ids[0]);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
        IM_CHECK(!image_is_selected(1));

        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick(row_ids[2]);
        ctx->KeyUp(ImGuiMod_Shift);
        ctx->KeyUp(ImGuiMod_Ctrl);

        for (int i = 0; i < 3; ++i) IM_CHECK(image_is_selected(i));
        IM_CHECK_EQ(hdrview()->current_image_index(), 2);

        // Shift alone still means the reference, which is why the range chord needs ctrl as well.
        IM_CHECK_EQ(hdrview()->reference_image_index(), -1);
    };

    t           = IM_REGISTER_TEST(engine, "navigation", "right_click_on_a_selected_group_covers_the_selection");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);
        use_flat_list_mode(ctx);

        std::vector<ImGuiID> group_ids = gather_rgba_group_rows(ctx);
        IM_CHECK_EQ((int)group_ids.size(), 2);

        ctx->ItemClick(group_ids[0]);
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(group_ids[1]);
        ctx->KeyUp(ImGuiMod_Ctrl);
        for (int i = 0; i < 2; ++i) IM_CHECK(image_is_selected(i));

        // Right-clicking a row that is in the selection covers the selection rather than that row alone,
        // the same way a plain click inside one keeps it.
        ctx->ItemClick(group_ids[1], ImGuiMouseButton_Right);
        ctx->ItemClick("//$FOCUSED/Ungroup channels");
        // The action is posted to the main thread rather than run from the popup, since it rebuilds the
        // very layer list the panel is walking.
        ctx->Yield(4);

        for (int i = 0; i < 2; ++i)
        {
            IM_CHECK_EQ((int)hdrview()->image(i)->groups.size(), 4);
            IM_CHECK_EQ((int)hdrview()->image(i)->history.size(), 1);
        }

        // Edited images left loaded would make the next test's close_all_images() prompt rather than
        // close.
        reset_images(ctx);
    };

    // Every image numbers its own groups, and the panel lists every image's rows, so a group index alone
    // cannot say which image a right-click meant.
    t           = IM_REGISTER_TEST(engine, "navigation", "right_click_names_the_image_whose_row_it_is");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);
        use_flat_list_mode(ctx);

        // Give the two images different group structures: after this, image 0 has four single-channel
        // groups where image 1 still has one of four channels, so the index 0 means different things.
        std::vector<ImGuiID> group_ids = gather_rgba_group_rows(ctx);
        IM_CHECK_EQ((int)group_ids.size(), 2);
        ctx->ItemClick(group_ids[0]);
        ctx->ItemClick(group_ids[0], ImGuiMouseButton_Right);
        ctx->ItemClick("//$FOCUSED/Ungroup channels");
        ctx->Yield(4);
        IM_CHECK_EQ((int)hdrview()->image(0)->groups.size(), 4);
        IM_CHECK_EQ((int)hdrview()->image(1)->groups.size(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // Now right-click the other image's row while image 0 is still the current one. Image 0's group 0
        // is a lone channel by now, so a command that took the index and applied it to the current image
        // would quietly do nothing and leave image 1 whole.
        group_ids = gather_rgba_group_rows(ctx);
        IM_CHECK_EQ((int)group_ids.size(), 1); // only image 1 still has an (R,G,B,A) row
        ctx->ItemClick(group_ids[0], ImGuiMouseButton_Right);
        ctx->ItemClick("//$FOCUSED/Ungroup channels");
        ctx->Yield(4);

        IM_CHECK_EQ((int)hdrview()->image(1)->groups.size(), 4);
        IM_CHECK_EQ((int)hdrview()->image(1)->history.size(), 1);

        // And the image being looked at was left alone, having neither moved nor gained an entry.
        IM_CHECK_EQ((int)hdrview()->image(0)->history.size(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        reset_images(ctx);
    };
}
