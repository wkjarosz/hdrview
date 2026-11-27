/** \file test_gui_info.cpp
    \author Wojciech Jarosz

    The Info window: HDRViewApp::can_reload() and the reload actions agree with what is readable.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"
#include "test_gui_support.h"

#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

void RegisterTests_Info(ImGuiTestEngine *engine)
{
    ImGuiTest *t;

    t           = IM_REGISTER_TEST(engine, "info", "reload_actions_track_what_can_be_reloaded");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        IM_CHECK_RETV(hdrview_test::load_fixture(ctx), );

        IM_CHECK(hdrview()->can_reload(hdrview()->current_image()));
        IM_CHECK(!hdrview()->can_reload(nullptr));

        // both reload actions are registered on every platform, and ask can_reload()
        IM_CHECK(hdrview()->action("Reload image").enabled());
        IM_CHECK(hdrview()->action("Reload all images").enabled());

        hdrview()->close_all_images();
        ctx->Yield();
        IM_CHECK(!hdrview()->action("Reload image").enabled());
        IM_CHECK(!hdrview()->action("Reload all images").enabled());
    };
}
