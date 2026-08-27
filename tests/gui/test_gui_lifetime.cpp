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
        while (hdrview()->num_images() > 0) hdrview()->close_image(0);
        ctx->Yield();

        hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
        for (int frame = 0; frame < 240 && hdrview()->num_images() < 2; ++frame) ctx->Yield();
        IM_CHECK(hdrview()->num_images() == 2);

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
            img->channels[group.channels[c]].update_stats(c, hdrview()->current_image(),
                                                          hdrview()->reference_image());

        hdrview()->close_image(1);

        // Let the worker run against the freed channels.
        for (int frame = 0; frame < 120; ++frame) ctx->Yield();

        IM_CHECK(hdrview()->num_images() == 1);
    };
}
