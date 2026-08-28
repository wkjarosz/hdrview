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

    HDRView renders everything -- image content and Dear ImGui's UI alike -- in "extended sRGB": sRGB
    encoding over an unbounded signed range, with 1.0 meaning the display's SDR reference white. On macOS
    that convention is what Metal's EDR path consumes directly, so no conversion is needed. On
    Windows/Linux the display may instead want scRGB linear, a plain power curve, or PQ, possibly over a
    wider gamut -- see the colorpass in app-draw.cpp, which converts to whatever this struct describes.

    Everything here is expressed in HDRView's own color vocabulary (colorspace.h's TransferFunction and
    Chromaticities). The wp_color_manager_v1 protocol integers that GLFW reports exist only inside
    query_display_colorspace(); nothing downstream of it should ever see them.
*/
struct DisplayColorSpace
{
    /// How the display wants the final frame encoded.
    TransferFunction tf{TransferFunction::Gamma, 2.2f};
    /// The display's primaries. Rendering is done in Rec.709, so a conversion is needed when these differ.
    Chromaticities chroma = gamut_chromaticities(ColorGamut_sRGB_BT709);
    /// The display's configured SDR reference white, in nits. scRGB fixes this at 80, but Windows' "SDR
    /// content brightness" slider and Wayland compositors' reference luminance routinely raise it.
    float sdr_white_nits = 80.f;
    /// The display's reported luminance limits, in nits. 0 means "unknown", *not* "no limit" -- only clamp
    /// to max_nits when it is greater than zero.
    float min_nits = 0.f, max_nits = 0.f;

    /// The reference white of `tf` itself, in nits, for the transfer functions that are defined relative to
    /// one. Absolute transfer functions (PQ) return 0 -- they map nits directly to code values and must not
    /// be normalized by a reference white first.
    float transfer_white_nits() const;

    /// True when the display wants something other than HDRView's own extended-sRGB convention, i.e. when
    /// the colorpass has to run at all. False for the plain-SDR combination {gamma 2.2, sRGB, 80 nits}.
    bool needs_color_management() const;

    bool operator==(const DisplayColorSpace &o) const;
    bool operator!=(const DisplayColorSpace &o) const { return !(*this == o); }

    /// Human-readable summary for logs and the About dialog, e.g. "PQ / BT.2020, 203 nits SDR white".
    std::string name() const;
};

/**
    Ask GLFW what color space the window's display currently wants.

    Window-scoped, so this needs the real window to exist -- call it no earlier than
    PostInit_AddPlatformBackendCallbacks. Returns the plain-SDR default on any platform or GLFW build
    without HDR support, which makes the colorpass inert.

    `window` is a GLFWwindow*, kept as void* so this header doesn't drag GLFW into everything that includes
    it.
*/
DisplayColorSpace query_display_colorspace(void *window);

#if defined(__APPLE__)
/**
    Ask Cocoa how much headroom the window's screen currently has, as a multiple of SDR reference white.

    macOS reports this ratio directly (`NSScreen`'s
    `maximumExtendedDynamicRangeColorComponentValue`), so unlike every other platform there are no nits to
    divide. It is also the one display property GLFW's Cocoa backend does not forward -- its luminance
    getters are stubs -- which is why this sits beside query_display_colorspace() rather than inside it.

    Moves on its own -- with the brightness slider, and with whatever else AppKit decides -- and no
    notification is posted for any of it, so poll rather than caching. It is a couple of Objective-C
    message sends, ~35 ns.

    Returns 1 for a display with no headroom, and 0 only if the window is on no screen at all.

    `window` is a GLFWwindow*, kept as void* for the same reason as above.
*/
float cocoa_display_headroom(void *window);
#endif

#if defined(_WIN32)
/**
    Ask DXGI for the peak luminance, in nits, of the display the window is on.

    This is the one display property the GLFW fork's Win32 backend does not measure: its max-luminance getter
    is a flag, returning 80 nits for "not HDR" and 0 for "HDR, ceiling unknown". `IDXGIOutput6::GetDesc1`
    has the real number, so query_display_colorspace() calls this to fill in what GLFW left unknown.

    The value is fixed for a given display, but the SDR white level it is divided by is not, so headroom
    still moves with the "SDR content brightness" slider.

    Returns 0 when the ceiling is unknown -- no matching DXGI output, or a Windows too old for
    IDXGIOutput6. Enumerating adapters is far too slow to do every frame; the caller throttles it.

    `window` is a GLFWwindow*, kept as void* for the same reason as above.
*/
float win32_display_max_nits(void *window);
#endif
