/** \file test_gui_loader.cpp
    \author Wojciech Jarosz

    BackgroundImageLoader driven through a running app: which directories it watches, and what stops it
    watching them.

    Distinct from test_gui_image_io.cpp, which loads a file the caller named. Here the loader is the one
    deciding what to open, so the tests are about the decisions rather than the decode.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <filesystem>
#include <fstream>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

namespace fs = std::filesystem;

namespace
{

//! A fresh, empty directory under the system temp dir, canonicalized the way the loader stores paths.
fs::path make_temp_dir(const char *stem)
{
    std::error_code ec;
    fs::path        d = fs::temp_directory_path(ec) / fmt::format("hdrview_watch_test_{}", stem);
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return fs::weakly_canonical(d, ec);
}

//! Copies the fixture into `dir` under `name`, returning its path.
fs::path place_fixture(const fs::path &dir, const char *name)
{
    std::error_code ec;
    fs::path        dst = dir / name;
    fs::copy_file(HDRVIEW_GUI_TEST_IMAGE, dst, fs::copy_options::overwrite_existing, ec);
    return dst;
}

bool is_watched(const fs::path &dir) { return hdrview()->image_loader().watched_directories().count(dir) != 0; }

void reset_images(ImGuiTestContext *ctx)
{
    hdrview()->close_all_images();
    for (int frame = 0; frame < 60 && hdrview()->num_images() != 0; ++frame) ctx->Yield();
}

void load_and_wait(ImGuiTestContext *ctx, const fs::path &file)
{
    const int before = hdrview()->num_images();
    hdrview()->load_images({file.u8string()});
    for (int frame = 0; frame < 240 && hdrview()->num_images() <= before; ++frame) ctx->Yield();
}

} // namespace

void RegisterTests_Loader(ImGuiTestEngine *engine)
{
    /*
        "Add watched folder..." exists to watch a folder you have *not* opened -- its whole point is to pick
        up files that do not exist yet. So no loaded image ever lives in it, and any rule phrased as "drop
        the directories no loaded image came from" throws it away. Closing an unrelated image must not.
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

        // An image from somewhere else entirely; the watched folder stays empty throughout.
        load_and_wait(ctx, image);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(is_watched(watch_dir));

        hdrview()->close_image(0);
        IM_CHECK_EQ(hdrview()->num_images(), 0);
        IM_CHECK(is_watched(watch_dir));

        // Nor may closing everything, for the same reason: the folder was asked for in its own right.
        load_and_wait(ctx, image);
        hdrview()->close_all_images();
        IM_CHECK(is_watched(watch_dir));

        // The X button beside it in the watched-folders table is what removes it, and must still work.
        hdrview()->image_loader().remove_watched_directories([&](const fs::path &p) { return p == watch_dir; });
        IM_CHECK(!is_watched(watch_dir));

        // The other half of the rule, so this does not simply pin every directory forever: a folder watched
        // only because its images were opened is still dropped once none of them is loaded.
        reset_images(ctx);
        hdrview()->load_images({image_dir.u8string()});
        for (int frame = 0; frame < 240 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(is_watched(image_dir));
        hdrview()->close_image(0);
        IM_CHECK(!is_watched(image_dir));

        std::error_code ec;
        fs::remove_all(watch_dir, ec);
        fs::remove_all(image_dir, ec);
    };
}
