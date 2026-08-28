//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "display_colorspace.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>

float cocoa_display_headroom(void *window)
{
    @autoreleasepool
    {
        // Per-window rather than per-application: an XDR laptop panel and an SDR external monitor have
        // genuinely different ceilings, and only the screen the window is actually on says anything
        // about what the user can see. `screen` is nil while the window is offscreen or not yet placed,
        // hence the fall back to whichever screen currently has focus.
        NSWindow *ns_window = window ? glfwGetCocoaWindow((GLFWwindow *)window) : nil;
        NSScreen *screen    = [ns_window screen] ?: [NSScreen mainScreen];
        if (!screen)
            return 0.f;

        // Already a multiple of SDR white -- the same convention as the extended-sRGB colorspace the
        // CAMetalLayer is configured with -- so it needs no conversion, unlike the nits every other
        // platform reports. An SDR screen reports exactly 1 here, never 0, so "unknown" does not arise.
        //
        // Deliberately the *current* value, which is what the display will actually produce right now.
        // It moves on its own, and by a lot: an XDR panel can fall from 5x to 1x within seconds and stay
        // there, on mains power and with no thermal warning, while its static sibling
        // maximumPotentialExtendedDynamicRangeColorComponentValue holds at 16. That sibling is the
        // steadier number, but it promises range the panel may currently refuse to give.
        return (float)[screen maximumExtendedDynamicRangeColorComponentValue];
    }
}
