//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// Ahead of every other include, since spdlog and GLFW both pull in <windows.h> themselves and whichever
// gets there first decides these: NOMINMAX keeps its min/max macros away from std::min/std::max, and
// WIN32_LEAN_AND_MEAN trims a header this file needs almost nothing from.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dxgi1_6.h>
#include <windows.h>

#include "display_colorspace.h"

#include <spdlog/spdlog.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace
{

/// Release and null, so the single exit path can drop whatever the loop below acquired.
template <typename T>
void release(T *&p)
{
    if (p)
        p->Release();
    p = nullptr;
}

} // namespace

float win32_display_max_nits(void *window)
{
    HWND hwnd = window ? glfwGetWin32Window((GLFWwindow *)window) : nullptr;
    if (!hwnd)
        return 0.f;

    // Which monitor the window is mostly on. DXGI identifies its outputs by the same HMONITOR, which is what
    // lets us pick the display the user is actually looking at rather than the first one enumerated.
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return 0.f;

    // No D3D device needed -- a bare factory is enough to walk adapters and outputs -- and DXGI needs no
    // CoInitialize. Created and released per call rather than kept alive: a cached factory goes stale
    // (IDXGIFactory1::IsCurrent) whenever the display configuration changes, which is exactly when the
    // answer here changes too.
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)))
        return 0.f;

    bool              matched = false;
    DXGI_OUTPUT_DESC1 found{};

    for (UINT a = 0; !matched; ++a)
    {
        IDXGIAdapter1 *adapter = nullptr;
        if (factory->EnumAdapters1(a, &adapter) != S_OK)
            break;

        for (UINT o = 0; !matched; ++o)
        {
            IDXGIOutput *output = nullptr;
            if (adapter->EnumOutputs(o, &output) != S_OK)
                break;

            // GetDesc1 (and the luminance fields with it) arrived with IDXGIOutput6 in Windows 10 1703; the
            // QueryInterface simply fails on anything older, leaving the ceiling unknown.
            IDXGIOutput6     *output6 = nullptr;
            DXGI_OUTPUT_DESC1 desc{};
            if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void **)&output6)) &&
                SUCCEEDED(output6->GetDesc1(&desc)) && desc.Monitor == monitor)
            {
                matched = true;
                found   = desc;
            }

            release(output6);
            release(output);
        }

        release(adapter);
    }

    release(factory);

    if (!matched)
        return 0.f;

    // Logged only when the numbers change, since this is polled several times a second.
    // MaxFullFrameLuminance appears nowhere else in HDRView, and a peak far above it is the usual reason an
    // image starts clipping well below the ceiling the histogram draws.
    static DXGI_OUTPUT_DESC1 last{};
    if (found.Monitor != last.Monitor || found.MaxLuminance != last.MaxLuminance ||
        found.MaxFullFrameLuminance != last.MaxFullFrameLuminance || found.MinLuminance != last.MinLuminance)
    {
        last = found;
        spdlog::info("Display reports {:g} nits peak luminance ({:g} nits full-frame, {:g} nits black).",
                     found.MaxLuminance, found.MaxFullFrameLuminance, found.MinLuminance);
    }

    // MaxLuminance is the small-area peak; MaxFullFrameLuminance is what the panel can sustain across the
    // whole screen, and on OLED the two differ severalfold. The peak is the one returned, matching what
    // Wayland compositors and macOS's EDR headroom describe, and the one that fits an image whose highlights
    // are small. Both come from EDID, which displays routinely overstate, so treat either as a claim rather
    // than a measurement.
    return (float)found.MaxLuminance;
}
