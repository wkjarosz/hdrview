/** \file test_gui_main.cpp
    \author Wojciech Jarosz

    Entry point for hdrview_gui_tests: a real HDRView instance, built with Dear ImGui Test Engine wired in via
    HDRViewApp::enable_gui_test_engine() (see src/app-windows.cpp). Unlike hdrview_tests (doctest), this binary
    owns the full HelloImGui frame loop for its lifetime, so it gets its own main() rather than sharing
    doctest's.
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
    // Keep console output focused on Test Engine's own pass/fail logging rather than the app's usual info-level
    // startup chatter.
    spdlog::set_level(spdlog::level::warn);

#if defined(HDRVIEW_BUILD_TREE_ASSETS_DIR)
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
#endif

    init_hdrview({}, {}, {}, {}, {});
    // The About dialog auto-opens on startup whenever no files were passed on the command line (see its
    // construction in app.cpp), which is always true for this harness; suppress that once, up front, so it
    // doesn't sit on top of the menu bar/toolbar and confound every other test.
    *hdrview()->action("Show help").p_selected = false;
    hdrview()->enable_gui_test_engine(&RegisterAllGuiTests);
    hdrview()->run();

    auto [tested, succeeded] = hdrview()->test_engine_result();
    fprintf(stderr, "hdrview_gui_tests: %d/%d tests passed.\n", succeeded, tested);
    return (tested > 0 && succeeded == tested) ? 0 : 1;
}
