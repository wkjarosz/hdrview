/** \file test_gui_image_io.cpp
    \author Wojciech Jarosz

    Loads a small fixture image and drives the exposure slider, exercising the path from image loading through
    to viewport/toolbar state. Loads directly via HDRViewApp::load_images() rather than through the File > Open
    menu, since that dialog is a native OS file picker Test Engine can't drive.
*/

#include "app.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <filesystem>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif
#ifndef HDRVIEW_GUI_TEST_IMAGE_2
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a second, distinctly-named fixture image path"
#endif

void RegisterTests_ImageIO(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "image_io", "load_and_adjust_exposure");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});

        // Loading happens on a background thread and is drained into m_images from ShowGui() one frame at a
        // time, so give it a bounded number of frames to land rather than assuming it's ready immediately.
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
        IM_CHECK(hdrview()->num_images() > 0);

        const float target_exposure = 2.5f;
        // The exposure slider lives in the top edge toolbar, a separate floating window from
        // "MainDockSpace" with a fixed internal name baked into Hello ImGui itself
        // (see docking_details.cpp: "##" + EdgeToolbarTypeName(Top) + "_2123243"). Addressing it directly
        // (rather than via a "**/" wildcard search across all windows) lets Test Engine bring it to front
        // before hovering/interacting with the item.
        ctx->SetRef("##Top_2123243");
        ctx->ItemInputValue("##ExposureSlider", target_exposure);
        IM_CHECK_EQ(hdrview()->exposure(), target_exposure);
    };

    t           = IM_REGISTER_TEST(engine, "image_io", "multi_image_load_switch_close");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // Don't assume a pristine start: an earlier test in this binary may have already loaded an image.
        hdrview()->close_all_images();
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
        for (int frame = 0; frame < 120 && hdrview()->num_images() < 2; ++frame) ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), 2);

        hdrview()->set_current_image_index(1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 1);
        IM_CHECK(hdrview()->current_image() == hdrview()->image(1));

        hdrview()->set_reference_image_index(0);
        IM_CHECK_EQ(hdrview()->reference_image_index(), 0);
        IM_CHECK(hdrview()->reference_image() == hdrview()->image(0));

        hdrview()->close_image(0);
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        hdrview()->close_all_images();
        IM_CHECK_EQ(hdrview()->num_images(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "image_io", "load_from_long_path");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        namespace fs = std::filesystem;

        hdrview()->close_all_images();
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        // Build a path over 260 characters, the classic Win32 MAX_PATH limit that HDRView's application
        // manifest (resources/windows/HDRView.manifest) opts out of. Requires the system-wide "Enable Win32
        // long paths" policy to actually succeed, which is outside HDRView's control -- tolerate failure here
        // rather than treat it as a test failure.
        fs::path          dir = fs::temp_directory_path() / "hdrview_gui_long_path_test";
        const std::string segment(50, 'a');
        while (dir.u8string().size() < 300) dir /= segment;

        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
        {
            ctx->LogWarning("Skipping: could not create a >260 character path (%s). This requires the "
                             "system-wide \"Enable Win32 long paths\" policy.",
                             ec.message().c_str());
            return;
        }

        fs::path long_path = dir / "fixture.png";
        fs::copy_file(HDRVIEW_GUI_TEST_IMAGE, long_path, fs::copy_options::overwrite_existing, ec);
        IM_CHECK(!ec);

        hdrview()->load_images({long_path.u8string()});
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
        IM_CHECK(hdrview()->num_images() > 0);

        hdrview()->close_all_images();
        fs::remove_all(fs::temp_directory_path() / "hdrview_gui_long_path_test", ec);
    };
}
