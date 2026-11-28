//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard_wayland.h
    \author Wojciech Jarosz

    The Wayland clipboard, which HDRView drives through a wl_data_source of its own.
*/

#pragma once

#include "clipboard.h"

#include <optional>
#include <string>
#include <vector>

/// The Wayland clipboard: HDRView's own data source, over the display GLFW already opened.
/**
    The GLFW fork's clipboard calls carry one file per copy and cannot list what another application offered, so a
    copy there has to choose one encoding for every receiver and a paste has to ask for formats blind. Owning the
    data source lifts both limits: a copy offers every encoding at once and each application takes the one it
    reads, and a paste sees what is on offer before asking for any of it. Only wl_data_device_manager and the seat
    are needed, both core protocol, and the objects live on GLFW's own display and event queue.
*/
namespace clipboard_wayland
{

/// Bind what the clipboard needs, once, returning whether it is usable; everything below needs this first.
bool start();

/// Put \p files on the clipboard, under every MIME type they name, returning whether that worked.
bool write(const std::vector<ClipboardFile> &files);

/// The MIME types the selection offers, or nothing when another application holds no selection at all.
std::vector<std::string> offered_types();

/// Read the selection as \p type, or nothing when it does not offer that type or the read fails.
std::optional<ClipboardFile> read(const std::string &type);

/// Whether what write() last put on the clipboard is still the selection.
bool owns_selection();

} // namespace clipboard_wayland
