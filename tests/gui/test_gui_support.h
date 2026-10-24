/** \file test_gui_support.h
    \author Wojciech Jarosz

    Shared scaffolding for the GUI suite: waiting, loading, dragging over the viewport, and temporary files.
*/

#pragma once

#include "app.h"
#include "edit/commands.h"
#include "image.h"
#include "imageio/image_loader.h"

#include "../test_log_capture.h"
#include "../test_zip.h"

#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace hdrview_test
{

namespace fs = std::filesystem;

/// Yields until `done` holds or `timeout` passes, and returns whether it held.
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

/// \p name with each slash escaped, which a menu path or a window ref would otherwise read as a separator.
inline std::string escaped(const std::string &name)
{
    std::string out;
    for (char c : name)
    {
        if (c == '/')
            out += '\\';
        out += c;
    }
    return out;
}

/// The Edit menu's path to the command named \p name, through the submenu that lists it, under \p label if it has one.
inline std::string edit_menu_item(const std::string &name, const std::string &label = {})
{
    const char *submenu = edit_submenu_of(name);
    return std::string("Edit/") + (submenu ? std::string(submenu) + "/" : std::string()) +
           escaped(label.empty() ? name : label);
}

/// Closes everything without prompting and waits for the list to empty, so a test starts from a known state.
inline void reset_images(ImGuiTestContext *ctx)
{
    hdrview()->close_all_images_immediately();
    wait_until(ctx, [] { return hdrview()->num_images() == 0; });
    // and at 100%, which a reshaping edit's refit or a test's zoom would otherwise carry into the next image loaded
    hdrview()->set_zoom(1.f);
}

/// Puts the view controls back where a person would leave them, since nothing else resets them between tests.
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

/// Runs a function as it goes out of scope, so state a test changes is put back even when a failed check returns.
template <typename F>
struct OnExit
{
    explicit OnExit(F f) : f(std::move(f)) {}
    ~OnExit() { f(); }
    OnExit(const OnExit &)            = delete;
    OnExit &operator=(const OnExit &) = delete;
    F       f;
};

/// A left-button drag from \p from by \p by, both in app coordinates, with \p mods held throughout.
inline void drag(ImGuiTestContext *ctx, float2 from, float2 by, ImGuiKeyChord mods = 0)
{
    if (mods)
        ctx->KeyDown(mods);
    ctx->MouseMoveToPos(ImVec2(from));
    ctx->MouseDown(ImGuiMouseButton_Left);
    ctx->MouseMoveToPos(ImVec2(from + by));
    ctx->MouseUp(ImGuiMouseButton_Left);
    if (mods)
        ctx->KeyUp(mods);
    ctx->Yield(2);
}

/// A point of the edit preview, in app coordinates, that lies in the viewport clear of the window \p dialog.
/**
    Tried at the middle of each side of the viewport in turn, which a dialog centered over it leaves clear.
*/
inline std::optional<float2> preview_point(ImGuiTestContext *ctx, const char *dialog)
{
    auto        *app    = hdrview();
    ImGuiWindow *window = ctx->WindowInfo(dialog).Window;
    for (const float2 t : {float2{0.1f, 0.5f}, float2{0.9f, 0.5f}, float2{0.5f, 0.1f}, float2{0.5f, 0.9f}})
    {
        const float2 vp = t * app->viewport_size(), at = app->app_pos_at_vp_pos(vp);
        const auto   uv = app->preview_uv(vp);
        if (uv && la::all(la::gequal(*uv, float2{0.f})) && la::all(la::lequal(*uv, float2{1.f})) && window &&
            !window->Rect().Contains(ImVec2(at)))
            return at;
    }
    return std::nullopt;
}

} // namespace hdrview_test
