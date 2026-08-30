/** \file test_gui_lifetime.cpp
    \author Wojciech Jarosz

    Closes the reference image while a comparison against it is being computed. Channel::update_stats()
    hands the async task raw Channel pointers into the reference image but retains a shared_ptr only to
    the current one, so closing the reference frees channels the worker is still reading. Run under
    AddressSanitizer, where the read shows up as heap-use-after-free.
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

        // Compare image 0 against image 1: only a blend mode other than Normal makes calculate() sample
        // the reference, which is what puts the reference's channels behind the raw pointers.
        hdrview()->set_current_image_index(0);
        hdrview()->set_reference_image_index(1);
        hdrview()->blend_mode() = BlendMode_Difference;
        ctx->Yield();

        // Kick off the comparison, then close the reference before it can finish.
        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);
        auto &group = img->groups[img->selected_group];
        for (int c = 0; c < group.num_channels; ++c)
            img->channels[group.channels[c]].update_stats(c, hdrview()->current_image(), hdrview()->reference_image());

        hdrview()->close_image(1);

        // Deliberately a soak rather than a wait for some condition: there is no state to wait for, and
        // the point is to give the worker a window in which to touch the channels it no longer owns. What
        // catches that is the sanitizer job, so the window has to exist even though nothing observable
        // changes -- and it is a duration, since frames are no longer paced by anything.
        soak(ctx, std::chrono::milliseconds(250));

        IM_CHECK(hdrview()->num_images() == 1);
    };
}
