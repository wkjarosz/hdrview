//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "fwd.h"

#include <optional>

/**
    The color space the display expects HDRView's final frame to be encoded in.

    HDRView renders everything in extended sRGB (sRGB encoding over an unbounded signed range, 1.0 meaning
    the display's SDR reference white), which Metal's EDR path consumes directly. On Windows/Linux the
    colorpass (app-colorpass.cpp) converts that to whatever this struct describes.

    This is HDRView's own color vocabulary; the wp_color_manager_v1 protocol integers GLFW reports exist
    only inside query_display_colorspace().
*/
struct DisplayColorSpace
{
    /// How the display wants the final frame encoded.
    TransferFunction tf{TransferFunction::Gamma, 2.2f};
    /// The primaries the frame is encoded in; HDRView renders in Rec.709 and converts when these differ.
    Chromaticities chroma = gamut_chromaticities(ColorGamut_sRGB_BT709);
    /// The panel's own primaries, where the platform reports them.
    /** Unlike `chroma`, these are what the screen can show once the OS has converted the frame to it: from the
        screen's color profile on macOS, and its EDID on Windows. */
    std::optional<Chromaticities> panel;
    /// The display's configured SDR reference white, in nits.
    /** scRGB fixes this at 80, but Windows' "SDR content brightness" slider and Wayland compositors'
        reference luminance routinely raise it. */
    float sdr_white_nits = 80.f;
    /// The display's reported luminance limits, in nits.
    /** 0 means "unknown", not "no limit": only clamp to max_nits when it is greater than zero. */
    float min_nits = 0.f, max_nits = 0.f;

    /// The reference white of `tf` itself, in nits, for the transfer functions defined relative to one.
    /** Absolute transfer functions (PQ) return 0: they map nits directly to code values. */
    float transfer_white_nits() const;

    /// True when the display wants something other than extended sRGB, so the colorpass has to run.
    /** False for the plain-SDR combination {gamma 2.2, sRGB, 80 nits}. */
    bool needs_color_management() const;

    bool operator==(const DisplayColorSpace &o) const;
    bool operator!=(const DisplayColorSpace &o) const { return !(*this == o); }

    /// Human-readable summary for logs and the About dialog, e.g. "PQ / BT.2020, 203 nits SDR white".
    std::string name() const;
};

/**
    Ask GLFW what color space the window's display currently wants.

    Window-scoped, so the real window must exist: call it no earlier than
    PostInit_AddPlatformBackendCallbacks. Returns the plain-SDR default on any platform or GLFW build
    without HDR support, which makes the colorpass inert.

    `window` is a GLFWwindow*, kept as void* so this header doesn't pull in GLFW.
*/
DisplayColorSpace query_display_colorspace(void *window);

/// The primaries of the colors a frame drawn for a display can show, and whether they are the panel's own.
struct DisplayGamut
{
    Chromaticities chroma;
    bool           panel = false; ///< true for the panel's own primaries, false for the frame's encoding's
};

/// The gamut a frame drawn for \p display reaches.
/**
    The panel's own primaries when the platform reports a plausible triangle for them and the frame can reach past
    its encoding's [0, 1] (\p float_buffer: macOS EDR, or scRGB), so the OS converts its colors outside the encoding
    to the panel. Otherwise the encoding clips to its own primaries, whatever the panel could show.
*/
DisplayGamut display_gamut(const DisplayColorSpace &display, bool float_buffer);

/// The steps the frame is dithered by in the image shader and in the colorpass, either 0 for none there.
struct DisplayDither
{ float image_shader = 0.f, colorpass = 0.f; };

/// Where a frame drawn for \p display is dithered, and by how large a step of the code values there.
/**
    PQ and HLG are dithered once the colorpass, which runs when \p color_managed, has encoded them, by a step of
    the 10 bits such displays take; the image shader's extended sRGB is not that encoding. Everything else is
    dithered as the image shader draws it: by a 10-bit step into a floating-point framebuffer (\p float_buffer:
    macOS EDR, or scRGB), which holds more than any panel shows, and by an 8-bit one into a fixed-point one.
*/
DisplayDither display_dither(const DisplayColorSpace &display, bool color_managed, bool float_buffer);

#if defined(__APPLE__)
/**
    Headroom of the screen the window is on, as a multiple of SDR reference white (`NSScreen`'s
    `maximumExtendedDynamicRangeColorComponentValue`). GLFW's Cocoa luminance getters are stubs, so this
    sits beside query_display_colorspace() instead of inside it.

    Changes with the brightness slider and posts no notification, so query it every frame; it is a couple of
    Objective-C message sends. Returns 1 for a display with no headroom, 0 if the window is on no screen.

    `window` is a GLFWwindow*, kept as void* for the same reason as above.
*/
float cocoa_display_headroom(void *window);

/// The primaries of the screen the window is on, read from its color profile; nullopt if they can't be read.
/**
    Reads the profile only when the screen's color space changes, so it is cheap to call every frame.
    `window` is a GLFWwindow*, kept as void* for the same reason as above.
*/
std::optional<Chromaticities> cocoa_display_primaries(void *window);
#endif

#if defined(_WIN32)
/// What DXGI reports about the display the window is on.
struct Win32Display
{
    float                         max_nits = 0.f; ///< peak luminance, in nits; 0 when unknown
    std::optional<Chromaticities> primaries;      ///< the panel's primaries, from its EDID
};

/**
    Ask DXGI for the peak luminance and primaries of the display the window is on.

    The GLFW fork's Win32 max-luminance getter is a flag: 80 nits for "not HDR", 0 for "HDR, ceiling
    unknown". `IDXGIOutput6::GetDesc1` has the real number, so query_display_colorspace() fills it in here.

    Leaves both unknown when there is no matching DXGI output, or the Windows is too old for IDXGIOutput6.
    Enumerating adapters is too slow to do every frame, so the caller throttles it.

    `window` is a GLFWwindow*, kept as void* for the same reason as above.
*/
Win32Display win32_display(void *window);
#endif
