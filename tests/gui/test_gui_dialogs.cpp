/** \file test_gui_dialogs.cpp
    \author Wojciech Jarosz

    Opens/closes a File-menu-driven dialog and asserts on its visibility. "Image loading options..." is used
    (rather than e.g. "About") because it's a plain-text MenuItem inside the "File" menu (see app-gui.cpp) and
    is always enabled, regardless of whether any image is loaded.
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

// A popup's ImGui::CloseCurrentPopup() request doesn't take effect until a couple of frames after the click
// that triggers it (empirically ~2 frames at ImGuiTestRunSpeed_Fast) - poll for the window to actually
// disappear rather than assuming one Yield() is enough.
static void wait_for_window_closed(ImGuiTestContext *ctx, const char *window_name)
{
    wait_until(ctx, [&] { return ctx->WindowInfo(window_name, ImGuiTestOpFlags_NoError).Window == nullptr; });
}

void RegisterTests_Dialogs(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "dialogs", "image_loading_options_open_close");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        // HelloImGui renders the menu bar via ImGui::BeginMainMenuBar(), a top-level construct whose fixed
        // internal window name is "##MainMenuBar" - not part of "MainDockSpace"'s ID stack.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Image loading options...");

        // The resulting popup is an independent top-level window, not a child of "##MainMenuBar" - reset the
        // ref to root before looking it up, otherwise WindowInfo() searches relative to the still-active ref.
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

    // Each format in the list draws its own options panel, and several of those read static state that
    // only drawing them fills in. Selecting one at a time is what runs every *_parameters_gui().
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

        // The format list is a BeginListBox(), whose child window carries a runtime ID-hash suffix
        // ("Save as.../<hex>"), so the entries cannot be addressed by a written-out path. Gather them
        // and click by id instead.
        ImGuiTestItemList items;
        ctx->GatherItems(&items, "//Save as...", -1);
        ImVector<ImGuiID> format_ids;
        for (auto &item : items)
            if (item.Depth == 1 && strstr(item.Window->Name, "Save as.../") != nullptr)
                format_ids.push_back(item.ID);
        // Eight entries are present whatever the build enables (BMP, HDR, JPEG and PNG via stb, OpenEXR,
        // PFM, QOI, TGA); the rest are gated on their HDRVIEW_ENABLE_* option, so this cannot expect all
        // fifteen.
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
