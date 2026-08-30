/** \file test_gui_info.cpp
    \author Wojciech Jarosz

    The Info window's "General" property table: that its one widget row ("Is transparency") lines up with the
    text rows around it, and that HDRViewApp::can_reload() -- which decides whether that row is even offered --
    agrees with what is actually readable.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

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
        hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
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
    ImGuiTest *t = IM_REGISTER_TEST(engine, "info", "transparency_row_is_as_tall_as_its_neighbors");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        load_fixture_and_show_info(ctx);

        // The fixture is an RGBA PNG carrying both an ICC profile and EXIF, so the General table holds the
        // "Is transparency" checkbox with text rows on either side of it -- what this test measures against.
        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);
        IM_CHECK(img->alpha_type != AlphaType_None);
        IM_CHECK(img->exif.valid());
        IM_CHECK(!img->icc_data.empty());

        // Every row is drawn into the scrolling child window PE::Begin("Image info") opens; the items in it
        // are the checkbox and one child window per text row (PE::WrappedText's "ResizableChild"). Only the
        // checkbox reports a label, so that is what tells the two apart.
        ctx->SetRef("");
        ImGuiTestItemList items;
        ctx->GatherItems(&items, "//Info", -1);

        struct Row
        {
            float y;
            bool  is_checkbox;
        };
        std::vector<Row> rows;
        for (const ImGuiTestItemInfo &item : items)
            if (item.Window && strstr(item.Window->Name, "Image info") != nullptr)
                rows.push_back({item.RectFull.Min.y, strcmp(item.DebugLabel, "##Alpha is transparency") == 0});

        std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.y < b.y; });

        int checkbox_row = -1;
        for (int i = 0; i < (int)rows.size(); ++i)
            if (rows[i].is_checkbox)
            {
                IM_CHECK_EQ(checkbox_row, -1); // exactly one
                checkbox_row = i;
            }
        IM_CHECK(checkbox_row >= 1);
        // "EXIF data" and "ICC data" follow it, both single-line text rows
        IM_CHECK(checkbox_row + 2 < (int)rows.size());

        // A row's height is the distance to the top of the next one. Nothing makes a widget row as tall as a
        // text row on its own -- the frame padding a checkbox carries and a line of text does not is enough
        // to set them apart -- so measure it rather than assume.
        const float checkbox_height = rows[checkbox_row + 1].y - rows[checkbox_row].y;
        const float text_height     = rows[checkbox_row + 2].y - rows[checkbox_row + 1].y;
        IM_CHECK(text_height > 0.f);
        IM_CHECK_LE(std::fabs(checkbox_height - text_height), 0.5f);
    };

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
