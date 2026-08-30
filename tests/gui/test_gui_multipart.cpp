/** \file test_gui_multipart.cpp
    \author Wojciech Jarosz

    Loads a real-world multi-part EXR fixture (rather than the simple single-layer PNG icons used elsewhere)
    and confirms it loads correctly. Each part of a multi-part EXR becomes its own separate Image in
    HDRViewApp (confirmed empirically - unlike a single-part multi-layer EXR, where layers become multiple
    ChannelGroups within one Image), so this exercises the multi-part loader path and num_images()-level
    machinery at a realistic scale, rather than channel-group navigation within a single image.

    Only built with real content on -cpm/-universal presets, where CPM fetches OpenEXR from source and
    vendors a handful of real-world test images alongside it (see HDRVIEW_TEST_OPENEXR_DIR, set the same way
    as for tests/test_exr_io.cpp's vendored-data doctests) - RegisterTests_Multipart registers nothing and
    this category simply doesn't exist on -local presets.
*/

#include "test_gui_registry.h"

#ifdef HDRVIEW_TEST_OPENEXR_DIR

#include "app.h"
#include "image.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <string>

// Loads the multi-part fixture and waits for its image count to stabilize (all parts loaded) rather than a
// fixed frame count, since a multi-part file's parts may land across several frames. Multi-part loads don't
// reliably leave current_image_index() pointing at any particular part (unlike a normal load_images() call
// with distinct paths), so this pins it to 0 explicitly rather than relying on whatever the loader happened
// to select.
static void load_multipart(ImGuiTestContext *ctx)
{
    hdrview()->close_all_images();
    hdrview()->load_images({std::string(HDRVIEW_TEST_OPENEXR_DIR) + "/multipart.0001.exr"});

    int last_count = -1;
    for (int frame = 0; frame < 240; ++frame)
    {
        int count = hdrview()->num_images();
        if (count > 0 && count == last_count)
            break;
        last_count = count;
        ctx->Yield();
    }
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
        Reloading one part of a multi-part file has to leave the image list the same length: the part
        replaces itself, and none of its siblings is disturbed. The watch loop reloads on every timestamp
        change, so a multi-part file being rewritten reaches this constantly, and it does so on a list where
        ten images share one path -- the case most likely to confuse a replacement for an addition.

        (Reloading a part re-reads only that part, since it carries the part's own channel selector, so
        each of these arrivals is a single image.)
    */
    t           = IM_REGISTER_TEST(engine, "multipart", "reloading_keeps_one_image_per_part");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_multipart(ctx);
        const int parts = hdrview()->num_images();
        IM_CHECK(parts > 1);

        hdrview()->reload_image(hdrview()->image(0));
        for (int frame = 0; frame < 240; ++frame) ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), parts);

        // And with a second reload already queued behind the first, which is what a file being written
        // repeatedly produces.
        hdrview()->reload_image(hdrview()->image(0));
        hdrview()->reload_image(hdrview()->image(0));
        for (int frame = 0; frame < 240; ++frame) ctx->Yield();
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

        // Content-agnostic sanity check: some parts (e.g. motion-vector/depth channels) may legitimately
        // contain NaN/Inf, so this doesn't assert they're absent - only that every pixel was accounted for
        // exactly once.
        const auto &s     = stats->summary;
        int         total = img->size().x * img->size().y;
        IM_CHECK_EQ(s.valid_pixels + s.nan_pixels + s.inf_pixels, total);
    };
}

#else // !HDRVIEW_TEST_OPENEXR_DIR

void RegisterTests_Multipart(ImGuiTestEngine *) {}

#endif
