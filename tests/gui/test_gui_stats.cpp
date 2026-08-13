/** \file test_gui_stats.cpp
    \author Wojciech Jarosz

    Loads a fixture image and asserts on the computed PixelStats for its current channel group, read
    directly off the data model (Channel::get_stats()) rather than parsed from the "Pixel statistics"
    window's rendered text, which has no per-cell widget ID to read back through Test Engine.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

void RegisterTests_Stats(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "stats", "computed_and_bounded");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        if (hdrview()->num_images() == 0)
        {
            hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
            for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
        }
        IM_CHECK(hdrview()->num_images() > 0);

        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);
        auto &group = img->groups[img->selected_group];
        IM_CHECK(group.num_channels > 0);

        PixelStats *stats = nullptr;
        for (int frame = 0; frame < 120; ++frame)
        {
            stats = img->channels[group.channels[0]].get_stats();
            if (stats->computed)
                break;
            ctx->Yield();
        }
        IM_CHECK(stats != nullptr);
        IM_CHECK(stats->computed);

        const auto &s = stats->summary;
        IM_CHECK_EQ(s.nan_pixels, 0);
        IM_CHECK_EQ(s.inf_pixels, 0);
        IM_CHECK(s.valid_pixels > 0);
        IM_CHECK(s.valid_pixels <= img->size().x * img->size().y);
        IM_CHECK(s.minimum <= s.average);
        IM_CHECK(s.average <= s.maximum);
        IM_CHECK(s.stddev >= 0.0);
    };
}
