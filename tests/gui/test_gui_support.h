/** \file test_gui_support.h
    \author Wojciech Jarosz

    Shared scaffolding for the GUI suite: waiting, loading, and temporary files.
*/

#pragma once

#include "app.h"
#include "image.h"
#include "imageio/image_loader.h"

#include "../test_log_capture.h"
#include "../test_zip.h"

#include "imgui_test_engine/imgui_te_context.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hdrview_test
{

namespace fs = std::filesystem;

/// Yields until `done` holds, or `timeout` passes. Returns whether it held.
/**
    The budget is a duration, not a frame count: vsync is off, so a frame costs microseconds.
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

/// Waits for every queued background load to be drained into the image list.
inline bool wait_for_loads(ImGuiTestContext *ctx)
{
    return wait_until(ctx, [] { return hdrview()->image_loader().num_pending_images() == 0; });
}

/// Loads `paths` and waits for all of them, returning the resulting image count.
inline int load_and_wait(ImGuiTestContext *ctx, const std::vector<std::string> &paths)
{
    hdrview()->load_images(paths);
    wait_for_loads(ctx);
    return hdrview()->num_images();
}

/// Closes everything without prompting and waits for the list to empty, so a test starts from a known state.
inline void reset_images(ImGuiTestContext *ctx)
{
    hdrview()->close_all_images_immediately();
    wait_until(ctx, [] { return hdrview()->num_images() == 0; });
}

/// Put the view controls back where a person would leave them, since nothing else resets them between tests.
inline void reset_view_controls(ImGuiTestContext *ctx)
{
    hdrview()->exposure() = hdrview()->exposure_live() = 0.f;
    hdrview()->offset() = hdrview()->offset_live() = 0.f;
    hdrview()->gamma() = hdrview()->gamma_live() = 1.f;

    hdrview()->set_zoom(1.f);
    hdrview()->center();
    // the default-constructed box is the inverted one with INT_MAX/INT_MIN corners, what Deselect passes
    hdrview()->set_selection(Box2i{});
    ctx->Yield();
}

/// Keeps the frame loop running for `duration`, giving a thread that should not still be running time to act.
inline void soak(ImGuiTestContext *ctx, std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) ctx->Yield();
}

/// Waits for a channel's statistics to finish, and returns them.
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

/// Writes `bytes` to `path` and returns it.
inline fs::path write_file(const fs::path &path, const std::string &bytes)
{
    std::ofstream os{path, std::ios::binary};
    os.write(bytes.data(), (std::streamsize)bytes.size());
    return path;
}

/// A fresh, empty directory under the system temp dir, canonicalized the way the loader stores paths.
inline fs::path make_temp_dir(const char *stem)
{
    std::error_code ec;
    fs::path        d = fs::temp_directory_path(ec) / (std::string("hdrview_test_") + stem);
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return fs::weakly_canonical(d, ec);
}

} // namespace hdrview_test
