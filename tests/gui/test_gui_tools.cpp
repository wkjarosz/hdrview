/** \file test_gui_tools.cpp
    \author Wojciech Jarosz

    Tools-menu mouse-mode actions: confirms exactly one of Pan/Rectangular-select/Pixel-inspector is active
    at a time.
*/

#include "app.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cstring>

static bool is_selected(const char *name) { return *hdrview()->action(name).p_selected; }

static void assert_only(const char *active_name)
{
    for (const char *name : {"Pan and zoom", "Rectangular select", "Pixel/color inspector"})
        IM_CHECK_EQ(is_selected(name), strcmp(name, active_name) == 0);
}

void RegisterTests_Tools(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "tools", "mouse_mode_mutually_exclusive");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        ctx->SetRef("##MainMenuBar");
        assert_only("Pan and zoom"); // default at startup

        ctx->MenuClick("Tools/Rectangular select");
        assert_only("Rectangular select");

        // The action's own name contains a literal '/' ("Pixel/color inspector"), which MenuClick would
        // otherwise misparse as a submenu path separator - escape it, mirroring how Dear ImGui's own demo
        // addresses its "Metrics/Debugger" menu item ("Tools/Metrics\\/Debugger").
        ctx->MenuClick("Tools/Pixel\\/color inspector");
        assert_only("Pixel/color inspector");

        ctx->MenuClick("Tools/Pan and zoom"); // restore the process-lifetime static default
        assert_only("Pan and zoom");
    };
}
