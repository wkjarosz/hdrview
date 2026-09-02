/** \file test_gui_smoke.cpp
    \author Wojciech Jarosz

    Startup smoke tests: the app boots and its dockable windows are present.
*/

#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

static void check_window_exists(ImGuiTestContext *ctx, const char *label)
{
    IM_CHECK_SILENT(ctx->WindowInfo(label).Window != nullptr);
}

void RegisterTests_Smoke(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "smoke", "startup_dockable_windows_exist");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        // dockable windows are top-level ImGui windows displayed inside "MainDockSpace"'s dock node, not
        // ID-children of it, so they are looked up at the root ref. "Log" is left out: it starts closed.
        for (const char *label : {"Pixel statistics", "Images", "Info", "Colorspace", "Watched Folders"})
            check_window_exists(ctx, label);
    };
}
