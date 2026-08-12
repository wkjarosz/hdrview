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

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

// A popup's ImGui::CloseCurrentPopup() request doesn't take effect until a couple of frames after the click
// that triggers it (empirically ~2 frames at ImGuiTestRunSpeed_Fast) - poll for the window to actually
// disappear rather than assuming one Yield() is enough.
static void wait_for_window_closed(ImGuiTestContext *ctx, const char *window_name)
{
    for (int frame = 0; frame < 30 && ctx->WindowInfo(window_name, ImGuiTestOpFlags_NoError).Window != nullptr; ++frame)
        ctx->Yield();
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
            for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
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
}
