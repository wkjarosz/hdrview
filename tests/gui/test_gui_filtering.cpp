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

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif
#ifndef HDRVIEW_GUI_TEST_IMAGE_2
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a second, distinctly-named fixture image path"
#endif

static void load_both_fixtures(ImGuiTestContext *ctx)
{
    // clear any file filter a prior test left active
    ctx->SetRef("Images");
    ctx->ItemInputValue("##file filter", "");

    hdrview()->close_all_images();
    hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
    wait_for_loads(ctx);
    IM_CHECK_EQ(hdrview()->num_images(), 2);
    IM_CHECK_EQ(hdrview()->num_visible_images(), 2);
}

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
    ImGuiTest *t = IM_REGISTER_TEST(engine, "filtering", "file_filter_narrows_visible_list");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);
        IM_CHECK_EQ(hdrview()->num_visible_images(), 2);

        // "icon-256.png" is the only fixture filename containing "256".
        ctx->SetRef("Images");
        ctx->ItemInputValue("##file filter", "256");
        IM_CHECK_EQ(hdrview()->num_visible_images(), 1);

        ctx->ItemInputValue("##file filter", "");
        IM_CHECK_EQ(hdrview()->num_visible_images(), 2);
    };

    t           = IM_REGISTER_TEST(engine, "filtering", "current_and_reference_update_when_hidden_by_filter");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_both_fixtures(ctx);

        // "icon.png" is a substring of "icon-256.png" too, so look up the plain-icon index by exclusion.
        int icon256_idx = find_image_index_containing("icon-256.png");
        int icon_idx    = find_image_index_containing("icon.png");
        if (icon_idx == icon256_idx)
            for (int i = 0; i < hdrview()->num_images(); ++i)
                if (i != icon256_idx)
                    icon_idx = i;
        IM_CHECK(icon_idx >= 0 && icon256_idx >= 0 && icon_idx != icon256_idx);

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
