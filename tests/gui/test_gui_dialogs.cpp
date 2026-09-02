/** \file test_gui_dialogs.cpp
    \author Wojciech Jarosz

    Opens and closes the File-menu dialogs and asserts on their visibility.
*/

#include "app.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#include <cstring>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

// ImGui::CloseCurrentPopup() takes a couple of frames to take effect (~2 at ImGuiTestRunSpeed_Fast), so
// poll for the window to disappear
static void wait_for_window_closed(ImGuiTestContext *ctx, const char *window_name)
{
    wait_until(ctx, [&] { return ctx->WindowInfo(window_name, ImGuiTestOpFlags_NoError).Window == nullptr; });
}

void RegisterTests_Dialogs(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "dialogs", "image_loading_options_open_close");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        // the menu bar is a top-level BeginMainMenuBar() window named "##MainMenuBar", not part of
        // "MainDockSpace"'s ID stack
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Image loading options...");

        // the popup is a top-level window, not a child of "##MainMenuBar": reset the ref to root, or
        // WindowInfo() searches relative to the still-active one
        ctx->SetRef("");
        IM_CHECK(ctx->WindowInfo("Image loading options...").Window != nullptr);

        ctx->SetRef("Image loading options...");
        ctx->ItemClick("OK");

        ctx->SetRef("");
        wait_for_window_closed(ctx, "Image loading options...");
        IM_CHECK(ctx->WindowInfo("Image loading options...", ImGuiTestOpFlags_NoError).Window == nullptr);
    };

    t           = IM_REGISTER_TEST(engine, "dialogs", "save_as_open_close");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (hdrview()->num_images() == 0)
        {
            hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
            wait_for_loads(ctx);
        }
        IM_CHECK(hdrview()->num_images() > 0);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Save as...");

        ctx->SetRef("");
        IM_CHECK(ctx->WindowInfo("Save as...").Window != nullptr);

        ctx->SetRef("Save as...");
        ctx->ItemClick("Cancel");

        ctx->SetRef("");
        wait_for_window_closed(ctx, "Save as...");
        IM_CHECK(ctx->WindowInfo("Save as...", ImGuiTestOpFlags_NoError).Window == nullptr);
    };

    // selecting each format in turn is what runs every *_parameters_gui()
    t           = IM_REGISTER_TEST(engine, "dialogs", "save_as_every_format_options_panel");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (hdrview()->num_images() == 0)
        {
            hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
            wait_for_loads(ctx);
        }
        IM_CHECK(hdrview()->num_images() > 0);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Save as...");
        ctx->SetRef("");
        IM_CHECK(ctx->WindowInfo("Save as...").Window != nullptr);

        // the format list is a BeginListBox(), whose child window carries a runtime ID-hash suffix
        // ("Save as.../<hex>"), so the entries have no written-out path: gather them and click by id
        ImGuiTestItemList items;
        ctx->GatherItems(&items, "//Save as...", -1);
        ImVector<ImGuiID> format_ids;
        for (auto &item : items)
            if (item.Depth == 1 && strstr(item.Window->Name, "Save as.../") != nullptr)
                format_ids.push_back(item.ID);
        // eight entries are present whatever the build enables (BMP, HDR, JPEG and PNG via stb, OpenEXR,
        // PFM, QOI, TGA); the rest are gated on their HDRVIEW_ENABLE_* option
        ctx->LogInfo("format list holds %d entries", format_ids.Size);
        IM_CHECK_GE(format_ids.Size, 8);

        for (ImGuiID id : format_ids)
        {
            ctx->ItemClick(id);
            ctx->Yield(3); // let the newly selected format's options panel draw
        }

        ctx->SetRef("Save as...");
        ctx->ItemClick("Cancel");
        ctx->SetRef("");
        wait_for_window_closed(ctx, "Save as...");
        IM_CHECK(ctx->WindowInfo("Save as...", ImGuiTestOpFlags_NoError).Window == nullptr);
    };
}
