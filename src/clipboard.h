//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard.h
    \author Wojciech Jarosz

    The system clipboard, holding an image as an encoded file, such as a PNG, which HDRView's own loaders read.
*/

#pragma once

#include <functional>
#include <optional>
#include <string>

/// An encoded file on the system clipboard.
struct ClipboardFile
{
    std::string type;  ///< Its MIME type, such as "image/png"
    std::string bytes; ///< The file's contents
};

/// Replace what the system clipboard holds with \p file, returning whether that worked or, on the web, started.
bool set_clipboard_file(const ClipboardFile &file);

/// Read the image file on the system clipboard, and pass it to \p done, or nothing when it holds none.
/**
    Natively, \p done runs before this returns. On the web it runs once the browser has read the clipboard, outside
    any frame: from the page's paste event after Ctrl/Cmd+V, and otherwise from the browser's asynchronous read,
    which may first ask the user to allow it.
*/
void read_clipboard_image(std::function<void(std::optional<ClipboardFile>)> done);

/// Whether \p file is what set_clipboard_file() last put on the system clipboard.
/**
    Browsers re-encode the images a page writes, so on the web this asks instead whether this page copied last and
    has kept the focus since: another application can copy only while it has the focus.
*/
bool is_own_clipboard_file(const ClipboardFile &file);

/// Whether the system clipboard held an image when last checked, cheap enough to ask every frame.
/**
    Asking the system is not: on X11 it waits on whichever application copied last, and on Windows it opens the
    clipboard, which keeps every other application out of it meanwhile. So the answer is kept, refreshed by
    watch_clipboard() as the window regains focus, by each copy, and by each read. On Wayland, where the GLFW fork
    cannot list what another application offered, and on the web, where a page cannot ask without reading, the answer
    is always yes.
*/
bool clipboard_holds_image();

/// Notice the window regaining focus, and ask the system clipboard again then; call once per frame.
/**
    Another application can change the clipboard only while it, not HDRView, has the focus.
*/
void watch_clipboard();

/// Keep the clipboard in memory, apart from the system's, as the GUI tests do, returning it for them to fill.
/**
    Whatever a test writes there directly reads as another application's copy, since set_clipboard_file() did not
    write it.
*/
ClipboardFile &use_private_clipboard();
