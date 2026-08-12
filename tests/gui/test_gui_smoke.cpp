/** \file test_gui_smoke.cpp
    \author Wojciech Jarosz

    Startup smoke tests: the app boots and its dockable windows (set up in HDRViewApp's constructor, see
    src/app.cpp) are actually present. These window names are stable, hardcoded literals, so they're safe
    SetRef()/WindowInfo() targets even as the surrounding GUI code evolves.
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
        // Dockable windows are top-level ImGui windows merely *displayed inside* the "MainDockSpace" host
        // window's dock node, not ID-children of it, so they're looked up at the (default, root) ref rather
        // than via SetRef("MainDockSpace").
        // "Log" starts closed (see log_window's initial visibility in app.cpp) so it's deliberately excluded
        // here; every other dockable window set up in the constructor starts open. Histogram, Channel
        // statistics, and Pixel inspector were merged into "Pixel statistics" (see #172).
        for (const char *label : {"Pixel statistics", "Images", "Info", "Colorspace", "Watched Folders"})
            check_window_exists(ctx, label);
    };
}
