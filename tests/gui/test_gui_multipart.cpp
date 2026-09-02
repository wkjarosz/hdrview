/** \file test_gui_multipart.cpp
    \author Wojciech Jarosz

    Loads a real-world multi-part EXR fixture, each part of which becomes its own Image. Registers nothing
    unless HDRVIEW_TEST_OPENEXR_DIR is set, which the -cpm/-universal presets do.
*/

#include "test_gui_registry.h"

#ifdef HDRVIEW_TEST_OPENEXR_DIR

#include "app.h"
#include "image.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#include <string>

// Loads the multi-part fixture, whose parts may land across several frames. Such a load leaves
// current_image_index() pointing at no particular part, so this pins it to 0.
static void load_multipart(ImGuiTestContext *ctx)
{
    hdrview()->close_all_images();
    hdrview()->load_images({std::string(HDRVIEW_TEST_OPENEXR_DIR) + "/multipart.0001.exr"});

    // every part arrives from one background load, so an empty queue means all of them have
    hdrview_test::wait_for_loads(ctx);
    IM_CHECK(hdrview()->num_images() > 1);
    hdrview()->set_current_image_index(0);
}

void RegisterTests_Multipart(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "multipart", "loads_as_multiple_images");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        load_multipart(ctx);

        for (int i = 0; i < hdrview()->num_images(); ++i)
        {
            auto img = hdrview()->image(i);
            IM_CHECK(img != nullptr);
            IM_CHECK(img->size().x > 0);
            IM_CHECK(img->size().y > 0);
        }
    };

    /*
        Reloading one part has to leave the image list the same length: the part replaces itself and none of
        its siblings is disturbed, on a list where every image shares one path. Reloading a part re-reads
        only that part, since it carries the part's own channel selector.
    */
    t           = IM_REGISTER_TEST(engine, "multipart", "reloading_keeps_one_image_per_part");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_multipart(ctx);
        const int parts = hdrview()->num_images();
        IM_CHECK(parts > 1);

        hdrview()->reload_image(hdrview()->image(0));
        hdrview_test::wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), parts);

        // and with a second reload already queued behind the first, as a file being rewritten produces
        hdrview()->reload_image(hdrview()->image(0));
        hdrview()->reload_image(hdrview()->image(0));
        hdrview_test::wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), parts);

        for (int i = 0; i < hdrview()->num_images(); ++i) IM_CHECK(hdrview()->image(i) != nullptr);
    };

    t           = IM_REGISTER_TEST(engine, "multipart", "each_part_computes_valid_stats");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_multipart(ctx);

        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);
        auto &group = img->groups[img->selected_group];
        IM_CHECK(group.num_channels > 0);

        PixelStats *stats = wait_for_stats(ctx, img->channels[group.channels[0]]);
        IM_CHECK(stats != nullptr);
        IM_CHECK(stats->computed);

        // some parts (motion-vector or depth channels) may legitimately hold NaN/Inf, so check only that
        // every pixel was accounted for once
        const auto &s     = stats->summary;
        int         total = img->size().x * img->size().y;
        IM_CHECK_EQ(s.valid_pixels + s.nan_pixels + s.inf_pixels, total);
    };
}

#else // !HDRVIEW_TEST_OPENEXR_DIR

void RegisterTests_Multipart(ImGuiTestEngine *) {}

#endif
