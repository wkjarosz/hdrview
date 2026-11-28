/** \file test_gui_session_bundle.cpp
    \author Wojciech Jarosz

    Loading a session bundled inside a zip: a "*.hsess" manifest at the zip's root alongside the images it
    references, which is how session sharing works on the web build. Bundles are built in memory here,
    since HDRViewApp::export_session_bundle() opens a native save-file dialog.
*/

#include "app.h"
#include "common.h"
#include "image.h"
#include "json.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#include <filesystem>
#include <fstream>

namespace
{

std::string read_file(const fs::path &path)
{
    std::ifstream ifs(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

fs::path write_test_zip(const char *name, const std::vector<std::pair<std::string, std::string>> &entries)
{
    return write_file(fs::temp_directory_path() / name, zip_bytes(entries));
}

} // namespace

void RegisterTests_SessionBundle(ImGuiTestEngine *engine)
{
    /*
        A zip with a root .hsess manifest loads as that session, whether dropped (load_images(), which CLI args
        use too) or through "Load session..." (load_session()). The same in-bundle path twice, so the shared
        (path, channel_selector) matching has to tell entries extracted from a zip apart, as test_gui_session.cpp
        does for files on disk; and a blend mode only a session applies, which a fallback to loading the zip's
        images plainly would leave alone.
    */
    ImGuiTest *t = IM_REGISTER_TEST(engine, "session_bundle", "zip_with_manifest_loads_as_session");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        json j          = session_manifest({{{"path", "images/000_fixture.png"}, {"selected_group", 0}},
                                            {{"path", "images/000_fixture.png"}, {"reference_group", 1}}},
                                           1);
        j["blend_mode"] = "difference";
        const fs::path zip_path =
            write_test_zip("hdrview_test_bundle.zip", {{"session.hsess", j.dump(4)},
                                                       {"images/000_fixture.png", read_file(HDRVIEW_GUI_TEST_IMAGE)}});

        OnExit restore{[] { hdrview()->blend_mode() = BlendMode_Normal; }};
        for (bool explicitly : {false, true})
        {
            ctx->LogInfo("through %s", explicitly ? "load_session()" : "load_images()");
            on_main_thread(ctx, [] { hdrview()->close_all_images(); });
            hdrview()->blend_mode() = BlendMode_Normal;
            if (explicitly)
                hdrview()->load_session(zip_path.string());
            else
                hdrview()->load_images({zip_path.string()});
            wait_for_loads(ctx);

            IM_CHECK_EQ(hdrview()->num_images(), 2);
            IM_CHECK(hdrview()->current_image() != nullptr);
            IM_CHECK(hdrview()->reference_image() != nullptr);
            IM_CHECK(hdrview()->current_image() != hdrview()->reference_image());
            IM_CHECK_EQ(hdrview()->current_image()->selected_group, 0);
            IM_CHECK_EQ(hdrview()->reference_image()->reference_group, 1);
            IM_CHECK_EQ((int)hdrview()->blend_mode(), (int)BlendMode_Difference);
        }
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "zip_without_manifest_loads_only_as_plain_images");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path zip_path =
            write_test_zip("hdrview_test_plain.zip", {{"a.png", read_file(HDRVIEW_GUI_TEST_IMAGE)},
                                                      {"b.png", read_file(HDRVIEW_GUI_TEST_IMAGE_2)}});

        // "Load session..." on it loads nothing, rather than falling back to its images; drained first, so a
        // fallback would have landed by the time the list is read
        on_main_thread(ctx, [] { hdrview()->close_all_images(); });
        hdrview()->load_session(zip_path.string());
        wait_for_loads(ctx);
        ctx->Yield(2);
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        // while dropped, it loads as an ordinary multi-image zip
        hdrview()->load_images({zip_path.string()});
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 2);
    };
}
