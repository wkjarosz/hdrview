#pragma once

bool        host_is_apple();
bool        host_is_safari();
const char *file_manager_name();
const char *reveal_in_file_manager_text();
void        show_in_file_manager(const char *filename);

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

//! Register the browser touch listeners that supply pinch-to-zoom, which no backend provides.
void install_touch_handlers();

//! Ask the browser to confirm before it navigates away while images are loaded.
/*!
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
