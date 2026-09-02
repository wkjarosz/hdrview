/** \file test_gui_navigation.cpp
    \author Wojciech Jarosz

    Navigating between loaded images by keyboard (the menu-less "Go to next/previous image") and by mouse
    clicks on Images-window rows.
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
    // clear any file filter a prior test left active, or neither fixture shows up below
    ctx->SetRef("Images");
    ctx->ItemInputValue("##file filter", "");

    // the unguarded close: close_all_images() prompts if an earlier test left an edited image behind
    reset_images(ctx);
    hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
    wait_for_loads(ctx);
    IM_CHECK_EQ(hdrview()->num_images(), 2);
    IM_CHECK_EQ(hdrview()->num_visible_images(), 2);
}

//! The Images window's image rows, in list order.
/*!
    The rows live in a BeginTable("ImageList", ...) that ImGui hosts in a child window named
    "Images/ImageList_<hex ID>", whose suffix is a runtime ID hash and cannot be hardcoded. Each image row
    is an unlabeled TreeNodeEx at depth 2 there (depth 3 is its nested content), so gather broadly and filter.
*/
static std::vector<ImGuiID> gather_image_rows(ImGuiTestContext *ctx)
{
    // GatherItems()'s parent ref resolves relative to whatever SetRef() is still active, so a bare "Images"
    // could resolve to "Images/Images": use the absolute form
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

//! Switches the Images window to its flat channel-group list, the only mode with group rows.
//! The mode persists in the app settings, so drive the combo. Its entries are icon-prefixed, so address
//! them by position in the popup: 1 = flat list.
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
//! Group rows and the table's header cells are both depth-3 items drawn with a leading icon glyph, so
//! neither depth nor an empty-label test separates them; match the group name instead.
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

        // the callbacks directly: these actions have no menu path, and their chords go through
        // process_shortcuts()'s !io.NavVisible gate, which Test Engine's forced gamepad-nav flags disturb
        hdrview()->action("Go to next image").callback();
        IM_CHECK_EQ(hdrview()->current_image_index(), 1);
        hdrview()->action("Go to previous image").callback();
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // one end-to-end check of the key binding: Test Engine forces ImGuiConfigFlags_NavEnableGamepad for
        // the process lifetime once any simulated input has run, leaving NavVisible stuck true, so clear it
        // right before the press to match the shipped configuration
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

        // channel-group rows only exist in the "flat list" file-list mode, and the mode persists in the app
        // settings, so drive the combo. Its entries are icon-prefixed: address them by position, 1 = flat list.
        ctx->SetRef("");
        ctx->ItemClick("//Images/##channel list mode");
        ImGuiTestItemList modes;
        ctx->GatherItems(&modes, "//$FOCUSED", -1);
        IM_CHECK(modes.GetSize() > 1);
        ctx->ItemClick(modes[1]->ID);
        ctx->Yield();

        // group rows and the table's header cells are both depth-3 items drawn with a leading icon glyph,
        // so match the group name instead; each RGBA fixture contributes one row named "(R,G,B,A)"
        ImGuiTestItemList all_items;
        ctx->GatherItems(&all_items, "//Images", -1);

        std::vector<ImGuiID> group_ids;
        for (const ImGuiTestItemInfo &item : all_items)
            if (item.Depth == 3 && item.Window && strstr(item.Window->Name, "ImageList") != nullptr &&
                strstr(item.DebugLabel, "(R,G,B,A)") != nullptr)
                group_ids.push_back(item.ID);
        IM_CHECK_EQ((int)group_ids.size(), 2);

        // shift-click adopts the clicked group as this image's reference group and makes it the reference
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick(group_ids[0]);
        ctx->KeyUp(ImGuiMod_Shift);
        IM_CHECK_EQ(hdrview()->reference_image_index(), 0);

        ConstImagePtr img = hdrview()->image(0);
        IM_CHECK(img != nullptr);
        IM_CHECK_EQ(img->reference_group, 0);

        // shift-clicking it again clears the reference; the group index has to land on -1, the "no reference
        // channel group" state, or that group is pinned the next time this image becomes the reference
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick(group_ids[0]);
        ctx->KeyUp(ImGuiMod_Shift);
        IM_CHECK_EQ(hdrview()->reference_image_index(), -1);
        IM_CHECK_EQ(img->reference_group, -1);
    };

    // the whole click state machine in one walk: a plain click collapses the selection or moves current
    // through it, ctrl/cmd toggles, and neither may empty the selection or put current outside it
    t           = IM_REGISTER_TEST(engine, "navigation", "ctrl_click_multi_selection");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);

        std::vector<ImGuiID> row_ids = gather_image_rows(ctx);
        IM_CHECK_EQ((int)row_ids.size(), 2);

        // a plain click on a row outside the selection collapses the selection onto it
        ctx->ItemClick(row_ids[1]);
        ctx->ItemClick(row_ids[0]);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
        IM_CHECK(image_is_selected(0));
        IM_CHECK(!image_is_selected(1));

        // ctrl/cmd adds to the selection without moving current
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(row_ids[1]);
        ctx->KeyUp(ImGuiMod_Ctrl);
        IM_CHECK(image_is_selected(0));
        IM_CHECK(image_is_selected(1));
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // a plain click inside the selection keeps it, and only moves current
        ctx->ItemClick(row_ids[1]);
        IM_CHECK_EQ(hdrview()->current_image_index(), 1);
        IM_CHECK(image_is_selected(0));
        IM_CHECK(image_is_selected(1));

        // taking the current row out of the selection hands current to what is left of it
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(row_ids[1]);
        ctx->KeyUp(ImGuiMod_Ctrl);
        IM_CHECK(!image_is_selected(1));
        IM_CHECK(image_is_selected(0));
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // the last selected row refuses to leave: an empty selection would leave every edit nothing to act on
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

        // three rows, so the range has a middle that a chord selecting only its ends would miss
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

        // shift alone means the reference, so the range chord needs ctrl as well
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

        // right-clicking a row inside the selection covers the whole selection
        ctx->ItemClick(group_ids[1], ImGuiMouseButton_Right);
        ctx->ItemClick("//$FOCUSED/Ungroup channels");
        // the action is posted to the main thread, since it rebuilds the layer list the panel is walking
        ctx->Yield(4);

        for (int i = 0; i < 2; ++i)
        {
            IM_CHECK_EQ((int)hdrview()->image(i)->groups.size(), 4);
            IM_CHECK_EQ((int)hdrview()->image(i)->history.size(), 1);
        }

        // edited images left loaded would make the next test's close_all_images() prompt
        reset_images(ctx);
    };

    // every image numbers its own groups, so a group index alone cannot say which image a right-click meant
    t           = IM_REGISTER_TEST(engine, "navigation", "right_click_names_the_image_whose_row_it_is");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);
        use_flat_list_mode(ctx);

        // give the two images different group structures, so that index 0 means different things in each
        std::vector<ImGuiID> group_ids = gather_rgba_group_rows(ctx);
        IM_CHECK_EQ((int)group_ids.size(), 2);
        ctx->ItemClick(group_ids[0]);
        ctx->ItemClick(group_ids[0], ImGuiMouseButton_Right);
        ctx->ItemClick("//$FOCUSED/Ungroup channels");
        ctx->Yield(4);
        IM_CHECK_EQ((int)hdrview()->image(0)->groups.size(), 4);
        IM_CHECK_EQ((int)hdrview()->image(1)->groups.size(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // right-click the other image's row while image 0 is still current: image 0's group 0 is a lone
        // channel by now, so applying the index to the current image would leave image 1 whole
        group_ids = gather_rgba_group_rows(ctx);
        IM_CHECK_EQ((int)group_ids.size(), 1); // only image 1 still has an (R,G,B,A) row
        ctx->ItemClick(group_ids[0], ImGuiMouseButton_Right);
        ctx->ItemClick("//$FOCUSED/Ungroup channels");
        ctx->Yield(4);

        IM_CHECK_EQ((int)hdrview()->image(1)->groups.size(), 4);
        IM_CHECK_EQ((int)hdrview()->image(1)->history.size(), 1);

        // the image being looked at was left alone, having neither moved nor gained an entry
        IM_CHECK_EQ((int)hdrview()->image(0)->history.size(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        reset_images(ctx);
    };
}
