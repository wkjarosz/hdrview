/** \file test_gui_filtering.cpp
    \author Wojciech Jarosz

    File-list filtering (the "##file filter" field in the "Images" window) and its interaction with the
    current/reference image selection.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

// background loads of a batch land in m_images in any order, so look up which index holds which fixture
static int find_image_index_containing(const char *substr)
{
    for (int i = 0; i < hdrview()->num_images(); ++i)
        if (hdrview()->image(i)->filename.find(substr) != std::string::npos)
            return i;
    return -1;
}

void RegisterTests_Filtering(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "filtering", "current_and_reference_update_when_hidden_by_filter");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        reset_images(ctx);
        IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2}), 2);

        // "icon-256.png" is the only fixture filename containing "256", and the other is "icon.png"
        const int icon256_idx = find_image_index_containing("icon-256.png");
        IM_CHECK(icon256_idx >= 0);
        const int icon_idx = 1 - icon256_idx;

        hdrview()->set_current_image_index(icon_idx);
        hdrview()->set_reference_image_index(icon256_idx);

        // filtering to "256" hides the current image, and update_visibility() advances current to the only
        // visible one left; the reference stays put since it is still visible
        ctx->SetRef("Images");
        ctx->ItemInputValue("##file filter", "256");
        IM_CHECK_EQ(hdrview()->num_visible_images(), 1);
        IM_CHECK_EQ(hdrview()->num_images(), 2);

        IM_CHECK_EQ(hdrview()->current_image_index(), icon256_idx);
        IM_CHECK_EQ(hdrview()->reference_image_index(), icon256_idx);

        // matching neither fixture hides both: current has nowhere visible to advance to (-1), and the
        // reference is cleared
        ctx->ItemInputValue("##file filter", "no_such_file");
        IM_CHECK_EQ(hdrview()->num_visible_images(), 0);
        IM_CHECK_EQ(hdrview()->current_image_index(), -1);
        IM_CHECK_EQ(hdrview()->reference_image_index(), -1);

        ctx->ItemInputValue("##file filter", "");
        IM_CHECK_EQ(hdrview()->num_visible_images(), 2);
    };
}
