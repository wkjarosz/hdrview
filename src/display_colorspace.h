//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "fwd.h"

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
    /// The display's primaries. Rendering is done in Rec.709, so a conversion is needed when these differ.
    Chromaticities chroma = gamut_chromaticities(ColorGamut_sRGB_BT709);
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
#endif

#if defined(_WIN32)
/**
    Ask DXGI for the peak luminance, in nits, of the display the window is on.

    The GLFW fork's Win32 max-luminance getter is a flag: 80 nits for "not HDR", 0 for "HDR, ceiling
    unknown". `IDXGIOutput6::GetDesc1` has the real number, so query_display_colorspace() fills it in here.

    Returns 0 when the ceiling is unknown (no matching DXGI output, or a Windows too old for IDXGIOutput6).
    Enumerating adapters is too slow to do every frame, so the caller throttles it.

    `window` is a GLFWwindow*, kept as void* for the same reason as above.
*/
float win32_display_max_nits(void *window);
#endif
