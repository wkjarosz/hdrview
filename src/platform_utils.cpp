#include "platform_utils.h"

#include <cstdlib>
#include <spdlog/spdlog.h>
#include <string>

#ifdef __EMSCRIPTEN__
#include "app.h"

#include <emscripten/html5.h>

EM_JS(bool, isSafari, (), {
    var is_safari = /^((?!chrome|android).)*safari/i.test(navigator.userAgent);
    return is_safari;
});
EM_JS(bool, isAppleDevice, (), {
    const ua = navigator.userAgent;
    return (ua.includes("Macintosh") || ua.includes("iPad") || ua.includes("iPhone") || ua.includes("iPod"));
});

#endif

bool host_is_apple()
{
#if defined(__EMSCRIPTEN__)
    return isAppleDevice();
#elif defined(__APPLE__)
    return true;
#else
    return false;
#endif
}
bool host_is_safari()
{
#if defined(__EMSCRIPTEN__)
    return isSafari();
#else
    return false;
#endif
}

const char *file_manager_name()
{
#if defined(_WIN32)
    return "Windows Explorer";
#elif defined(__APPLE__)
    return "Finder";
#elif defined(__linux__)
    return "File Manager";
#else
    return "Unknown";
#endif
}

const char *reveal_in_file_manager_text()
{
#if defined(_WIN32)
    return "Reveal in Windows Explorer";
#elif defined(__APPLE__)
    return "Reveal in Finder";
#elif defined(__linux__)
    return "Reveal in File Manager";
#else
    return "Reveal in unknown File Manager";
#endif
}

void show_in_file_manager(const char *filename)
{
#if defined(_WIN32)
    // Use explorer.exe to select the file
    std::string command = "explorer /select,\"" + std::string(filename) + "\"";
    if (int e = system(command.c_str()); e != 0)
        spdlog::warn("Failed to launch file manager with command: {}. Exit code {}", command, e);
#elif defined(__APPLE__)
    // Use open command with -R to reveal the file in Finder
    std::string command = "open -R \"" + std::string(filename) + "\"";
    if (int e = system(command.c_str()); e != 0)
        spdlog::warn("Failed to launch file manager with command: {}. Exit code {}", command, e);
#elif defined(__linux__)
    // Try to use xdg-open to open the containing folder
    std::string filepath(filename);
    size_t      last_slash = filepath.find_last_of("/\\");
    std::string folder     = (last_slash != std::string::npos) ? filepath.substr(0, last_slash) : ".";
    std::string command    = "xdg-open \"" + folder + "\"";
    if (int e = system(command.c_str()); e != 0)
        spdlog::warn("Failed to launch file manager with command: {}. Exit code {}", command, e);
#else
    // Unsupported platform
#endif
}

#if defined(__EMSCRIPTEN__)
//------------------------------------------------------------------------------
//  Javascript interface functions
//
//! Two-finger pan and pinch-to-zoom, read straight from the browser's touch events.
/*!
    Neither backend supplies it. GLFW has no gesture API on any platform, and the Emscripten port
    hello_imgui uses tracks a single touch point, synthesizing mouse events from it and discarding the
    rest -- so the second finger never reaches the application otherwise.

    Registering alongside the port's own listeners leaves that synthesis intact: one finger still pans
    through the ordinary mouse path. The gesture is reduced here to how the first two fingers' separation
    and midpoint changed; what that does to the viewport is HDRViewApp::touch_gesture()'s business.

    The deltas are taken per event rather than per frame because several touchmoves can arrive between
    two frames, and every one of them is part of the same continuous motion.
*/
static EM_BOOL on_touch(int event_type, const EmscriptenTouchEvent *event, void *)
{
    // The first two fingers are the gesture; a third changes nothing.
    static float  previous_distance = 0.f;
    static float2 previous_midpoint{0.f};

    const bool ending = event_type == EMSCRIPTEN_EVENT_TOUCHEND || event_type == EMSCRIPTEN_EVENT_TOUCHCANCEL;

    // touches[] carries the fingers that just left alongside those still down, so the ones that ended
    // have to be skipped to find what is actually on the glass -- including when picking the first two,
    // which a lifted finger would otherwise be one of.
    int down[2]      = {0, 0};
    int touches_down = 0;
    for (int i = 0; i < event->numTouches; ++i)
    {
        if (ending && event->touches[i].isChanged)
            continue;
        if (touches_down < 2)
            down[touches_down] = i;
        ++touches_down;
    }

    const bool pinching = touches_down >= 2;

    float  scale = 1.f;
    float2 from{0.f}, to{0.f};
    if (pinching)
    {
        const float2 a{(float)event->touches[down[0]].targetX, (float)event->touches[down[0]].targetY};
        const float2 b{(float)event->touches[down[1]].targetX, (float)event->touches[down[1]].targetY};
        const float  distance = length(b - a);
        const float2 midpoint = 0.5f * (a + b);

        // The first event of a pinch has nothing to compare against, so it only sets the baseline.
        if (previous_distance > 0.f && distance > 0.f)
        {
            scale = distance / previous_distance;
            from  = previous_midpoint;
            to    = midpoint;
        }
        previous_distance = distance;
        previous_midpoint = midpoint;
    }
    else
        previous_distance = 0.f;

    hdrview()->touch_gesture(touches_down, scale, from, to);

    // Claim the event only while pinching, so one finger still reaches the port's mouse synthesis.
    return pinching ? EM_TRUE : EM_FALSE;
}

void install_touch_handlers()
{
    // The canvas hello_imgui draws into (id="canvas" in shell.emscripten.html).
    // EMSCRIPTEN_EVENT_TARGET_WINDOW would also catch touches beginning on the surrounding page.
    const char *canvas = "#canvas";
    emscripten_set_touchstart_callback(canvas, nullptr, EM_FALSE, on_touch);
    emscripten_set_touchmove_callback(canvas, nullptr, EM_FALSE, on_touch);
    emscripten_set_touchend_callback(canvas, nullptr, EM_FALSE, on_touch);
    emscripten_set_touchcancel_callback(canvas, nullptr, EM_FALSE, on_touch);
}

// A non-empty return asks the browser to confirm; it shows its own wording, not this text.
static const char *on_before_unload(int, const void *, void *)
{
    return hdrview()->num_images() > 0 ? "Images are open and are not saved anywhere." : nullptr;
}

void install_navigation_guard() { emscripten_set_beforeunload_callback(nullptr, on_before_unload); }

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int hdrview_loadfile(const char *filename, const char *buffer, size_t buffer_size,
                                              bool should_select)
    {
        spdlog::info("User dropped a {:.0h} file with filename '{}'", human_readible{buffer_size}, filename);

        if (!buffer || buffer_size == 0)
        {
            spdlog::warn("Empty file, skipping...");
            return 1;
        }
        else
        {
            hdrview()->load_image(filename, string_view{buffer, buffer_size}, should_select);
            return 0;
        }
    }

} // extern "C"

#endif