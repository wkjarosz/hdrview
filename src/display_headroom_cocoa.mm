//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "display_colorspace.h"
#include "imageio/icc.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>

float cocoa_display_headroom(void *window)
{
    @autoreleasepool
    {
        // the window's own screen; nil while it is offscreen or unplaced, so fall back to the focused one
        NSWindow *ns_window = window ? glfwGetCocoaWindow((GLFWwindow *)window) : nil;
        NSScreen *screen    = [ns_window screen] ?: [NSScreen mainScreen];
        if (!screen)
            return 0.f;

        // Already a multiple of SDR white, the convention the CAMetalLayer's extended-sRGB colorspace uses;
        // an SDR screen reports 1, never 0. The current, not the potential, value: an XDR panel can fall from
        // 5x to 1x within seconds and stay there, while its maximumPotential... sibling holds at 16.
        return (float)[screen maximumExtendedDynamicRangeColorComponentValue];
    }
}

std::optional<Chromaticities> cocoa_display_primaries(void *window)
{
    @autoreleasepool
    {
        NSWindow     *ns_window = window ? glfwGetCocoaWindow((GLFWwindow *)window) : nil;
        NSScreen     *screen    = [ns_window screen] ?: [NSScreen mainScreen];
        NSColorSpace *space     = [screen colorSpace];

        // the profile is parsed only when the screen's color space changes: on moving to another screen, or on
        // picking another profile in System Settings
        static NSColorSpace                 *last_space = nil;
        static std::optional<Chromaticities> last_primaries;
        if (space == last_space || [space isEqual:last_space])
            return last_primaries;

        last_space     = space;
        last_primaries = std::nullopt;
        if (NSData *icc = [space ICCProfileData])
        {
            Chromaticities chr;
            if (ICCProfile((const uint8_t *)icc.bytes, icc.length).extract_chromaticities(&chr))
                last_primaries = chr;
        }
        return last_primaries;
    }
}
