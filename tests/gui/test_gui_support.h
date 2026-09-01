/** \file test_gui_support.h
    \author Wojciech Jarosz

    Shared scaffolding for the GUI suite: waiting, loading, and temporary files.

    Every one of these tests has to wait for something -- a background load to land, a window to close, a
    queue to drain -- and each file had grown its own loop to do it. Besides the duplication, a loop that
    yields a fixed number of frames is waiting for a duration rather than for the thing it cares about: too
    long here (a load that finished in three frames still costs two hundred), and potentially too short on
    a busy CI runner, which is a flake nobody would reproduce locally.

    So there is one primitive, wait_until(), and everything else is phrased in terms of the condition it is
    actually waiting for.
*/

#pragma once

#include "app.h"
#include "image.h"
#include "imageio/image_loader.h"

#include "imgui_test_engine/imgui_te_context.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace hdrview_test
{

namespace fs = std::filesystem;

//! Yields until `done` holds, or `timeout` passes. Returns whether it held.
/*!
    The budget is a duration, not a number of frames. What these tests wait on is background work -- a
    decode on another thread, a file appearing -- whose length has nothing to do with how fast this thread
    can spin, and the suite runs with vsync off, so a frame costs microseconds. A frame budget that stood
    for "about two seconds" at 60 Hz stands for about five milliseconds now, and the wait would return
    before the work it is waiting for could possibly have finished.

    It is a backstop against hanging the suite, not the thing being waited for: pass a condition that says
    what has to become true, and the wait costs only as long as that takes.
*/
template <typename F>
bool wait_until(ImGuiTestContext *ctx, F &&done, std::chrono::milliseconds timeout = std::chrono::seconds(20))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return done();
        ctx->Yield();
    }
    return true;
}

//! Waits for every queued background load to be drained into the image list.
/*!
    This is the honest end of a load: BackgroundImageLoader hands finished images to the app and drops them
    from its queue in the same pass, so an empty queue means every image that is going to arrive has. It is
    also the only way to wait for an image *not* to appear -- a fixed number of frames only ever says "not
    yet".
*/
inline bool wait_for_loads(ImGuiTestContext *ctx)
{
    return wait_until(ctx, [] { return hdrview()->image_loader().num_pending_images() == 0; });
}

//! Loads `paths` and waits for all of them, returning the resulting image count.
inline int load_and_wait(ImGuiTestContext *ctx, const std::vector<std::string> &paths)
{
    hdrview()->load_images(paths);
    wait_for_loads(ctx);
    return hdrview()->num_images();
}

//! Closes everything and waits for the list to empty, so a test starts from a known state.
/*!
    Deliberately the unguarded close: close_all_images() asks before discarding edits, and a test that just
    edited something would sit waiting on a modal that nothing is going to answer. Getting back to an empty
    list is scaffolding, not a user action.
*/
inline void reset_images(ImGuiTestContext *ctx)
{
    hdrview()->close_all_images_immediately();
    wait_until(ctx, [] { return hdrview()->num_images() == 0; });
}

//! Put the view controls back where a person would leave them.
/*!
    Several tests set these to values nobody would choose -- an exposure of 1e30 to check that a session
    file carrying one does not take the app with it, a zoom of a hundredth to check the transform stays
    invertible. Nothing resets them between tests, so whatever the last one left is what every test after
    it is drawn through, and a suite that is passing ends up looking like a suite that has broken.

    Only the view: the images are reset_images()'s business, and an edit's own undo is the test's.
*/
inline void reset_view_controls(ImGuiTestContext *ctx)
{
    hdrview()->exposure() = hdrview()->exposure_live() = 0.f;
    hdrview()->offset() = hdrview()->offset_live() = 0.f;
    hdrview()->gamma() = hdrview()->gamma_live() = 1.f;

    hdrview()->set_zoom(1.f);
    hdrview()->center();
    // Deliberately the default-constructed box, which is the inverted one with INT_MAX and INT_MIN
    // corners -- the same thing Deselect passes. Anything that reads a cleared selection has to cope with
    // it, and clearing to the degenerate box at the origin instead would leave the suite unable to notice
    // that something did not.
    hdrview()->set_selection(Box2i{});
    ctx->Yield();
}

//! Keeps the frame loop running for `duration`, whatever happens.
/*!
    For the one case that is not waiting for a condition: giving a thread that should no longer be running
    a window in which to prove that it is. There is nothing to observe -- what catches the misbehaviour is
    the sanitizer -- so the window is a duration, and has to stay one. Counting frames instead would have
    quietly become a 400x shorter soak the moment the suite stopped waiting on the display.
*/
inline void soak(ImGuiTestContext *ctx, std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) ctx->Yield();
}

//! Waits for a channel's statistics to finish, and returns them.
/*!
    Statistics are computed off the main thread, and get_stats() hands back a different object as that work
    lands, so it has to be re-read each time round rather than cached before the wait.
*/
inline PixelStats *wait_for_stats(ImGuiTestContext *ctx, Channel &channel,
                                  const std::function<bool(const PixelStats *)> &also = {})
{
    PixelStats *stats = nullptr;
    wait_until(ctx,
               [&]
               {
                   stats = channel.get_stats();
                   return stats && stats->computed && (!also || also(stats));
               });
    return stats;
}

//! A fresh, empty directory under the system temp dir, canonicalized the way the loader stores paths.
inline fs::path make_temp_dir(const char *stem)
{
    std::error_code ec;
    fs::path        d = fs::temp_directory_path(ec) / (std::string("hdrview_test_") + stem);
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return fs::weakly_canonical(d, ec);
}

} // namespace hdrview_test
