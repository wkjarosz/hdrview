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

#include "test_gui_support.h"

using namespace hdrview_test;

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
            wait_for_loads(ctx);
        }
        IM_CHECK(hdrview()->num_images() > 0);

        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);
        auto &group = img->groups[img->selected_group];
        IM_CHECK(group.num_channels > 0);

        PixelStats *stats = wait_for_stats(ctx, img->channels[group.channels[0]]);
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

    t           = IM_REGISTER_TEST(engine, "stats", "selection_off_the_image_computes_empty_stats");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // The app-level counterpart to test_pixel_stats.cpp's "A selection that misses the channel" case:
        // a selection is not clamped to the image anywhere, so one dragged into the empty area beside it
        // reaches PixelStats::calculate() as a region that intersects the data window into an inverted box.
        if (hdrview()->num_images() == 0)
        {
            hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
            wait_for_loads(ctx);
        }
        IM_CHECK(hdrview()->num_images() > 0);

        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);

        // Well past the data window in y, overlapping it in x -- the one-axis miss, whose volume() is
        // negative rather than spuriously positive. The gap has to exceed the statistics pass's block size
        // (1 << 20) once multiplied by the width, or the same overflow lands on a block count of zero and
        // nothing runs; one block's worth of rows clears that for any width.
        constexpr int gap = 1 << 20;
        const int2    lo = img->data_window.min, hi = img->data_window.max;
        hdrview()->roi() = hdrview()->roi_live() = Box2i{int2{lo.x, hi.y + gap}, int2{hi.x, hi.y + 2 * gap}};

        auto &group = img->groups[img->selected_group];
        IM_CHECK(group.num_channels > 0);

        PixelStats *stats = wait_for_stats(ctx, img->channels[group.channels[0]],
                                           [](const PixelStats *s) { return s->settings.roi == hdrview()->roi(); });
        IM_CHECK(stats != nullptr);
        IM_CHECK(stats->computed);
        IM_CHECK_EQ(stats->summary.valid_pixels, 0);

        // Put the selection back so later tests see the default state.
        hdrview()->roi() = hdrview()->roi_live() = Box2i{int2{0}};
        ctx->Yield(3);
    };
}
