/** \file test_gui_support.h
    \author Wojciech Jarosz

    Shared scaffolding for the GUI suite: waiting, loading, dragging over the viewport, and temporary files.
*/

#pragma once

#include "app.h"
#include "clipboard.h"
#include "edit/commands.h"
#include "image.h"
#include "imageio/image_loader.h"
#include "json.h"
#include "version.h"

#include "../test_log_capture.h"
#include "../test_zip.h"

#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#if !defined(HDRVIEW_GUI_TEST_IMAGE) || !defined(HDRVIEW_GUI_TEST_IMAGE_2)
#error "HDRVIEW_GUI_TEST_IMAGE and _2 must be defined by CMake to small fixture image paths"
#endif

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

/// Runs \p f on the main thread and waits for it, for work a test may not do on its own thread.
/**
    Anything that destroys an image belongs here: Image's destructor deletes its textures, and the GL context is
    current on the main thread alone, so the same call from the Test Engine's coroutine thread deletes them with no
    context and faults.
*/
inline void on_main_thread(ImGuiTestContext *ctx, std::function<void()> f)
{
    auto done = std::make_shared<std::atomic_bool>(false);
    hdrview()->post_to_main_thread(
        [f = std::move(f), done]
        {
            f();
            done->store(true);
        });
    hdrview()->wake_event_loop();
    wait_until(ctx, [done] { return done->load(); });
}

/// Closes everything without prompting and waits for the list to empty, so a test starts from a known state.
/**
    An image's destructor deletes its textures, which is a GL call, and the context is current on the main thread
    alone, so both the close and the release of every \p held reference the test still owns happen there. A test
    that keeps an image across this call must hand it over here, since once the list is closed the test holds the
    last reference and would otherwise destroy it on the Test Engine's coroutine thread.
*/
template <typename... Held> void reset_images(ImGuiTestContext *ctx, Held &...held)
{
    auto keep = std::make_shared<std::vector<ConstImagePtr>>();
    keep->reserve(sizeof...(held));
    (keep->push_back(std::move(held)), ...);
    // waiting on the close itself, not on an empty list, which an earlier close may already have left behind
    on_main_thread(ctx,
                   [keep]
                   {
                       hdrview()->close_all_images_immediately();
                       keep->clear();
                   });
    // and at 100%, which a reshaping edit's refit or a test's zoom would otherwise carry into the next image loaded
    hdrview()->set_zoom(1.f);
}

/// Starts from the small fixture alone, loaded \p copies times, whatever an earlier test left loaded or selected.
/**
    Edits are selection-only by default, so a selection left behind would narrow the next test's; the edit subject
    and both clipboards, HDRView's and the private stand-in for the system's, are cleared for the same reason.
*/
inline bool load_fixture(ImGuiTestContext *ctx, int copies = 1)
{
    reset_images(ctx);
    hdrview()->roi()          = Box2i{};
    hdrview()->edit_subject() = EditSubject{};
    hdrview()->set_clipboard(nullptr);
    use_private_clipboard() = {};
    IM_CHECK_SILENT_RETV(load_and_wait(ctx, std::vector<std::string>(copies, HDRVIEW_GUI_TEST_IMAGE_2)) == copies,
                         false);
    IM_CHECK_SILENT_RETV(hdrview()->current_image() != nullptr, false);
    return true;
}

/// Whether the window titled \p name, a dialog say, has closed, as a save dialog does only once it has written.
inline bool window_closed(ImGuiTestContext *ctx, const std::string &name)
{
    ctx->SetRef("");
    const std::string ref = "//" + escaped(name);
    return wait_until(ctx, [&] { return ctx->WindowInfo(ref.c_str(), ImGuiTestOpFlags_NoError).Window == nullptr; });
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

/// A session file's contents, showing the first of \p images against \p reference, with \p view's settings.
inline json session_manifest(std::initializer_list<json> images, int reference = -1, const json &view = json::object())
{
    return {{"type", "HDRView session"},
            {"version", fmt::format("{}.{}.{}", version_major(), version_minor(), version_patch())},
            {"images", std::vector<json>(images)},
            {"current", 0},
            {"reference", reference},
            {"blend_mode", "normal"},
            {"view", view}};
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

/// Puts the view's zoom and pan back as they were when it was made, however the test ends.
struct KeepView
{
    float  zoom = hdrview()->zoom();
    float2 at   = hdrview()->vp_pos_at_pixel(float2{0.f});
    ~KeepView()
    {
        hdrview()->set_zoom(zoom);
        hdrview()->reposition_pixel_to_vp_pos(at, float2{0.f});
    }
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
