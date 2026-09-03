/** \file test_gui_loader.cpp
    \author Wojciech Jarosz

    BackgroundImageLoader driven through a running app: which directories it watches and what stops it
    watching them, and what it does with an archive whose contents it has to discover for itself.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

#include <filesystem>
#include <fstream>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

namespace fs = std::filesystem;
using namespace hdrview_test;

namespace
{

/// Copies the fixture into `dir` under `name`, returning its path.
fs::path place_fixture(const fs::path &dir, const char *name)
{
    std::error_code ec;
    fs::path        dst = dir / name;
    fs::copy_file(HDRVIEW_GUI_TEST_IMAGE, dst, fs::copy_options::overwrite_existing, ec);
    return dst;
}

bool is_watched(const fs::path &dir) { return hdrview()->image_loader().watched_directories().count(dir) != 0; }

/// Writes a zip holding `contents` under "inside.png", into `dir` under `name`.
/**
    A non-zero `declared` overwrites the entry's uncompressed size in both of its headers, so the archive
    claims more than it stores; zero leaves it alone.
*/
fs::path write_zip(const fs::path &dir, const char *name, uint32_t declared, const std::string &contents)
{
    std::string bytes = zip_bytes({{"inside.png", contents}});
    if (declared)
        declare_uncompressed_size(bytes, declared);
    return write_file(dir / name, bytes);
}

} // namespace

void RegisterTests_Loader(ImGuiTestEngine *engine)
{
    /*
        "Add watched folder..." watches a folder you have not opened, to pick up files that do not exist
        yet, so no loaded image lives in it and a rule phrased as "drop the directories no loaded image
        came from" would throw it away.
    */
    ImGuiTest *t = IM_REGISTER_TEST(engine, "loader", "explicit_watch_outlives_the_images");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        const fs::path watch_dir = make_temp_dir("explicit");
        const fs::path image_dir = make_temp_dir("images");
        const fs::path image     = place_fixture(image_dir, "a.png");
        IM_CHECK(fs::exists(image));

        reset_images(ctx);
        hdrview()->image_loader().remove_watched_directories([](const fs::path &) { return true; });

        IM_CHECK(hdrview()->image_loader().add_watched_directory(watch_dir, true));
        IM_CHECK(is_watched(watch_dir));

        // an image from somewhere else; the watched folder stays empty throughout
        load_and_wait(ctx, {image.u8string()});
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(is_watched(watch_dir));

        hdrview()->close_image(0);
        IM_CHECK_EQ(hdrview()->num_images(), 0);
        IM_CHECK(is_watched(watch_dir));

        // nor may closing everything: the folder was asked for in its own right
        load_and_wait(ctx, {image.u8string()});
        hdrview()->close_all_images();
        IM_CHECK(is_watched(watch_dir));

        // the X button beside it in the watched-folders table removes it, and must still work
        hdrview()->image_loader().remove_watched_directories([&](const fs::path &p) { return p == watch_dir; });
        IM_CHECK(!is_watched(watch_dir));

        // the other half of the rule: a folder watched only because its images were opened is dropped once
        // none of them is loaded
        reset_images(ctx);
        hdrview()->load_images({image_dir.u8string()});
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(is_watched(image_dir));
        hdrview()->close_image(0);
        IM_CHECK(!is_watched(image_dir));

        std::error_code ec;
        fs::remove_all(watch_dir, ec);
        fs::remove_all(image_dir, ec);
    };

    /*
        Opening a zip is the path where the loader discovers the entries itself, so it allocates from
        whatever the archive claims. A claim its stored bytes cannot back is refused before the allocation:
        the buffer is value-initialized, so the pages are touched and overcommit does not soften it.
        tests/test_loader_limits.cpp covers the same guard on the two session-manifest paths.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "an_archive_lying_about_an_entry_is_refused_up_front");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir = make_temp_dir("zip");
        // well past deflate's 1032:1 ceiling, but small enough that a build without the guard wastes the
        // allocation instead of exhausting the machine. The payload has to be highly compressible: against
        // an already-compressed entry the same declared size would be a ratio deflate can reach.
        const fs::path lying = write_zip(dir, "lying.zip", 64u << 20, std::string(64, 'x'));

        reset_images(ctx);
        {
            LogCapture log;
            hdrview()->load_images({lying.u8string()});
            wait_for_loads(ctx);
            IM_CHECK_EQ(hdrview()->num_images(), 0);
            IM_CHECK(log.saw("Skipping zip entry 'inside.png'"));
        }

        // the same image in a truthful archive still opens, so the guard refuses the claim, not the format
        std::ifstream     in{HDRVIEW_GUI_TEST_IMAGE, std::ios::binary};
        const std::string fixture{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const fs::path    honest = write_zip(dir, "honest.zip", 0, fixture);
        reset_images(ctx);
        hdrview()->load_images({honest.u8string()});
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };

    /*
        Two reloads in flight for one image: the watch loop polls four times a second and schedules a reload
        on every timestamp change, so a file being written repeatedly outruns the load. The arrival is
        matched to its slot by pointer identity, so the second reload no longer finds the object it was told
        to replace.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "overlapping_reloads_replace_rather_than_accumulate");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir  = make_temp_dir("reload");
        const fs::path file = place_fixture(dir, "a.png");

        reset_images(ctx);
        load_and_wait(ctx, {file.u8string()});
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        auto original = hdrview()->image(0);
        IM_CHECK(original != nullptr);

        // both scheduled before either can finish, the way the watch loop schedules them
        hdrview()->reload_image(original);
        hdrview()->reload_image(original);
        wait_for_loads(ctx);

        // one file, one entry: whichever reload landed last
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(hdrview()->image(0) != original);
        IM_CHECK(hdrview()->image(0)->path == fs::path(file));

        // however many pile up, not just two
        auto current = hdrview()->image(0);
        for (int i = 0; i < 4; ++i) hdrview()->reload_image(current);
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(hdrview()->image(0)->path == fs::path(file));

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };

    // a folder joins the recent list only if loading it produced an image
    t           = IM_REGISTER_TEST(engine, "loader", "only_folders_that_opened_something_become_recent");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path empty_dir = make_temp_dir("recent_empty");
        const fs::path junk_dir  = make_temp_dir("recent_junk");
        const fs::path full_dir  = make_temp_dir("recent_full");
        place_fixture(full_dir, "a.png");
        {
            // a file no loader claims, so the folder is non-empty but yields nothing
            std::ofstream junk{junk_dir / "notes.txt"};
            junk << "not an image";
        }

        auto is_recent = [](const fs::path &p)
        {
            const auto &recents = hdrview()->image_loader().recent_files();
            return std::find(recents.begin(), recents.end(), p.u8string()) != recents.end();
        };

        reset_images(ctx);
        hdrview()->image_loader().clear_recent_files();

        hdrview()->load_images({empty_dir.u8string()});
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 0);
        IM_CHECK(!is_recent(empty_dir));

        hdrview()->load_images({junk_dir.u8string()});
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 0);
        IM_CHECK(!is_recent(junk_dir));

        // a folder that does hold an image is still recorded
        hdrview()->load_images({full_dir.u8string()});
        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(is_recent(full_dir));

        reset_images(ctx);
        hdrview()->image_loader().clear_recent_files();
        std::error_code ec;
        fs::remove_all(empty_dir, ec);
        fs::remove_all(junk_dir, ec);
        fs::remove_all(full_dir, ec);
    };

    /*
        A zip can hold an entry with no bytes in it. The loader decides where to read from by asking whether
        the buffer it was handed is empty, so a zero-byte entry must not fall through to "open this path",
        against a name assembled for display (archive.zip/inside.png) that no filesystem has.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "an_empty_zip_entry_is_reported_as_empty");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir   = make_temp_dir("empty_entry");
        const fs::path empty = write_zip(dir, "empty.zip", 0, std::string{});
        IM_CHECK(!empty.empty());

        reset_images(ctx);
        {
            LogCapture log;
            hdrview()->load_images({empty.u8string()});
            wait_for_loads(ctx);
            IM_CHECK_EQ(hdrview()->num_images(), 0);
            IM_CHECK(log.saw("is empty"));
            // it must not claim something is missing from the filesystem: the name it would be talking
            // about was assembled for display and was never a path
            IM_CHECK(!log.saw("doesn't exist"));
        }

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };

    /*
        The same identity matching in the other direction: an image closed while its reload was in flight.
        The arrival has a slot to fill that no longer exists, and appending it would put the image the user
        just closed back in the list.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "closing_an_image_mid_reload_does_not_resurrect_it");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir  = make_temp_dir("closed_reload");
        const fs::path file = place_fixture(dir, "a.png");

        reset_images(ctx);
        load_and_wait(ctx, {file.u8string()});
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        hdrview()->reload_image(hdrview()->image(0));
        hdrview()->close_image(0);
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        wait_for_loads(ctx);
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        // an ordinary load still adds, so this is not refusing arrivals in general
        load_and_wait(ctx, {file.u8string()});
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };
}
