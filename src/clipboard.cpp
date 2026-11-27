//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "clipboard.h"
#include "clipboard_platform.h"

#include <imgui.h>

#include <string_view>
#include <utility>
#include <vector>

using namespace std;
namespace platform = clipboard_platform;

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

bool s_holds_image = false; ///< See clipboard_holds_image(), for a clipboard that can be asked

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

} // namespace

vector<ClipboardFile> &use_private_clipboard()
{
    s_use_private = true;
    return s_private;
}

bool set_clipboard_files(const vector<ClipboardFile> &files)
{
    if (s_use_private)
        s_private = files;
    else if (!platform::write(files))
        return false;

    s_own.clear();
    for (const auto &f : files) s_own.push_back({f.type, f.bytes.size(), hash_bytes(f.bytes)});
    s_holds_image = true;
    return true;
}

void read_clipboard_image(function<void(ClipboardRead)> done)
{
    if (s_use_private)
        return done({preferred_file(s_private)});

    platform::read(
        [done = std::move(done)](ClipboardRead read)
        {
            if (platform::can_ask() && !read.failed)
                s_holds_image = read.file.has_value();
            done(std::move(read));
        });
}

bool is_own_clipboard_file(const ClipboardFile &file)
{
    if (!s_use_private)
        if (const auto own = platform::own_copy(file))
            return *own;
    for (const auto &own : s_own)
        if (file.type == own.type && file.bytes.size() == own.size && hash_bytes(file.bytes) == own.hash)
            return true;
    return false;
}

bool clipboard_carries(const string &type) { return s_use_private || platform::carries(type); }

bool clipboard_keeps_hdr() { return s_use_private || platform::keeps_hdr(); }

bool clipboard_holds_image()
{
    if (s_use_private)
        return preferred_file(s_private).has_value();
    // a clipboard that cannot be asked without reading might hold one
    return !platform::can_ask() || s_holds_image;
}

void watch_clipboard()
{
    static bool s_started = false;
    if (!std::exchange(s_started, true))
        platform::start();

    // away from the start, so the first frame with the focus asks
    static bool s_away = true;
    if (ImGui::GetIO().AppFocusLost)
        s_away = true;
    if (!s_away || s_use_private || !platform::can_ask())
        return;

    // Dear ImGui's GLFW backend leaves its focus query out on Wayland and the web, which cannot be asked anyway
    const auto &io = ImGui::GetPlatformIO();
    if (io.Platform_GetWindowFocus && !io.Platform_GetWindowFocus(ImGui::GetMainViewport()))
        return;
    s_away        = false;
    s_holds_image = platform::holds_image();
}
