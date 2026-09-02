/** \file test_gui_info.cpp
    \author Wojciech Jarosz

    The Info window's "General" property table: its one widget row lines up with the text rows around it,
    and HDRViewApp::can_reload() agrees with what is readable.
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

    // which panels start open depends on the saved layout; focus it too, or docked alongside others it is
    // an unselected tab that draws nothing
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

        // both reload actions are registered on every platform, and ask can_reload()
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
