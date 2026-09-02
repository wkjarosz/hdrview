/** \file test_gui_info.cpp
    \author Wojciech Jarosz

    The Info window's "General" property table: that its one widget row ("Is transparency") lines up with the
    text rows around it, and that HDRViewApp::can_reload() -- which decides whether that row is even offered --
    agrees with what is actually readable.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"
#include "test_gui_support.h"

#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

static void load_fixture_and_show_info(ImGuiTestContext *ctx)
{
    if (hdrview()->num_images() == 0)
    {
        hdrview_test::load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE});
    }
    IM_CHECK(hdrview()->num_images() > 0);

    // Which panels start open depends on the saved layout, so ask for this one rather than assuming it, and
    // focus it: docked alongside other panels it would otherwise be an unselected tab, drawing nothing.
    *hdrview()->action("Show Info window").p_selected = true;
    ctx->Yield(2);
    ctx->WindowFocus("//Info");
    ctx->Yield(2);
    IM_CHECK(ctx->WindowInfo("//Info").Window != nullptr);
}

void RegisterTests_Info(ImGuiTestEngine *engine)
{
    ImGuiTest *t;

    t           = IM_REGISTER_TEST(engine, "info", "reload_actions_track_what_can_be_reloaded");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_fixture_and_show_info(ctx);

        IM_CHECK(hdrview()->can_reload(hdrview()->current_image()));
        IM_CHECK(!hdrview()->can_reload(nullptr));

        // Both reload actions are registered on every platform, and ask can_reload() rather than merely
        // whether an image is loaded.
        IM_CHECK(hdrview()->action("Reload image").enabled());
        IM_CHECK(hdrview()->action("Reload all images").enabled());

        hdrview()->close_all_images();
        ctx->Yield();
        IM_CHECK(!hdrview()->action("Reload image").enabled());
        IM_CHECK(!hdrview()->action("Reload all images").enabled());

        // leave an image loaded, as the other tests in this run expect
        load_fixture_and_show_info(ctx);
    };
}
