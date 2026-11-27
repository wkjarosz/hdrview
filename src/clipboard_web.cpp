//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard_web.cpp
    \author Wojciech Jarosz

    The browser's clipboard, for the web build: see clipboard_platform.h.

    A page reads the clipboard two ways. After Ctrl/Cmd+V, the page's paste event delivers the image without asking
    the user; from a menu, the asynchronous navigator.clipboard.read() does, which may first ask permission. Either
    way the image arrives later, through hdrview_clipboard_delivered(), outside any frame.
*/

#include "clipboard_platform.h"

#include <emscripten/emscripten.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <utility>

using namespace std;

namespace
{

bool s_own_since_focus = false; ///< Whether this page copied last and has kept the focus since

/// A read waiting on the browser, which hdrview_clipboard_delivered() completes.
function<void(ClipboardRead)> s_waiting;
/// What the page's last paste event delivered, while no read was waiting for it.
optional<ClipboardFile> s_pasted;
optional<int>           s_pasted_frame;            ///< The frame s_pasted arrived in
bool                    s_paste_in_flight = false; ///< A paste event's file is on its way

/// How many frames a paste event's file waits for the Ctrl/Cmd+V shortcut it came with, which may arrive after it.
constexpr int k_paste_frames = 2;

// The paste event's image, and a blur, after which another application may copy next.
EM_JS(void, hdrview_clipboard_listen, (), {
    // hands a file, or a failed read's reason, to hdrview_clipboard_delivered()
    globalThis.hdrview_clipboard_deliver = function(type, buffer, error)
    {
        var bytes = new Uint8Array(buffer);
        var ptr   = Module._malloc(Math.max(1, bytes.length));
        Module.HEAPU8.set(bytes, ptr);
        Module.ccall('hdrview_clipboard_delivered', null, [ 'string', 'number', 'number', 'string' ],
                     [ type, ptr, bytes.length, error || '' ]);
        Module._free(ptr);
    };

    document.addEventListener('paste', function(e) {
        var items = e.clipboardData ? e.clipboardData.items : [];
        for (var i = 0; i < items.length; ++i)
        {
            var item = items[i];
            if (item.kind != 'file' || !item.type.startsWith('image/'))
                continue;
            var file = item.getAsFile();
            if (!file)
                continue;
            Module.ccall('hdrview_clipboard_paste_started', null, [], []);
            file.arrayBuffer().then(function(buffer) { hdrview_clipboard_deliver(file.type, buffer); });
            return;
        }
    });
    window.addEventListener('blur', function() { Module.ccall('hdrview_clipboard_away', null, [], []); });

    // Safari allows clipboard access only while it handles a click or key press, and the frame that acts on the press
    // comes too late. During each click and key press, the GLFW port requests an animation frame callback, which
    // Safari counts as part of the press, and runs the actions deferred to it there, as it does for its own text
    // clipboard. The extra request here runs an action queued with no press pending, which Chrome and Firefox allow.
    // The port requests one only for mouse and key events, and a tap sends no mouse events, since the port cancels
    // the touch's default handling; so a lifted finger requests one too.
    if (typeof GLFW3 != 'undefined')
        document.addEventListener('touchend', function() { requestAnimationFrame(GLFW3.executeDeferredActions); });
    globalThis.hdrview_clipboard_soon = function(action)
    {
        if (typeof GLFW3 == 'undefined')
            return action();
        GLFW3.deferAction(action);
        requestAnimationFrame(GLFW3.executeDeferredActions);
    };
});

// only Chromium has navigator.userAgentData, and only its clipboard writes a PQ PNG unchanged
EM_JS(int, hdrview_clipboard_keeps_hdr, (), { return navigator.userAgentData !== undefined ? 1 : 0; });

// a failed read passes its reason, which HDRView logs as an error
EM_JS(void, hdrview_clipboard_read, (), {
    var deliver = hdrview_clipboard_deliver;
    if (!navigator.clipboard || !navigator.clipboard.read)
    {
        deliver('', new ArrayBuffer(0), 'This browser cannot read images from the clipboard.');
        return;
    }
    hdrview_clipboard_soon(function() {
        navigator.clipboard.read()
            .then(function(items) {
                for (var i = 0; i < items.length; ++i)
                {
                    var type = items[i].types.find(function(t) { return t.startsWith('image/'); });
                    if (type)
                        return items[i].getType(type).then(function(blob) {
                            return blob.arrayBuffer().then(function(buffer) { deliver(type, buffer); });
                        });
                }
                deliver('', new ArrayBuffer(0));
            })
            .catch(function(error) { deliver('', new ArrayBuffer(0), 'Could not read the clipboard: ' + error); });
    });
});

EM_JS_DEPS(hdrview_clipboard, "$UTF8ToString");
EM_JS(void, hdrview_clipboard_write, (const char *type, const char *bytes, size_t length), {
    var mime  = UTF8ToString(type);
    var error = function(message) { Module.ccall('hdrview_clipboard_error', null, [ 'string' ], [ message ]); };
    if (!navigator.clipboard || !navigator.clipboard.write || typeof ClipboardItem == 'undefined')
    {
        error('This browser cannot write images to the clipboard.');
        return;
    }
    // copied out of the module's memory now, since the write completes after this returns
    var blob   = new Blob([Module.HEAPU8.slice(bytes, bytes + length)], {type : mime});
    var item   = {};
    item[mime] = blob;
    hdrview_clipboard_soon(function() {
        navigator.clipboard.write([new ClipboardItem(item)]).catch(function(e) {
            error('Could not write to the clipboard: ' + e);
        });
    });
});

} // namespace

// called from the JavaScript above
extern "C"
{
    EMSCRIPTEN_KEEPALIVE void hdrview_clipboard_paste_started() { s_paste_in_flight = true; }

    EMSCRIPTEN_KEEPALIVE void hdrview_clipboard_delivered(const char *type, const char *bytes, size_t length,
                                                          const char *error)
    {
        s_paste_in_flight = false;
        ClipboardRead read;
        if (bytes && length)
            read.file = ClipboardFile{type ? type : "", string(bytes, length)};
        if (error && *error)
        {
            spdlog::error("{}", error);
            read.failed = true;
        }

        if (s_waiting)
        {
            auto done = std::move(s_waiting);
            s_waiting = nullptr;
            done(std::move(read));
        }
        else
        {
            // a paste event ahead of the Ctrl/Cmd+V shortcut it came with, or of no paste at all
            s_pasted       = std::move(read.file);
            s_pasted_frame = ImGui::GetFrameCount();
        }
    }

    EMSCRIPTEN_KEEPALIVE void hdrview_clipboard_error(const char *message) { spdlog::error("{}", message); }

    EMSCRIPTEN_KEEPALIVE void hdrview_clipboard_away() { s_own_since_focus = false; }
}

namespace clipboard_platform
{

void start() { hdrview_clipboard_listen(); }

bool write(const vector<ClipboardFile> &files)
{
    // a page may write only a PNG
    const ClipboardFile *file = single_file(files);
    if (!file)
        return false;
    hdrview_clipboard_write(file->type.c_str(), file->bytes.data(), file->bytes.size());
    s_own_since_focus = true;
    return true;
}

void read(function<void(ClipboardRead)> done)
{
    // a Ctrl/Cmd+V's own paste event, arriving or just arrived, which needs no permission
    if (s_paste_in_flight)
    {
        s_waiting = std::move(done);
        return;
    }
    if (s_pasted_frame && ImGui::GetFrameCount() - *s_pasted_frame <= k_paste_frames)
    {
        s_pasted_frame.reset();
        return done({std::exchange(s_pasted, nullopt)});
    }
    s_waiting = std::move(done);
    hdrview_clipboard_read();
}

// a page cannot ask without reading, which may ask the user's permission
bool can_ask() { return false; }
bool holds_image() { return true; }

bool carries(const string &type) { return type == "image/png"; }
bool keeps_hdr() { return hdrview_clipboard_keeps_hdr(); }

optional<bool> own_copy(const ClipboardFile &) { return s_own_since_focus; }

} // namespace clipboard_platform
