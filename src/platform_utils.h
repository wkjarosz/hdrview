#pragma once

#include <string>

bool        host_is_apple();
bool        host_is_safari();
const char *file_manager_name();
const char *reveal_in_file_manager_text();
void        show_in_file_manager(const char *filename);

#if !defined(__EMSCRIPTEN__)
#include <nfd.hpp>

/// Sets up the native file dialogs once GLFW is, and returns why they cannot open, or an empty string when they can.
/**
    On Linux the dialogs need an xdg-desktop-portal with a file chooser, which a desktop may lack, and which would
    otherwise surface only as a failed dialog once the user tries to open or save.
*/
std::string init_file_dialogs();

/// Undoes init_file_dialogs(), if it got as far as setting the dialogs up.
void quit_file_dialogs();

/// The handle that makes a native file dialog a child of the GLFW window \p glfw_window.
nfdwindowhandle_t file_dialog_parent(void *glfw_window);
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
