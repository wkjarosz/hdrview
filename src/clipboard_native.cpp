//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard_native.cpp
    \author Wojciech Jarosz

    The system clipboard on macOS, Windows and Linux: see clipboard_platform.h.

    dacap/clip reaches it on macOS, Windows and X11, passing each encoded file as a custom format under the name the
    platform gives that file type. On Wayland, clip has no access, and the GLFW fork's own clipboard calls carry one
    file per copy.
*/

#include "clipboard_platform.h"

#include <clip.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>
#include <qoi.h>
#endif

#if defined(__linux__)
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#endif

using namespace std;

namespace
{

/// The clipboard formats HDRView reads, in the order it tries them, each as a MIME type and the platform's name.
struct Format
{
    const char *mime;
    const char *native;
};
// A read takes them in this order: a TIFF can hold floats, a PNG is at most 16-bit but is the one every
// application labels correctly, and an EXR comes last. An EXR holds floats too, but an application that renders
// every format it offers from one 8-bit image -- Krita does -- puts linear 8-bit values in it, which quantize the
// shadows far more coarsely than the same 8 bits through the PNG's sRGB curve.
// Qt's and gdk-pixbuf's readers both ask for "image/x-exr", the registered type; Krita offers an EXR under
// "image/exr", its own spelling, which only a read has to recognize.
#if defined(__APPLE__)
const Format k_formats[] = {
    {"image/tiff", "public.tiff"}, {"image/png", "public.png"}, {"image/x-exr", "com.ilm.openexr-image"}};
#elif defined(_WIN32)
// the name every Windows application registers for PNG data; TIFF has only a predefined format, which clip cannot
// address by name, and no application registers one for EXR
const Format k_formats[] = {{"image/png", "PNG"}};
#else
const Format k_formats[] = {{"image/tiff", "image/tiff"},
                            {"image/png", "image/png"},
                            {"image/x-exr", "image/x-exr"},
                            {"image/exr", "image/exr"}};
#endif

const char *native_name(const string &mime)
{
    for (const auto &f : k_formats)
        if (mime == f.mime)
            return f.native;
    return nullptr;
}

#if defined(__linux__)
bool on_wayland() { return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND; }
#else
constexpr bool on_wayland() { return false; }
#endif

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

/// What clip finds on the clipboard: the first of k_formats it holds, or on Windows, a bitmap.
ClipboardRead read_through_clip()
{
    ClipboardRead read;
    try
    {
        clip::lock lock;
        if (!lock.locked())
            return read;

        for (const auto &f : k_formats)
        {
            const clip::format format = clip::register_format(f.native);
            if (!lock.is_convertible(format))
                continue;
            string bytes(lock.get_data_length(format), '\0');
            if (!bytes.empty() && lock.get_data(format, bytes.data(), bytes.size()))
            {
                read.file = ClipboardFile{f.mime, std::move(bytes)};
                return read;
            }
        }

#if defined(_WIN32)
        // many Windows applications, and the Print Screen key, offer only a bitmap
        if (lock.is_convertible(clip::image_format()))
        {
            clip::image image;
            if (lock.get_image(image))
                read.file = bitmap_as_qoi(image);
        }
#endif
    }
    catch (const exception &e)
    {
        spdlog::error("Could not read the system clipboard: {}", e.what());
        read.failed = true;
    }
    return read;
}

} // namespace

namespace clipboard_platform
{

void start() {}

bool write(const vector<ClipboardFile> &files)
{
#if defined(__linux__)
    if (on_wayland())
    {
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
}

void read(function<void(ClipboardRead)> done)
{
#if defined(__linux__)
    if (on_wayland())
    {
        // The GLFW fork cannot list what another application offered, so each format is asked for in turn and the
        // one that answers wins. It hands back HDRView's own copy whatever format is asked for, which is why
        // is_own_clipboard_file() recognizes a copy by its bytes rather than by the format it came back as.
        ClipboardRead read;
        for (const auto &f : k_formats)
        {
            size_t      length = 0;
            const char *data   = glfwGetWaylandClipboardData(f.mime, &length);
            if (data && length)
            {
                read.file = ClipboardFile{f.mime, string(data, length)};
                break;
            }
        }
        return done(std::move(read));
    }
#endif
    done(read_through_clip());
}

// the GLFW fork cannot list what another application offered on Wayland
bool can_ask() { return !on_wayland(); }

bool holds_image()
{
    // Asking waits on whichever application copied last on X11, and on Windows opens the clipboard, keeping every
    // other application out of it meanwhile; clipboard.cpp asks only as the window regains focus.
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
}

bool carries(const string &type) { return on_wayland() ? type == "image/x-exr" : native_name(type) != nullptr; }

bool keeps_hdr() { return true; }

optional<bool> own_copy(const ClipboardFile &) { return nullopt; }

} // namespace clipboard_platform
