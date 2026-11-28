//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard_wayland.cpp
    \author Wojciech Jarosz

    See clipboard_wayland.h. Everything here runs on the main thread, from GLFW's own event dispatch, apart from
    the threads that hand a copy's bytes to whoever asked for them.
*/

#include "clipboard_wayland.h"

#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <utility>

using namespace std;

namespace
{

struct State
{
    wl_display             *display  = nullptr;
    wl_seat                *seat     = nullptr;
    wl_data_device_manager *manager  = nullptr;
    wl_data_device         *device   = nullptr; ///< Non-null once start() has succeeded
    wl_keyboard            *keyboard = nullptr;
    bool                    started  = false;

    uint32_t serial = 0; ///< The last serial an input event carried, which set_selection must cite

    /// HDRView's own copy, while the clipboard still holds it, and the source offering it.
    wl_data_source       *source = nullptr;
    vector<ClipboardFile> offered;

    wl_data_offer *offer = nullptr; ///< Another application's selection, or none
    vector<string> offer_types;     ///< What that selection says it holds
};

State &state()
{
    static State s;
    return s;
}

/// Forget another application's selection, which its offer stands for.
void drop_offer()
{
    auto &s = state();
    if (s.offer)
        wl_data_offer_destroy(s.offer);
    s.offer = nullptr;
    s.offer_types.clear();
}

//
// another application's selection
//

void offer_handle_offer(void *, wl_data_offer *offer, const char *mime_type)
{
    auto &s = state();
    if (offer == s.offer && mime_type)
        s.offer_types.emplace_back(mime_type);
}

void offer_handle_source_actions(void *, wl_data_offer *, uint32_t) {}
void offer_handle_action(void *, wl_data_offer *, uint32_t) {}

const wl_data_offer_listener k_offer_listener = {offer_handle_offer, offer_handle_source_actions,
                                                 offer_handle_action};

void device_handle_data_offer(void *, wl_data_device *, wl_data_offer *offer)
{
    // the types follow this event and the selection event follows those, so listening now is the only chance to
    // hear what the offer holds
    drop_offer();
    state().offer = offer;
    wl_data_offer_add_listener(offer, &k_offer_listener, nullptr);
}

void device_handle_selection(void *, wl_data_device *, wl_data_offer *offer)
{
    // A nil offer means the clipboard was cleared, or that HDRView owns it, since a client hears its own
    // selection as none. One this never saw announced is one whose types are unknown.
    if (offer != state().offer)
        drop_offer();
}

void device_handle_enter(void *, wl_data_device *, uint32_t, wl_surface *, wl_fixed_t, wl_fixed_t, wl_data_offer *) {}
void device_handle_leave(void *, wl_data_device *) {}
void device_handle_motion(void *, wl_data_device *, uint32_t, wl_fixed_t, wl_fixed_t) {}
void device_handle_drop(void *, wl_data_device *) {}

const wl_data_device_listener k_device_listener = {device_handle_data_offer, device_handle_enter,
                                                   device_handle_leave,      device_handle_motion,
                                                   device_handle_drop,       device_handle_selection};

//
// HDRView's own copy
//

void source_handle_send(void *, wl_data_source *, const char *mime_type, int32_t fd)
{
    for (const auto &file : state().offered)
        if (file.type == mime_type)
        {
            // On a thread with its own copy of the bytes: a receiver reads at its own pace, and may still be
            // reading once another application has taken the clipboard.
            thread{[fd, bytes = file.bytes]
                   {
                       // so a receiver that gives up early fails this write rather than raising SIGPIPE
                       sigset_t pipe_only;
                       sigemptyset(&pipe_only);
                       sigaddset(&pipe_only, SIGPIPE);
                       pthread_sigmask(SIG_BLOCK, &pipe_only, nullptr);

                       for (size_t sent = 0; sent < bytes.size();)
                       {
                           const ssize_t n = write(fd, bytes.data() + sent, bytes.size() - sent);
                           if (n > 0)
                               sent += size_t(n);
                           else if (n < 0 && errno == EINTR)
                               continue;
                           else
                               break;
                       }
                       close(fd);
                   }}
                .detach();
            return;
        }
    close(fd); // a type HDRView never offered, which the receiver sees as nothing
}

void source_handle_cancelled(void *, wl_data_source *source)
{
    // another application took the clipboard, so this copy is no longer on it
    auto &s = state();
    if (s.source == source)
    {
        s.source = nullptr;
        s.offered.clear();
    }
    wl_data_source_destroy(source);
}

void source_handle_target(void *, wl_data_source *, const char *) {}
void source_handle_dnd_drop_performed(void *, wl_data_source *) {}
void source_handle_dnd_finished(void *, wl_data_source *) {}
void source_handle_action(void *, wl_data_source *, uint32_t) {}

const wl_data_source_listener k_source_listener = {source_handle_target,       source_handle_send,
                                                   source_handle_cancelled,    source_handle_dnd_drop_performed,
                                                   source_handle_dnd_finished, source_handle_action};

//
// the seat, for the serial set_selection has to cite
//

void keyboard_handle_keymap(void *, wl_keyboard *, uint32_t, int32_t fd, uint32_t)
{
    close(fd); // HDRView reads the keymap through GLFW; this one is closed so the compositor's fd is not leaked
}
void keyboard_handle_enter(void *, wl_keyboard *, uint32_t serial, wl_surface *, wl_array *)
{
    state().serial = serial;
}
void keyboard_handle_leave(void *, wl_keyboard *, uint32_t serial, wl_surface *) { state().serial = serial; }
void keyboard_handle_key(void *, wl_keyboard *, uint32_t serial, uint32_t, uint32_t, uint32_t)
{
    state().serial = serial;
}
void keyboard_handle_modifiers(void *, wl_keyboard *, uint32_t serial, uint32_t, uint32_t, uint32_t, uint32_t)
{
    state().serial = serial;
}
void keyboard_handle_repeat_info(void *, wl_keyboard *, int32_t, int32_t) {}

const wl_keyboard_listener k_keyboard_listener = {keyboard_handle_keymap,    keyboard_handle_enter,
                                                  keyboard_handle_leave,     keyboard_handle_key,
                                                  keyboard_handle_modifiers, keyboard_handle_repeat_info};

void seat_handle_capabilities(void *, wl_seat *seat, uint32_t capabilities)
{
    auto &s = state();
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !s.keyboard)
    {
        s.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(s.keyboard, &k_keyboard_listener, nullptr);
    }
}

void seat_handle_name(void *, wl_seat *, const char *) {}

const wl_seat_listener k_seat_listener = {seat_handle_capabilities, seat_handle_name};

void registry_handle_global(void *, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto &s = state();
    if (strcmp(interface, wl_data_device_manager_interface.name) == 0 && !s.manager)
        s.manager = static_cast<wl_data_device_manager *>(
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, min(version, 3u)));
    else if (strcmp(interface, wl_seat_interface.name) == 0 && !s.seat)
    {
        s.seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, min(version, 5u)));
        wl_seat_add_listener(s.seat, &k_seat_listener, nullptr);
    }
}

void registry_handle_global_remove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener k_registry_listener = {registry_handle_global, registry_handle_global_remove};

} // namespace

namespace clipboard_wayland
{

bool start()
{
    auto &s = state();
    if (s.started)
        return s.device != nullptr;
    s.started = true;

    s.display = glfwGetWaylandDisplay();
    if (!s.display)
        return false;

    // A second binding of each global, alongside GLFW's own, which the protocol allows and which keeps this
    // clipboard independent of what the fork does with its own. Neither is torn down: both live as long as the
    // display.
    auto *registry = wl_display_get_registry(s.display);
    wl_registry_add_listener(registry, &k_registry_listener, nullptr);
    wl_display_roundtrip(s.display); // the globals
    wl_display_roundtrip(s.display); // and the seat's capabilities, which follow its binding

    if (!s.manager || !s.seat)
    {
        spdlog::warn("Wayland: no clipboard, since the compositor offers no {}",
                     s.manager ? "seat" : "data device manager");
        return false;
    }

    s.device = wl_data_device_manager_get_data_device(s.manager, s.seat);
    wl_data_device_add_listener(s.device, &k_device_listener, nullptr);
    spdlog::debug("Wayland: HDRView's own clipboard is ready");
    return true;
}

bool write(const vector<ClipboardFile> &files)
{
    auto &s = state();
    if (!start() || files.empty())
        return false;

    // set_selection must cite the serial of an event HDRView received, which a copy, made while it has the
    // keyboard, always has behind it
    if (!s.serial)
    {
        spdlog::warn("Wayland: cannot copy before the window has had the keyboard");
        return false;
    }

    auto *source = wl_data_device_manager_create_data_source(s.manager);
    if (!source)
        return false;
    wl_data_source_add_listener(source, &k_source_listener, nullptr);
    for (const auto &file : files) wl_data_source_offer(source, file.type.c_str());
    wl_data_device_set_selection(s.device, source, s.serial);

    // the source this replaces is left for its cancelled event to destroy, which the compositor sends as the
    // selection changes hands
    s.source  = source;
    s.offered = files;
    wl_display_flush(s.display);

    string types;
    for (const auto &file : files) types += (types.empty() ? "" : ", ") + file.type;
    spdlog::debug("Wayland: the clipboard now offers {}", types);
    return true;
}

vector<string> offered_types()
{
    auto &s = state();
    if (!start())
        return {};
    if (!s.source)
        return s.offer_types;

    vector<string> types;
    for (const auto &file : s.offered) types.push_back(file.type);
    return types;
}

optional<ClipboardFile> read(const string &type)
{
    auto &s = state();
    if (!start())
        return nullopt;

    // HDRView's own copy needs no round trip through the compositor
    if (s.source)
    {
        for (const auto &file : s.offered)
            if (file.type == type)
                return file;
        return nullopt;
    }
    if (!s.offer)
        return nullopt;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) == -1)
    {
        spdlog::error("Wayland: could not make a pipe to read the clipboard: {}", strerror(errno));
        return nullopt;
    }
    wl_data_offer_receive(s.offer, type.c_str(), fds[1]);
    wl_display_flush(s.display);
    close(fds[1]);

    // Blocking on the paste that asked for it, as every other platform's read does. Only a type the selection
    // named is asked for, so the source has bytes to send and closes the pipe once it has.
    string bytes;
    char   buffer[64 * 1024];
    for (;;)
    {
        const ssize_t n = ::read(fds[0], buffer, sizeof buffer);
        if (n == 0)
            break;
        if (n > 0)
            bytes.append(buffer, size_t(n));
        else if (errno != EINTR)
        {
            spdlog::error("Wayland: could not read the clipboard: {}", strerror(errno));
            break;
        }
    }
    close(fds[0]);

    if (bytes.empty())
        return nullopt;
    return ClipboardFile{type, std::move(bytes)};
}

bool owns_selection() { return state().source != nullptr; }

} // namespace clipboard_wayland
