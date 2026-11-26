//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "clipboard.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include <clip.h>
#endif

#if defined(_WIN32)
#include <cstdlib>
#include <qoi.h>
#endif

#if defined(__linux__)
// the GLFW fork's clipboard for data other than text, which clip does not reach on Wayland
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#endif

using namespace std;

using ReadDone = function<void(ClipboardRead)>;

namespace
{

/// One of the encodings set_clipboard_files() last wrote, to recognize it when it is read back.
struct OwnFile
{
    string type;
    size_t size = 0;
    size_t hash = 0;
};
vector<OwnFile> s_own;

size_t hash_bytes(const string &bytes) { return std::hash<string_view>{}(string_view{bytes}); }

bool                  s_use_private = false;
vector<ClipboardFile> s_private;

#if defined(__EMSCRIPTEN__) || defined(__linux__)
/// The PNG among \p files, for a clipboard that holds one file per copy, or else the first.
const ClipboardFile *single_file(const vector<ClipboardFile> &files)
{
    for (const auto &f : files)
        if (f.type == "image/png")
            return &f;
    return files.empty() ? nullptr : &files.front();
}
#endif

/// The file among \p files that a read takes: a TIFF, since it can hold floats, then a PNG, then any other.
optional<ClipboardFile> preferred_file(const vector<ClipboardFile> &files)
{
    for (const char *type : {"image/tiff", "image/png"})
        for (const auto &f : files)
            if (f.type == type && !f.bytes.empty())
                return f;
    for (const auto &f : files)
        if (!f.bytes.empty())
            return f;
    return nullopt;
}

bool s_holds_image = false; ///< See clipboard_holds_image()

#if defined(__EMSCRIPTEN__)

bool s_own_since_focus = false; ///< Whether this page copied last and has kept the focus since

/// A read waiting on the browser, which hdrview_clipboard_delivered() completes.
ReadDone s_waiting;
/// What the page's last paste event delivered, while no read was waiting for it.
optional<ClipboardFile> s_pasted;
int                     s_pasted_frame    = -1000; ///< The frame s_pasted arrived in
bool                    s_paste_in_flight = false; ///< A paste event's file is on its way

// After Ctrl/Cmd+V, the page's paste event delivers the clipboard's image without the permission prompt that the
// browser's asynchronous read shows. A blur means another application may copy next.
EM_JS(void, hdrview_clipboard_listen, (), {
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
            file.arrayBuffer().then(function(buffer) {
                var bytes = new Uint8Array(buffer);
                var ptr   = Module._malloc(Math.max(1, bytes.length));
                Module.HEAPU8.set(bytes, ptr);
                Module.ccall('hdrview_clipboard_delivered', null, [ 'string', 'number', 'number', 'string' ],
                             [ file.type, ptr, bytes.length, '' ]);
                Module._free(ptr);
            });
            return;
        }
    });
    window.addEventListener('blur', function() { Module.ccall('hdrview_clipboard_away', null, [], []); });

    // Safari allows clipboard access only while it handles a click or key press, and the frame that acts on the press
    // comes too late. During each click and key press, the GLFW port requests an animation frame callback, which
    // Safari counts as part of the press, and runs the actions deferred to it there, as it does for its own text
    // clipboard. The extra request here runs an action queued with no press pending, which Chrome and Firefox allow.
    globalThis.hdrview_clipboard_soon = function(action) {
        if (typeof GLFW3 == 'undefined')
            return action();
        GLFW3.deferAction(action);
        requestAnimationFrame(GLFW3.executeDeferredActions);
    };
});

// only Chromium has navigator.userAgentData, and only its clipboard writes a PQ PNG unchanged
EM_JS(int, hdrview_clipboard_keeps_hdr, (), { return navigator.userAgentData !== undefined ? 1 : 0; });

EM_JS(void, hdrview_clipboard_read, (), {
    // a failed read passes its reason too, which HDRView logs as an error
    function deliver(type, buffer, error)
    {
        var bytes = new Uint8Array(buffer);
        var ptr   = Module._malloc(Math.max(1, bytes.length));
        Module.HEAPU8.set(bytes, ptr);
        Module.ccall('hdrview_clipboard_delivered', null, [ 'string', 'number', 'number', 'string' ],
                     [ type, ptr, bytes.length, error || '' ]);
        Module._free(ptr);
    }
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

#else

/// The clipboard formats HDRView reads, in the order it tries them, each as a MIME type and the platform's name.
struct Format
{
    const char *mime;
    const char *native;
};
// TIFF first where the platform names one, since it can hold floats, where PNG stops at 16-bit integers
#if defined(__APPLE__)
const Format k_formats[] = {{"image/tiff", "public.tiff"}, {"image/png", "public.png"}};
#elif defined(_WIN32)
// the name every Windows application registers for PNG data; TIFF has only a predefined format, which clip cannot
// address by name
const Format k_formats[] = {{"image/png", "PNG"}};
#else
const Format k_formats[] = {{"image/tiff", "image/tiff"}, {"image/png", "image/png"}};
#endif

const char *native_name(const string &mime)
{
    for (const auto &f : k_formats)
        if (mime == f.mime)
            return f.native;
    return nullptr;
}

#if defined(_WIN32)
/// A bitmap that clip decoded, as a QOI file, which HDRView reads losslessly like any other.
optional<ClipboardFile> bitmap_as_qoi(const clip::image &image)
{
    const clip::image_spec &s = image.spec();
    if (s.width == 0 || s.height == 0 || s.bits_per_pixel % 8 != 0 || s.bits_per_pixel > 32)
        return nullopt;

    const size_t bytes_per_pixel = s.bits_per_pixel / 8;
    // a channel the bitmap lacks is 0 for color and opaque for alpha
    auto channel = [](uint32_t px, unsigned long mask, unsigned long shift, uint8_t missing) -> uint8_t
    {
        const uint32_t max = uint32_t(mask >> shift);
        return max ? uint8_t(uint64_t((px & mask) >> shift) * 255 / max) : missing;
    };

    vector<uint8_t> rgba(size_t(s.width) * s.height * 4);
    for (unsigned long y = 0; y < s.height; ++y)
    {
        const char *row = image.data() + y * s.bytes_per_row;
        for (unsigned long x = 0; x < s.width; ++x)
        {
            uint32_t px = 0;
            memcpy(&px, row + x * bytes_per_pixel, bytes_per_pixel);
            uint8_t *out = &rgba[(size_t(y) * s.width + x) * 4];
            out[0]       = channel(px, s.red_mask, s.red_shift, 0);
            out[1]       = channel(px, s.green_mask, s.green_shift, 0);
            out[2]       = channel(px, s.blue_mask, s.blue_shift, 0);
            out[3]       = channel(px, s.alpha_mask, s.alpha_shift, 255);
        }
    }

    const qoi_desc desc{unsigned(s.width), unsigned(s.height), 4, QOI_SRGB};
    int            length = 0;
    void          *qoi    = qoi_encode(rgba.data(), &desc, &length);
    if (!qoi)
        return nullopt;
    ClipboardFile file{"image/qoi", string(static_cast<const char *>(qoi), size_t(length))};
    free(qoi);
    return file;
}
#endif

#if defined(__linux__)
bool on_wayland() { return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND; }
#endif

/// The image file on the system's own clipboard, or nothing when it holds none or, setting \p failed, the read failed.
optional<ClipboardFile> read_system_clipboard(bool &failed)
{
#if defined(__linux__)
    if (on_wayland())
    {
        size_t      length = 0;
        const char *data   = glfwGetWaylandClipboardData("image/png", &length);
        if (!data || length == 0)
            return nullopt;
        return ClipboardFile{"image/png", string(data, length)};
    }
#endif
    try
    {
        clip::lock lock;
        if (!lock.locked())
            return nullopt;

        for (const auto &f : k_formats)
        {
            const clip::format format = clip::register_format(f.native);
            if (!lock.is_convertible(format))
                continue;
            string bytes(lock.get_data_length(format), '\0');
            if (!bytes.empty() && lock.get_data(format, bytes.data(), bytes.size()))
                return ClipboardFile{f.mime, std::move(bytes)};
        }

#if defined(_WIN32)
        // many Windows applications, and the Print Screen key, offer only a bitmap
        if (lock.is_convertible(clip::image_format()))
        {
            clip::image image;
            if (lock.get_image(image))
                return bitmap_as_qoi(image);
        }
#endif
    }
    catch (const exception &e)
    {
        spdlog::error("Could not read the system clipboard: {}", e.what());
        failed = true;
    }
    return nullopt;
}

#endif // __EMSCRIPTEN__

/// Put \p files on the system's own clipboard, returning whether that worked, or on the web, started.
bool write_system_clipboard(const vector<ClipboardFile> &files)
{
#if defined(__EMSCRIPTEN__)
    // a page may write only a PNG
    const ClipboardFile *file = single_file(files);
    if (!file)
        return false;
    hdrview_clipboard_write(file->type.c_str(), file->bytes.data(), file->bytes.size());
    return true;
#else
#if defined(__linux__)
    if (on_wayland())
    {
        // the fork offers one type per copy
        const ClipboardFile *file = single_file(files);
        if (!file)
            return false;
        glfwSetWaylandClipboardData(file->bytes.data(), file->type.c_str(), file->bytes.size());
        return true;
    }
#endif
    try
    {
        clip::lock lock;
        if (!lock.locked() || !lock.clear())
            return false;
        bool wrote = false;
        for (const auto &file : files)
        {
            // a format the platform has no name for, such as TIFF on Windows, is left out
            if (const char *native = native_name(file.type))
                wrote |= lock.set_data(clip::register_format(native), file.bytes.data(), file.bytes.size());
        }
        return wrote;
    }
    catch (const exception &e)
    {
        spdlog::error("Could not write to the system clipboard: {}", e.what());
        return false;
    }
#endif
}

/// Whether the system's own clipboard holds an image in a format HDRView reads.
bool system_clipboard_holds_image()
{
#if defined(__EMSCRIPTEN__)
    return true;
#else
#if defined(__linux__)
    if (on_wayland())
        return true;
#endif
    try
    {
        clip::lock lock;
        if (!lock.locked())
            return false;
        for (const auto &f : k_formats)
            if (lock.is_convertible(clip::register_format(f.native)))
                return true;
#if defined(_WIN32)
        return lock.is_convertible(clip::image_format());
#endif
    }
    catch (const exception &e)
    {
        spdlog::debug("Could not ask the system clipboard what it holds: {}", e.what());
    }
    return false;
#endif
}

} // namespace

#if defined(__EMSCRIPTEN__)
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
#endif

vector<ClipboardFile> &use_private_clipboard()
{
    s_use_private = true;
    return s_private;
}

bool set_clipboard_files(const vector<ClipboardFile> &files)
{
    if (s_use_private)
        s_private = files;
    else if (!write_system_clipboard(files))
        return false;

    s_own.clear();
    for (const auto &f : files) s_own.push_back({f.type, f.bytes.size(), hash_bytes(f.bytes)});
    s_holds_image = true;
#if defined(__EMSCRIPTEN__)
    s_own_since_focus = true;
#endif
    return true;
}

void read_clipboard_image(ReadDone done)
{
    if (s_use_private)
        return done({preferred_file(s_private)});

#if defined(__EMSCRIPTEN__)
    // a Ctrl/Cmd+V's own paste event, arriving or just arrived, which needs no permission
    if (s_paste_in_flight)
    {
        s_waiting = std::move(done);
        return;
    }
    if (ImGui::GetFrameCount() - s_pasted_frame <= 2)
    {
        s_pasted_frame = -1000;
        return done({std::exchange(s_pasted, nullopt)});
    }
    s_waiting = std::move(done);
    hdrview_clipboard_read();
#else
    ClipboardRead read;
    read.file     = read_system_clipboard(read.failed);
    s_holds_image = read.file.has_value();
    done(std::move(read));
#endif
}

bool is_own_clipboard_file(const ClipboardFile &file)
{
#if defined(__EMSCRIPTEN__)
    if (!s_use_private)
        return s_own_since_focus;
#endif
    for (const auto &own : s_own)
        if (file.type == own.type && file.bytes.size() == own.size && hash_bytes(file.bytes) == own.hash)
            return true;
    return false;
}

bool clipboard_carries(const string &type)
{
    if (s_use_private)
        return true;
#if defined(__EMSCRIPTEN__)
    return type == "image/png";
#else
#if defined(__linux__)
    if (on_wayland())
        return type == "image/png";
#endif
    return native_name(type) != nullptr;
#endif
}

bool clipboard_keeps_hdr()
{
#if defined(__EMSCRIPTEN__)
    return s_use_private || hdrview_clipboard_keeps_hdr();
#else
    return true;
#endif
}

bool clipboard_holds_image() { return s_use_private ? preferred_file(s_private).has_value() : s_holds_image; }

void watch_clipboard()
{
#if defined(__EMSCRIPTEN__)
    static bool s_listening = false;
    if (!std::exchange(s_listening, true))
        hdrview_clipboard_listen();
#endif

    // away from the start, so the first frame with the focus asks
    static bool s_away = true;
    if (ImGui::GetIO().AppFocusLost)
        s_away = true;
    if (!s_away || s_use_private)
        return;

    // the backend's own focus query, which Dear ImGui's GLFW backend leaves out on Wayland and the web, where asking
    // costs nothing anyway
    const auto &platform = ImGui::GetPlatformIO();
    if (platform.Platform_GetWindowFocus && !platform.Platform_GetWindowFocus(ImGui::GetMainViewport()))
        return;
    s_away        = false;
    s_holds_image = system_clipboard_holds_image();
}
