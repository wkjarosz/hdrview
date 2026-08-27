/** \file test_gui_session.cpp
    \author Wojciech Jarosz

    Coverage for two properties of session save/load (see
    HDRViewApp::save_session()/load_session()/begin_session_load()/finish_pending_session() in
    src/app-file-io.cpp):

    1. A `.hsess` session file arriving through the same code path as an image (drag-and-drop, CLI args,
       Finder "Open With", the "Open image..." dialog - all of which funnel through
       HDRViewApp::load_images()) is routed to session loading rather than treated as an unsupported image.
    2. Listing the same image path more than once in one session (e.g. to compare two channel groups of it
       side by side) resolves each occurrence to its own distinct pending load, since identity isn't
       tracked by path alone; current/reference don't collapse onto the same image.

    These don't drive the "Save session..."/"Load session..." menu items (which open native file dialogs
    that can't be automated here) - instead they write a hand-crafted .hsess file directly and exercise
    load_images()/load_session() exactly as those entry points do.
*/

#include "app.h"
#include "image.h"
#include "json.h"
#include "test_gui_registry.h"
#include "version.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <filesystem>
#include <fstream>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

namespace
{

fs::path write_temp_session(const json &j, const char *name)
{
    fs::path      path = fs::temp_directory_path() / name;
    std::ofstream ofs{path};
    ofs << j.dump(4);
    return path;
}

string current_version_string() { return fmt::format("{}.{}.{}", version_major(), version_minor(), version_patch()); }

} // namespace

void RegisterTests_Session(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "session", "hsess_dropped_as_image_loads_as_session");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        json j;
        j["type"]    = "HDRView session";
        j["version"] = current_version_string();
        json entry;
        entry["path"]         = fs::path(HDRVIEW_GUI_TEST_IMAGE).generic_u8string();
        j["images"]           = json::array({entry});
        j["current"]          = 0;
        j["reference"]        = -1;
        j["blend_mode"]       = "normal";
        j["view"]             = json::object();
        fs::path session_path = write_temp_session(j, "hdrview_test_dnd.hsess");

        hdrview()->close_all_images();
        // This is the exact call the GLFW drop callback, CLI args, and macOS Finder "Open With" all funnel
        // through (see load_images() in src/app-file-io.cpp) - calling it directly with a .hsess path is a
        // faithful stand-in for dragging one onto the app.
        hdrview()->load_images({session_path.string()});
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "session", "duplicate_image_current_and_reference_stay_distinct");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // The same fixture listed twice, with different selected_group/reference_group per entry - mirrors
        // the reported repro (same image loaded twice, one channel group picked for current, a different
        // one for reference).
        json entry0, entry1;
        entry0["path"]            = fs::path(HDRVIEW_GUI_TEST_IMAGE).generic_u8string();
        entry0["selected_group"]  = 0;
        entry1["path"]            = fs::path(HDRVIEW_GUI_TEST_IMAGE).generic_u8string();
        entry1["reference_group"] = 1;

        json j;
        j["type"]             = "HDRView session";
        j["version"]          = current_version_string();
        j["images"]           = json::array({entry0, entry1});
        j["current"]          = 0;
        j["reference"]        = 1;
        j["blend_mode"]       = "normal";
        j["view"]             = json::object();
        fs::path session_path = write_temp_session(j, "hdrview_test_duplicate.hsess");

        hdrview()->close_all_images();
        // load_session(const string&) is the non-dialog overload load_images() dispatches .hsess paths to;
        // calling it directly here skips the native file-picker, which can't be driven by the test engine.
        hdrview()->load_session(session_path.string());
        for (int frame = 0; frame < 120 && hdrview()->num_images() < 2; ++frame) ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 2);
        // The core of the bug: both occurrences of the same path must resolve to distinct Image instances,
        // and current/reference must each point at their own intended occurrence, not collapse to one.
        // IM_CHECK returns from this lambda on failure, so a null current/reference below is never
        // dereferenced.
        IM_CHECK(hdrview()->current_image_index() != hdrview()->reference_image_index());
        IM_CHECK(hdrview()->current_image() != nullptr);
        IM_CHECK(hdrview()->reference_image() != nullptr);
        IM_CHECK(hdrview()->current_image() != hdrview()->reference_image());
        IM_CHECK_EQ(hdrview()->current_image()->selected_group, 0);
        IM_CHECK_EQ(hdrview()->reference_image()->reference_group, 1);
    };

    t           = IM_REGISTER_TEST(engine, "session", "missing_file_entry_logs_warning_and_loads_rest");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // One entry points at a file that doesn't exist alongside one that does - exercises
        // finish_pending_session()'s partial-failure path (BackgroundImageLoader never reports failures
        // directly; num_pending_images() reaching 0 with an unresolved entry left is how it's detected).
        json good, missing;
        good["path"]    = fs::path(HDRVIEW_GUI_TEST_IMAGE).generic_u8string();
        missing["path"] = (fs::temp_directory_path() / "hdrview_test_does_not_exist.png").generic_u8string();

        json j;
        j["type"]             = "HDRView session";
        j["version"]          = current_version_string();
        j["images"]           = json::array({good, missing});
        j["current"]          = 0;
        j["reference"]        = 1;
        j["blend_mode"]       = "normal";
        j["view"]             = json::object();
        fs::path session_path = write_temp_session(j, "hdrview_test_missing.hsess");

        hdrview()->close_all_images();
        hdrview()->load_session(session_path.string());
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();

        // The missing entry never arrives; only the good one should load, and reference (entry 1) should
        // stay unset rather than the modal hanging or the app crashing.
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
        IM_CHECK_EQ(hdrview()->reference_image_index(), -1);
    };

    t           = IM_REGISTER_TEST(engine, "session", "out_of_range_values_are_clamped");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // A session file is ordinary user data, and several of its fields are used as array indices or as
        // divisors. None of these values is reachable through the GUI; they stand in for a hand-edited,
        // truncated, or version-skewed file.
        json entry;
        entry["path"]            = fs::path(HDRVIEW_GUI_TEST_IMAGE).generic_u8string();
        entry["selected_group"] = 9999; // indexed unchecked by raw_pixel() via active_group_index()

        json view;
        view["colormap_index"] = 9999; // indexes HDRViewApp::m_colormaps
        view["zoom"]           = 0.f;  // pixel_at_vp_pos() divides by this
        view["gamma"]          = 0.f;  // inverted before use, so this divides by zero
        view["exposure"]       = 1e30f;
        view["offset"]         = -1e30f;
        view["roi"]            = json::array({json::array({100, 100}), json::array({10, 10})}); // inverted

        json j;
        j["type"]             = "HDRView session";
        j["version"]          = current_version_string();
        j["images"]           = json::array({entry});
        j["current"]          = 0;
        j["reference"]        = -1;
        j["blend_mode"]       = "normal";
        j["view"]             = view;
        fs::path session_path = write_temp_session(j, "hdrview_test_out_of_range.hsess");

        hdrview()->close_all_images();
        hdrview()->load_session(session_path.string());
        for (int frame = 0; frame < 120 && hdrview()->num_images() == 0; ++frame) ctx->Yield();

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(hdrview()->current_image() != nullptr);

        auto img = hdrview()->current_image();
        IM_CHECK(img->is_valid_group(img->selected_group));

        // colormap() indexes m_colormaps with the restored value, so an out-of-range one is only visible
        // from outside as the colormap it returns.
        IM_CHECK(hdrview()->colormap() >= 0);
        IM_CHECK(hdrview()->colormap() < Colormap_COUNT);

        // Zoom stays invertible, so pixel_at_vp_pos() keeps returning a finite coordinate -- the selection
        // and pixel inspector convert that to an int2, which is undefined for a non-finite float.
        IM_CHECK(hdrview()->zoom() >= HDRViewApp::MIN_ZOOM);
        IM_CHECK(hdrview()->zoom() <= HDRViewApp::MAX_ZOOM);

        // An inverted selection intersects the data window into an inverted box, whose negative volume()
        // is a near-2^64 pixel count where PixelStats::calculate() takes it as a size_t.
        IM_CHECK(hdrview()->roi().min.x <= hdrview()->roi().max.x);
        IM_CHECK(hdrview()->roi().min.y <= hdrview()->roi().max.y);

        // Gamma keeps only its floor -- it is inverted before use. Exposure and offset are carried back
        // as written, since Ctrl+click entry and the keyboard shortcuts can set values outside the
        // sliders' travel and a session has to round-trip those.
        IM_CHECK(hdrview()->gamma() >= HDRViewApp::MIN_GAMMA);
        IM_CHECK_EQ(hdrview()->exposure(), 1e30f);
        IM_CHECK_EQ(hdrview()->offset(), -1e30f);

        // Let a few frames run so anything reading these (the pixel inspector, the statistics window)
        // actually touches them.
        ctx->Yield(5);
    };
}
