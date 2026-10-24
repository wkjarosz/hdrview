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
#include "version.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#include <cstring>
#include <filesystem>
#include <fstream>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif
#ifndef HDRVIEW_GUI_TEST_IMAGE_2
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a second, distinctly-named fixture image path"
#endif

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

std::string current_version_string()
{
    return fmt::format("{}.{}.{}", version_major(), version_minor(), version_patch());
}

} // namespace

void RegisterTests_SessionBundle(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "session_bundle", "zip_with_manifest_loads_as_session");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        json entry;
        entry["path"] = "images/000_fixture.png";
        json j;
        j["type"]      = "HDRView session";
        j["version"]   = current_version_string();
        j["images"]    = json::array({entry});
        j["current"]   = 0;
        j["reference"] = -1;
        // a non-default value only session loading applies: the plain-multi-image-zip fallback never
        // touches blend_mode, and would extract this same entry as an ordinary image and look identical
        j["blend_mode"] = "difference";
        j["view"]       = json::object();

        hdrview()->blend_mode() = BlendMode_Normal;

        fs::path zip_path =
            write_test_zip("hdrview_test_bundle.zip", {{"session.hsess", j.dump(4)},
                                                       {"images/000_fixture.png", read_file(HDRVIEW_GUI_TEST_IMAGE)}});

        hdrview()->close_all_images();
        // the same entry point drag-and-drop and CLI args use, and the blend mode the session sets put back after
        OnExit restore{[] { hdrview()->blend_mode() = BlendMode_Normal; }};
        hdrview()->load_images({zip_path.string()});
        wait_for_loads(ctx);

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
        IM_CHECK_EQ((int)hdrview()->blend_mode(), (int)BlendMode_Difference);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "zip_without_manifest_loads_as_plain_images");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // a zip with no root .hsess entry still loads as an ordinary multi-image zip
        fs::path zip_path = write_test_zip("hdrview_test_plain.zip", {{"a.png", read_file(HDRVIEW_GUI_TEST_IMAGE)},
                                                                      {"b.png", read_file(HDRVIEW_GUI_TEST_IMAGE_2)}});

        hdrview()->close_all_images();
        hdrview()->load_images({zip_path.string()});
        wait_for_loads(ctx);

        IM_CHECK_EQ(hdrview()->num_images(), 2);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "duplicate_image_in_bundle_stays_distinct");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // test_gui_session.cpp's duplicate-image case for a zip-bundled session: the same in-bundle path
        // twice, so the shared (path, channel_selector) matching disambiguates entries extracted from a zip
        json entry0, entry1;
        entry0["path"]            = "images/000_fixture.png";
        entry0["selected_group"]  = 0;
        entry1["path"]            = "images/000_fixture.png";
        entry1["reference_group"] = 1;

        json j;
        j["type"]       = "HDRView session";
        j["version"]    = current_version_string();
        j["images"]     = json::array({entry0, entry1});
        j["current"]    = 0;
        j["reference"]  = 1;
        j["blend_mode"] = "normal";
        j["view"]       = json::object();

        fs::path zip_path = write_test_zip(
            "hdrview_test_bundle_duplicate.zip",
            {{"session.hsess", j.dump(4)}, {"images/000_fixture.png", read_file(HDRVIEW_GUI_TEST_IMAGE)}});

        hdrview()->close_all_images();
        hdrview()->load_images({zip_path.string()});
        wait_for_loads(ctx);

        IM_CHECK_EQ(hdrview()->num_images(), 2);
        IM_CHECK(hdrview()->current_image_index() != hdrview()->reference_image_index());
        IM_CHECK(hdrview()->current_image() != nullptr);
        IM_CHECK(hdrview()->reference_image() != nullptr);
        IM_CHECK(hdrview()->current_image() != hdrview()->reference_image());
        IM_CHECK_EQ(hdrview()->current_image()->selected_group, 0);
        IM_CHECK_EQ(hdrview()->reference_image()->reference_group, 1);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "explicit_load_session_accepts_zip_bundle");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // load_session(const string&), behind the "Load session..." menu item, accepts a .zip bundle
        // directly, not only a plain .hsess
        json entry;
        entry["path"] = "images/000_fixture.png";
        json j;
        j["type"]       = "HDRView session";
        j["version"]    = current_version_string();
        j["images"]     = json::array({entry});
        j["current"]    = 0;
        j["reference"]  = -1;
        j["blend_mode"] = "normal";
        j["view"]       = json::object();

        fs::path zip_path = write_test_zip(
            "hdrview_test_explicit_load.zip",
            {{"session.hsess", j.dump(4)}, {"images/000_fixture.png", read_file(HDRVIEW_GUI_TEST_IMAGE)}});

        hdrview()->close_all_images();
        hdrview()->load_session(zip_path.string());
        wait_for_loads(ctx);

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "explicit_load_session_rejects_non_bundle_zip");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // an explicit "Load session..." on a zip with no manifest must not fall back to loading it as plain
        // images, which is what drag-and-drop and "Open image..." do
        fs::path zip_path =
            write_test_zip("hdrview_test_explicit_load_rejects.zip", {{"a.png", read_file(HDRVIEW_GUI_TEST_IMAGE)}});

        hdrview()->close_all_images();
        hdrview()->load_session(zip_path.string());
        // drained first, so a fallback to plain images would have landed by the time the list is read
        wait_for_loads(ctx);
        ctx->Yield(2);

        IM_CHECK_EQ(hdrview()->num_images(), 0);
    };
}
