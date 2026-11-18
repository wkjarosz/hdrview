#pragma once

#include <functional>
#include <string>

bool        host_is_apple();
bool        host_is_safari();
const char *file_manager_name();
const char *reveal_in_file_manager_text();
void        show_in_file_manager(const char *filename);

/// What init_file_dialogs() returned, and always an empty string in the web build, which has no native dialogs.
const std::string &file_dialog_problem();

/// Why the native folder dialogs cannot open when the file dialogs can, or an empty string when they can too.
const std::string &folder_dialog_problem();

#if !defined(__EMSCRIPTEN__)
#include <vector>

/// Sets up the native file dialogs once GLFW is, and returns why they cannot open, or an empty string when they can.
/**
    With NFDe's portal backend, which HDRView builds on Linux, the dialogs need an xdg-desktop-portal with a file
    chooser. A desktop may lack one, which would otherwise surface only as a failed dialog once the user tries to open
    or save.
*/
const std::string &init_file_dialogs();

/// Undoes init_file_dialogs(), if it got as far as setting the dialogs up.
void quit_file_dialogs();

/// A choice of file type in a native dialog: its name, and its comma-separated extensions without dots ("png,jpg").
struct FileFilter
{ std::string name, extensions; };

/// The files a native open dialog over \p glfw_window returns, none if it was canceled or failed.
/**
    The dialog offers \p filters, the first chosen to begin with, and lets several files be chosen when \p multiple.
*/
std::vector<std::string> open_file_paths(const char *title, const std::vector<FileFilter> &filters, bool multiple,
                                         void *glfw_window);

/// The file a native save dialog over \p glfw_window returns, or an empty string if it was canceled or failed.
std::string save_file_path(const char *title, const std::string &default_name, const std::vector<FileFilter> &filters,
                           void *glfw_window);

/// The folder a native folder dialog over \p glfw_window returns, or an empty string if it was canceled or failed.
std::string folder_path(const char *title, void *glfw_window);
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

/// Register the browser touch listeners that supply pinch-to-zoom, which no backend provides.
void install_touch_handlers();

/// Keep the settings file at \p path in the browser's storage, saving it, with \p save_app_settings, as the page hides.
/**
    A web page's files live in memory, gone when it closes, and it never exits, when a native build saves. So this
    restores the file from the browser's storage now, before Hello ImGui reads it, and whenever the page is hidden,
    the last moment a browser reliably reports, saves what Hello ImGui saves on exit, then calls
    \p save_app_settings, then copies the file back. The storage is localStorage, which is synchronous, so the save
    finishes as the page goes; each folder of a site keeps its own copy, so its development and release builds do
    not share one.
*/
void keep_settings_in_browser(const std::string &path, std::function<void()> save_app_settings);
/// Remove the copy keep_settings_in_browser() keeps and reload the page, which then starts from the defaults.
void forget_settings_in_browser();

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
