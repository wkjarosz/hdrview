/** \file test_gui_session.cpp
    \author Wojciech Jarosz

    Session save/load (HDRViewApp::save_session()/load_session()/begin_session_load()/
    finish_pending_session() in src/app-file-io.cpp), driven by writing a .hsess file and calling
    load_images()/load_session()/"Open recent": the menu items themselves open native file dialogs that
    cannot be automated here.
*/

#include "app.h"
#include "image.h"
#include "json.h"
#include "test_gui_registry.h"
#include "version.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

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
        // the call the GLFW drop callback, CLI args and Finder "Open With" all funnel through
        hdrview()->load_images({session_path.string()});
        wait_for_loads(ctx);

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "session", "hsess_from_open_recent_loads_as_session");
    t->TestFunc = [](ImGuiTestContext *ctx)
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
        fs::path session_path = write_temp_session(j, "hdrview_test_recent.hsess");

        // an entry is addressed below by the path prefix its truncated label exposes, so start from an
        // empty list and let only this session's temp-directory path match
        ctx->SetRef("##MainMenuBar"); // the menu bar is its own top-level window (see test_gui_dialogs.cpp)
        ctx->MenuClick("File/Open recent/Clear recently opened");

        // loading the session once is what puts it in the recent list
        hdrview()->close_all_images();
        hdrview()->load_session(session_path.string());
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        hdrview()->close_all_images();
        ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        // entries are labeled with the middle-elided path, so their IDs are unpredictable: match on the
        // leading part that survives ImGuiTestItemInfo::DebugLabel's 32-char truncation
        ctx->SetRef("##MainMenuBar");
        ctx->MenuAction(ImGuiTestAction_Open, "File/Open recent");
        ImGuiTestItemList entries;
        ctx->GatherItems(&entries, "//$FOCUSED", -1);
        ImGuiID recent_id = 0;
        for (const ImGuiTestItemInfo &item : entries)
        {
            // each label is "<path>##File<n>", and a short path leaves part of that suffix inside the
            // truncation, possibly just one of its two '#': cut at the first. Linux's /tmp is short enough.
            string label{item.DebugLabel};
            label = label.substr(0, label.find('#'));
            if (!label.empty() && session_path.string().rfind(label, 0) == 0)
            {
                IM_CHECK_EQ(recent_id, (ImGuiID)0); // the entry must be unambiguous
                recent_id = item.ID;
            }
        }
        IM_CHECK(recent_id != 0);
        ctx->ItemClick(recent_id);

        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
    };

    t           = IM_REGISTER_TEST(engine, "session", "duplicate_image_current_and_reference_stay_distinct");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // the same fixture listed twice, with a different selected_group/reference_group per entry
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
        // load_session(const string&) is the non-dialog overload load_images() dispatches .hsess paths to
        hdrview()->load_session(session_path.string());
        wait_for_loads(ctx);

        IM_CHECK_EQ(hdrview()->num_images(), 2);
        // both occurrences of the same path must resolve to distinct Image instances, with current and
        // reference each pointing at their own. IM_CHECK returns from this lambda on failure, so a null
        // current/reference below is never dereferenced.
        IM_CHECK(hdrview()->current_image_index() != hdrview()->reference_image_index());
        IM_CHECK(hdrview()->current_image() != nullptr);
        IM_CHECK(hdrview()->reference_image() != nullptr);
        IM_CHECK(hdrview()->current_image() != hdrview()->reference_image());
        IM_CHECK_EQ(hdrview()->current_image()->selected_group, 0);
        IM_CHECK_EQ(hdrview()->reference_image()->reference_group, 1);
    };

    // written by build_session_manifest(), so a name it writes that the loader does not read shows up here
    // as a selection that did not come back
    t           = IM_REGISTER_TEST(engine, "session", "multi_selection_survives_a_round_trip");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        reset_images(ctx);
        IM_CHECK_SILENT(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2}) == 2);

        hdrview()->set_current_image_index(0);
        hdrview()->toggle_group_selected(1, hdrview()->image(1)->selected_group);
        IM_CHECK(hdrview()->image(0)->is_selected());
        IM_CHECK(hdrview()->image(1)->is_selected());

        // absolute paths, so the file can sit in the temp directory and still find its images
        json     j = hdrview()->build_session_manifest([](ConstImagePtr img) { return img->path.generic_u8string(); });
        fs::path session_path = write_temp_session(j, "hdrview_test_selection.hsess");

        reset_images(ctx);
        hdrview()->load_session(session_path.string());
        wait_for_loads(ctx);
        wait_until(ctx, [] { return hdrview()->num_images() == 2; });

        IM_CHECK_EQ(hdrview()->num_images(), 2);

        // both come back selected: without the selection in the file, update_visibility() collapses it onto
        // the group the current image is showing
        for (int i = 0; i < 2; ++i) IM_CHECK(hdrview()->image(i)->is_selected());

        fs::remove(session_path);
    };

    t           = IM_REGISTER_TEST(engine, "session", "missing_file_entry_logs_warning_and_loads_rest");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // one entry points at a file that does not exist, alongside one that does: BackgroundImageLoader
        // never reports failures, so finish_pending_session() detects one as num_pending_images() reaching
        // 0 with an entry still unresolved
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
        wait_for_loads(ctx);

        // the missing entry never arrives: only the good one loads, and reference (entry 1) stays unset
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);
        IM_CHECK_EQ(hdrview()->reference_image_index(), -1);
    };

    t           = IM_REGISTER_TEST(engine, "session", "out_of_range_values_are_clamped");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // a session file is ordinary user data, and several of its fields are used as array indices or
        // divisors; none of these values is reachable through the GUI
        json entry;
        entry["path"]           = fs::path(HDRVIEW_GUI_TEST_IMAGE).generic_u8string();
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
        wait_for_loads(ctx);

        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(hdrview()->current_image() != nullptr);

        auto img = hdrview()->current_image();
        IM_CHECK(img->is_valid_group(img->selected_group));

        // colormap() indexes m_colormaps with the restored value, so an out-of-range one is visible from
        // outside only as the colormap it returns
        IM_CHECK(hdrview()->colormap() >= 0);
        IM_CHECK(hdrview()->colormap() < Colormap_COUNT);

        // zoom stays invertible, so pixel_at_vp_pos() returns a finite coordinate: the selection and pixel
        // inspector convert that to an int2, which is undefined for a non-finite float
        IM_CHECK(hdrview()->zoom() >= HDRViewApp::MIN_ZOOM);
        IM_CHECK(hdrview()->zoom() <= HDRViewApp::MAX_ZOOM);

        // an inverted selection intersects the data window into an inverted box, whose negative volume() is
        // a near-2^64 pixel count where PixelStats::calculate() takes it as a size_t
        IM_CHECK(hdrview()->roi().min.x <= hdrview()->roi().max.x);
        IM_CHECK(hdrview()->roi().min.y <= hdrview()->roi().max.y);

        // gamma keeps only its floor, since it is inverted before use; exposure and offset come back as
        // written, because Ctrl+click entry and the shortcuts can set values outside the sliders' travel
        IM_CHECK(hdrview()->gamma() >= MIN_GAMMA);
        IM_CHECK_EQ(hdrview()->exposure(), 1e30f);
        IM_CHECK_EQ(hdrview()->offset(), -1e30f);

        // let a few frames run so the pixel inspector and the statistics window touch these
        ctx->Yield(5);

        // the hostile values must not outlive this test: the zoom of zero is clamped to the smallest there
        // is, which leaves every later test looking at a speck
        reset_view_controls(ctx);
    };

    // The prompt only appears with images already open, which every other test here has closed. Loading a
    // session over one is the only way to reach it, so this opens an image first on purpose.
    t           = IM_REGISTER_TEST(engine, "session", "replacing_a_session_asks_first");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        json j;
        j["type"]    = "HDRView session";
        j["version"] = current_version_string();
        json entry;
        entry["path"]               = fs::path(HDRVIEW_GUI_TEST_IMAGE).generic_u8string();
        j["images"]                 = json::array({entry});
        j["current"]                = 0;
        j["reference"]              = -1;
        j["blend_mode"]             = "normal";
        j["view"]                   = json::object();
        const fs::path session_path = write_temp_session(j, "hdrview_test_replace.hsess");

        reset_images(ctx);
        IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE_2}), 1);
        const int kept_id = hdrview()->current_image()->id;

        // cancelling leaves the open image alone
        hdrview()->load_session(session_path.string());
        ctx->SetRef("");
        IM_CHECK(wait_until(ctx, [&] { return ctx->WindowInfo("Replace session?", ImGuiTestOpFlags_NoError).Window; }));
        ctx->SetRef("Replace session?");
        ctx->ItemClick("Cancel");
        ctx->SetRef("");
        ctx->Yield(4);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK_EQ(hdrview()->current_image()->id, kept_id);

        // confirming loads the session in its place
        hdrview()->load_session(session_path.string());
        IM_CHECK(wait_until(ctx, [&] { return ctx->WindowInfo("Replace session?", ImGuiTestOpFlags_NoError).Window; }));
        ctx->SetRef("Replace session?");
        ctx->ItemClick("Replace");
        ctx->SetRef("");
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(hdrview()->current_image()->id != kept_id);
    };
}
