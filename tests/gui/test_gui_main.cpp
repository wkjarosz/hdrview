/** \file test_gui_main.cpp
    \author Wojciech Jarosz

    Entry point for hdrview_gui_tests: a real HDRView instance, with Dear ImGui Test Engine wired in via
    HDRViewApp::enable_gui_test_engine() (see src/app-windows.cpp).
*/

#include "app.h"
#include "test_gui_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>

#if defined(HDRVIEW_BUILD_TREE_ASSETS_DIR)
#include <hello_imgui/hello_imgui_assets.h>
#endif

int main(int, char **)
{
    // keep the console to Test Engine's pass/fail logging
    spdlog::set_level(spdlog::level::warn);

#if defined(HDRVIEW_BUILD_TREE_ASSETS_DIR)
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
#endif

    init_hdrview({}, {}, {}, {}, {});
    // the About dialog auto-opens when no files were passed on the command line, always the case here, and
    // would sit on top of the menu bar and toolbar
    *hdrview()->action("Show help").p_selected = false;
    hdrview()->enable_gui_test_engine(&RegisterAllGuiTests);
    hdrview()->run();

    auto [tested, succeeded] = hdrview()->test_engine_result();
    fprintf(stderr, "hdrview_gui_tests: %d/%d tests passed.\n", succeeded, tested);
    return (tested > 0 && succeeded == tested) ? 0 : 1;
}
