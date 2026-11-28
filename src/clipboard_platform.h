//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard_platform.h
    \author Wojciech Jarosz

    Private header for the platform-specific implementations of the clipboard: clipboard_native.cpp and
    clipboard_web.cpp.
*/

#pragma once

#include "clipboard.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clipboard_platform
{

/// Set up anything the clipboard needs from the main loop, once, from the first frame.
void start();

/// Put \p files on the system clipboard, returning whether that worked or, on the web, started.
bool write(const std::vector<ClipboardFile> &files);

/// Read the image on the system clipboard and pass what the read found to \p done; see read_clipboard_image().
void read(std::function<void(ClipboardRead)> done);

/// Whether holds_image() can tell what the clipboard holds without reading it; Wayland and the browsers cannot.
bool can_ask();
/// Whether the system clipboard holds an image HDRView reads, which may wait on another application.
bool holds_image();

/// See clipboard_carries().
bool carries(const std::string &type);
/// See clipboard_keeps_hdr().
bool keeps_hdr();

/// Whether \p file is what write() last put on the clipboard, where the platform tells that apart from its bytes.
/**
    A browser re-encodes what a page writes, so the web knows its own copy by the page keeping the focus since; the
    native clipboard returns nothing, and clipboard.cpp compares the bytes.
*/
std::optional<bool> own_copy(const ClipboardFile &file);

/// The file among \p files that a clipboard holding one per copy takes, or else the first.
/**
    The EXR, which carries the image's own half-float values in its own primaries, where the PQ PNG re-encodes
    them into BT.2020. The browsers take only the PNG, and clipboard_web.cpp's carries() leaves the rest out.
*/
inline const ClipboardFile *single_file(const std::vector<ClipboardFile> &files)
{
    for (const char *type : {"image/x-exr", "image/tiff", "image/png"})
        for (const auto &f : files)
            if (f.type == type)
                return &f;
    return files.empty() ? nullptr : &files.front();
}

} // namespace clipboard_platform
