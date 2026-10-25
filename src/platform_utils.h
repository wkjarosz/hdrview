#pragma once

#include <string>

bool        host_is_apple();
bool        host_is_safari();
const char *file_manager_name();
const char *reveal_in_file_manager_text();
void        show_in_file_manager(const char *filename);

#if !defined(__EMSCRIPTEN__)
#include <vector>

/// Sets up the native file dialogs once GLFW is, and returns why they cannot open, or an empty string when they can.
/**
    With NFDe's portal backend, which HDRView builds on Linux, the dialogs need an xdg-desktop-portal with a file
    chooser. A desktop may lack one, which would otherwise surface only as a failed dialog once the user tries to open
    or save.
*/
const std::string &init_file_dialogs();

/// What init_file_dialogs() returned.
const std::string &file_dialog_problem();

/// Why the native folder dialogs cannot open when the file dialogs can, or an empty string when they can too.
const std::string &folder_dialog_problem();

/// Undoes init_file_dialogs(), if it got as far as setting the dialogs up.
void quit_file_dialogs();

/// The files a native open dialog over \p glfw_window returns, none if it was canceled or failed.
/**
    The dialog offers files with the comma-separated \p extensions, given without their dots ("png,jpg"), under
    \p filter_name, and lets several be chosen when \p multiple.
*/
std::vector<std::string> open_file_paths(const char *title, const char *filter_name, const std::string &extensions,
                                         bool multiple, void *glfw_window);

/// The file a native save dialog over \p glfw_window returns, or an empty string if it was canceled or failed.
std::string save_file_path(const char *title, const std::string &default_name, const char *filter_name,
                           const std::string &extensions, void *glfw_window);

/// The folder a native folder dialog over \p glfw_window returns, or an empty string if it was canceled or failed.
std::string folder_path(const char *title, void *glfw_window);
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

/// Register the browser touch listeners that supply pinch-to-zoom, which no backend provides.
void install_touch_handlers();

/// Ask the browser to confirm before it navigates away while images are loaded.
/**
    The web build holds everything in memory, so leaving the page discards it. The GLFW port swallows most
    keys that would leave, but the browser-reserved ones (Ctrl/Cmd+W everywhere, Cmd+R in Safari) never reach
    the page at all, so the browser has to do the prompting.
*/
void install_navigation_guard();

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int hdrview_loadfile(const char *filename, const char *buffer, size_t buffer_size,
                                              bool should_select);
} // extern "C"

#endif
