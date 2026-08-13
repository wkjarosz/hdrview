/** \file test_gui_session_bundle.cpp
    \author Wojciech Jarosz

    Coverage for loading a session bundled inside a zip (a "*.hsess" manifest at the zip's root, alongside
    the images it references) - the mechanism that makes session sharing work on the Emscripten/web build,
    which has no real filesystem and whose file-upload helper can only select one file at a time (so a
    manifest plus several images has to arrive as one zip). The load-side logic is identical on native and
    web (same shared code, fed zip bytes from different sources), so exercising it here via
    HDRViewApp::load_images() - the same entry point drag-and-drop and CLI args use - gives real coverage of
    the web path too, even though this test suite can't drive an actual browser.

    Bundles are built directly with miniz's writer API here rather than through
    HDRViewApp::export_session_bundle() (which opens a native save-file dialog that can't be automated).
*/

#include "app.h"
#include "common.h"
#include "image.h"
#include "json.h"
#include "test_gui_registry.h"
#include "version.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <miniz.h>

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
    fs::path       zip_path = fs::temp_directory_path() / name;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    mz_zip_writer_init_file(&zip, zip_path.string().c_str(), 0);
    for (auto &[entry_name, contents] : entries)
        mz_zip_writer_add_mem(&zip, entry_name.c_str(), contents.data(), contents.size(), MZ_DEFAULT_LEVEL);
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return zip_path;
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
        // A non-default value that only gets applied if this actually went through session loading (the
        // plain-multi-image-zip fallback never touches blend_mode) -- without this, a single-image bundle
        // is indistinguishable from "detection silently failed, but the fallback path happened to load the
        // one image anyway", since extract_and_schedule() would extract this same entry as an ordinary
        // image regardless.
        j["blend_mode"] = "difference";
        j["view"]       = json::object();

        hdrview()->blend_mode() = BlendMode_Normal;

        fs::path zip_path =
            write_test_zip("hdrview_test_bundle.zip", {{"session.hsess", j.dump(4)},
                                                       {"images/000_fixture.png", read_file(HDRVIEW_GUI_TEST_IMAGE)}});

        hdrview()->close_all_images();
        // The same entry point drag-and-drop and CLI args use.
        hdrview()->load_images({zip_path.string()});
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
        IM_CHECK_EQ((int)hdrview()->blend_mode(), (int)BlendMode_Difference);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "zip_without_manifest_loads_as_plain_images");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // Regression check: a zip with no root .hsess entry must still load as an ordinary multi-image zip
        // (the pre-existing behavior), not be mistaken for a broken session bundle.
        fs::path zip_path = write_test_zip("hdrview_test_plain.zip", {{"a.png", read_file(HDRVIEW_GUI_TEST_IMAGE)},
                                                                      {"b.png", read_file(HDRVIEW_GUI_TEST_IMAGE_2)}});

        hdrview()->close_all_images();
        hdrview()->load_images({zip_path.string()});
        for (int frame = 0; frame < 120 && hdrview()->num_images() < 2; ++frame) ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 2);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "duplicate_image_in_bundle_stays_distinct");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // Mirrors test_gui_session.cpp's duplicate-image case, but for a zip-bundled session: the same
        // in-bundle path listed twice, to confirm the (path, channel_selector) matching machinery shared
        // with the plain-file case also disambiguates correctly when entries are extracted from a zip
        // rather than read from disk.
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
        for (int frame = 0; frame < 120 && hdrview()->num_images() < 2; ++frame) ctx->Yield();

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
        // load_session(const string&) -- the function behind the "Load session..." menu item -- must accept
        // a .zip bundle directly, not just a plain .hsess (this is what "Load session..." should now support
        // on native, per the explicit request to unify it with bundle loading).
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
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "explicit_load_session_rejects_non_bundle_zip");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // The strict counterpart of the above: an explicit "Load session..." on a zip with no manifest
        // must not silently fall back to loading it as plain images (unlike drag-and-drop/"Open image...",
        // where that fallback is the right move) -- num_images() should stay at whatever it was.
        fs::path zip_path =
            write_test_zip("hdrview_test_explicit_load_rejects.zip", {{"a.png", read_file(HDRVIEW_GUI_TEST_IMAGE)}});

        hdrview()->close_all_images();
        hdrview()->load_session(zip_path.string());
        ctx->Yield();
        ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "session_bundle", "reexporting_bundle_loaded_image_recovers_its_bytes");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // An image loaded from inside a bundle carries a synthetic "zip_path/entry_path" identity rather
        // than a real filesystem path (see begin_bundle_session_load) -- export_session_bundle() must
        // recognize that and re-extract the original bytes from the source zip rather than silently
        // omitting the image (which previously produced a bundle containing only the manifest, with no
        // images, whenever every loaded image came from a zip).
        std::string fixture_bytes = read_file(HDRVIEW_GUI_TEST_IMAGE);

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

        fs::path zip_path = write_test_zip("hdrview_test_reexport_bundle.zip",
                                           {{"session.hsess", j.dump(4)}, {"images/000_fixture.png", fixture_bytes}});

        hdrview()->close_all_images();
        hdrview()->load_images({zip_path.string()});
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        auto img = hdrview()->image(0);
        // Confirms the loaded image's path is indeed the synthetic, non-real-file form this test targets.
        IM_CHECK(!fs::exists(img->path));

        // Exercises the same split_zip_entry()+zip_extract_entry() combination export_session_bundle() uses
        // to recover the original bytes from a synthetic zip-entry path.
        std::string source = img->path.u8string();
        std::string source_zip, entry_path;
        IM_CHECK(split_zip_entry(source, source_zip, entry_path));
        IM_CHECK(!entry_path.empty());

        std::string zip_bytes = read_file(source_zip);
        auto        recovered = zip_extract_entry(zip_bytes, entry_path);
        IM_CHECK(recovered.has_value());
        IM_CHECK(*recovered == fixture_bytes);
    };
}
