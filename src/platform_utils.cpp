
#include "platform_utils.h"

#include <cstdlib>
#include <string>

#ifdef __EMSCRIPTEN__
#include "app.h"
#include "common.h"
#include "imgui.h"
#include <emscripten_browser_clipboard.h>

EM_JS(bool, isSafari, (), {
    var is_safari = /^((?!chrome|android).)*safari/i.test(navigator.userAgent);
    return is_safari;
});
EM_JS(bool, isAppleDevice, (), {
    const ua = navigator.userAgent;
    return (ua.includes("Macintosh") || ua.includes("iPad") || ua.includes("iPhone") || ua.includes("iPod"));
});

static std::string g_clipboard_content; // this stores the content for our internal clipboard

static char const *get_clipboard_for_imgui(ImGuiContext *user_data [[maybe_unused]])
{
    /// Callback for imgui, to return clipboard content
    spdlog::info("ImGui requested clipboard content, returning '{}'", g_clipboard_content);
    return g_clipboard_content.c_str();
}

static void set_clipboard_from_imgui(ImGuiContext *user_data [[maybe_unused]], char const *text)
{
    /// Callback for imgui, to set clipboard content
    g_clipboard_content = text;
    spdlog::info("ImGui setting clipboard content to '{}'", g_clipboard_content);
    emscripten_browser_clipboard::copy(g_clipboard_content); // send clipboard data to the browser
}
#endif

void setup_imgui_clipboard()
{
#ifdef __EMSCRIPTEN__
    // spdlog::info("Setting up paste callback");
    // emscripten_browser_clipboard::paste(
    //     [](std::string &&paste_data, void *)
    //     {
    //         /// Callback to handle clipboard paste from browser
    //         spdlog::info("Browser pasted: '{}'", paste_data);
    //         g_clipboard_content = std::move(paste_data);
    //     });
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = set_clipboard_from_imgui;
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = get_clipboard_for_imgui;
#endif
}

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
    system(command.c_str());
#elif defined(__APPLE__)
    // Use open command with -R to reveal the file in Finder
    std::string command = "open -R \"" + std::string(filename) + "\"";
    system(command.c_str());
#elif defined(__linux__)
    // Try to use xdg-open to open the containing folder
    std::string filepath(filename);
    size_t      last_slash = filepath.find_last_of("/\\");
    std::string folder     = (last_slash != std::string::npos) ? filepath.substr(0, last_slash) : ".";
    std::string command    = "xdg-open \"" + folder + "\"";
    system(command.c_str());
#else
    // Unsupported platform
#endif
}

#if defined(__EMSCRIPTEN__)
//------------------------------------------------------------------------------
//  Javascript interface functions
//
extern "C"
{
    EMSCRIPTEN_KEEPALIVE int hdrview_loadfile(const char *filename, const char *buffer, size_t buffer_size,
                                              bool should_select)
    {
        auto [size, unit] = human_readable_size(buffer_size);
        spdlog::info("User dropped a {:.0f} {} file with filename '{}'", size, unit, filename);

        if (!buffer || buffer_size == 0)
        {
            spdlog::warn("Empty file, skipping...");
            return 1;
        }
        else
        {
            hdrview()->load_image(filename, {buffer, buffer_size}, should_select);
            return 0;
        }
    }

} // extern "C"

#endif