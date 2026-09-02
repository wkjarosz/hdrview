/** \file test_gui_lifetime.cpp
    \author Wojciech Jarosz

    Closes the reference image while a comparison against it is being computed: Channel::update_stats()
    hands the async task raw Channel pointers into the reference but retains a shared_ptr only to the
    current image. Run under AddressSanitizer, where a stale read shows up as heap-use-after-free.
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
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a second fixture image path"
#endif

void RegisterTests_Lifetime(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "lifetime", "close_reference_during_stats");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        reset_images(ctx);

        IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2}), 2);

        // only a blend mode other than Normal makes calculate() sample the reference, putting its channels
        // behind the raw pointers
        hdrview()->set_current_image_index(0);
        hdrview()->set_reference_image_index(1);
        hdrview()->blend_mode() = BlendMode_Difference;
        ctx->Yield();

        // kick off the comparison, then close the reference before it can finish
        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);
        auto &group = img->groups[img->selected_group];
        for (int c = 0; c < group.num_channels; ++c)
            img->channels[group.channels[c]].update_stats(c, hdrview()->current_image(), hdrview()->reference_image());

        hdrview()->close_image(1);

        // a soak, not a wait: there is no state to wait for, only a window in which the worker may touch
        // channels it no longer owns, and the sanitizer job is what catches that
        soak(ctx, std::chrono::milliseconds(250));

        IM_CHECK(hdrview()->num_images() == 1);
    };
}
