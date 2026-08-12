/** \file test_gui_dialogs.cpp
    \author Wojciech Jarosz

    Opens/closes a File-menu-driven dialog and asserts on its visibility. "Image loading options..." is used
    (rather than e.g. "About") because it's a plain-text MenuItem inside the "File" menu (see app-gui.cpp) and
    is always enabled, regardless of whether any image is loaded.
*/

#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

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
        IM_CHECK(ctx->WindowInfo("Image loading options...", ImGuiTestOpFlags_NoError).Window == nullptr);
    };
}
