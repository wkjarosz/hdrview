//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "display_colorspace.h"

#include <chrono>
#include <spdlog/spdlog.h>

// glad (via hello_imgui_include_opengl.h) must be included before GLFW/glfw3.h, and GLFW_INCLUDE_NONE keeps
// glfw3.h from also pulling in the system OpenGL headers itself, which glad refuses to build alongside.
#if defined(HELLOIMGUI_HAS_OPENGL)
#include <hello_imgui/hello_imgui_include_opengl.h>
#define GLFW_INCLUDE_NONE
#endif
#if defined(HELLOIMGUI_USE_GLFW3)
#include <GLFW/glfw3.h>
#endif

using namespace std;

// Everything in here exists only to translate what an HDR-enabling GLFW fork reports. Against stock GLFW
// (and on Emscripten) none of it is reachable, so it is compiled out entirely rather than left as dead code.
#if defined(GLFW_FLOATBUFFER)
namespace
{

// The wp_color_manager_v1 code points GLFW reports. GLFW publishes no enum or macro for these -- the Wayland
// protocol's numbering is the de facto cross-platform ABI of glfwGetWindowTransfer()/glfwGetWindowPrimaries(),
// which return these same integers verbatim on Windows and macOS too (where only a small subset is
// reachable). Mirrored from wayland-protocols' staging/color-management/color-management-v1.xml.
enum WpTransfer : uint32_t
{
    WpTransfer_BT1886 = 1,
    WpTransfer_Gamma22,
    WpTransfer_Gamma28,
    WpTransfer_ST240,
    WpTransfer_ExtLinear,
    WpTransfer_Log100,
    WpTransfer_Log316,
    WpTransfer_xvYCC,
    WpTransfer_sRGB,
    WpTransfer_ExtsRGB,
    WpTransfer_ST2084_PQ,
    WpTransfer_ST428,
    WpTransfer_HLG
};

enum WpPrimaries : uint32_t
{
    WpPrimaries_sRGB = 1,
    WpPrimaries_PAL_M,
    WpPrimaries_PAL,
    WpPrimaries_NTSC,
    WpPrimaries_GenericFilm,
    WpPrimaries_BT2020,
    WpPrimaries_CIE1931_XYZ,
    WpPrimaries_DCI_P3,
    WpPrimaries_Display_P3,
    WpPrimaries_AdobeRGB
};

TransferFunction transfer_from_wp(uint32_t wp)
{
    switch (wp)
    {
    case WpTransfer_BT1886: return TransferFunction::ITU;
    case WpTransfer_Gamma22: return {TransferFunction::Gamma, 2.2f};
    case WpTransfer_Gamma28: return {TransferFunction::Gamma, 2.8f};
    case WpTransfer_ST240: return TransferFunction::ST240;
    case WpTransfer_ExtLinear: return TransferFunction::Linear;
    case WpTransfer_Log100: return TransferFunction::Log100;
    case WpTransfer_Log316: return TransferFunction::Log100_Sqrt10;
    case WpTransfer_xvYCC: return TransferFunction::IEC61966_2_4;
    // Plain and extended sRGB share the same curve; HDRView's sRGB encoders are sign-extended already, so
    // the extended variant needs no separate case. Reachable on Windows whenever advanced color is enabled
    // but the framebuffer is only 8 bits per channel.
    case WpTransfer_sRGB: [[fallthrough]];
    case WpTransfer_ExtsRGB: return TransferFunction::sRGB;
    case WpTransfer_ST2084_PQ: return TransferFunction::BT2100_PQ;
    case WpTransfer_ST428: return TransferFunction::DCI_P3;
    case WpTransfer_HLG: return TransferFunction::BT2100_HLG;
    default:
        spdlog::warn("Display reports unknown wp_color_manager_v1 transfer function {}; assuming gamma 2.2.", wp);
        return {TransferFunction::Gamma, 2.2f};
    }
}

Chromaticities chromaticities_from_wp(uint32_t wp)
{
    switch (wp)
    {
    case WpPrimaries_sRGB: return gamut_chromaticities(ColorGamut_sRGB_BT709);
    case WpPrimaries_PAL_M: return gamut_chromaticities(ColorGamut_BT470M);
    case WpPrimaries_PAL: return gamut_chromaticities(ColorGamut_BT470BG);
    case WpPrimaries_NTSC: return gamut_chromaticities(ColorGamut_SMPTE170M_240M);
    case WpPrimaries_GenericFilm: return gamut_chromaticities(ColorGamut_Film);
    case WpPrimaries_BT2020: return gamut_chromaticities(ColorGamut_BT2020_2100);
    case WpPrimaries_CIE1931_XYZ: return gamut_chromaticities(ColorGamut_CIE1931XYZ);
    case WpPrimaries_DCI_P3: return gamut_chromaticities(ColorGamut_DCI_P3_SMPTE431);
    case WpPrimaries_Display_P3: return gamut_chromaticities(ColorGamut_Display_P3_SMPTE432);
    case WpPrimaries_AdobeRGB: return gamut_chromaticities(ColorGamut_AdobeRGB);
    default:
        spdlog::warn("Display reports unknown wp_color_manager_v1 primaries {}; assuming sRGB/BT.709.", wp);
        return gamut_chromaticities(ColorGamut_sRGB_BT709);
    }
}

} // namespace
#endif // GLFW_FLOATBUFFER

float DisplayColorSpace::transfer_white_nits() const
{
    switch (tf.type)
    {
    // PQ maps absolute luminance straight to code values -- there is no reference white to normalize by.
    case TransferFunction::BT2100_PQ: return 0.f;
    case TransferFunction::BT2100_HLG: return 1000.f;
    case TransferFunction::ITU: [[fallthrough]];
    case TransferFunction::IEC61966_2_4: return 100.f;
    // scRGB's fixed convention, and the sane default for everything else.
    default: return 80.f;
    }
}

bool DisplayColorSpace::needs_color_management() const
{
    static const Chromaticities rec709 = gamut_chromaticities(ColorGamut_sRGB_BT709);
    return !(tf.type == TransferFunction::Gamma && tf.gamma == 2.2f && chroma == rec709 && sdr_white_nits == 80.f);
}

bool DisplayColorSpace::operator==(const DisplayColorSpace &o) const
{
    if (tf.type != o.tf.type)
        return false;
    if (tf.type == TransferFunction::Gamma && tf.gamma != o.tf.gamma)
        return false;
    return chroma == o.chroma && sdr_white_nits == o.sdr_white_nits && min_nits == o.min_nits && max_nits == o.max_nits;
}

string DisplayColorSpace::name() const
{
    string s = color_profile_name(chroma, tf);
    s += fmt::format(", {:g} nits SDR white", sdr_white_nits);
    if (max_nits > 0.f)
        s += fmt::format(", {:g}-{:g} nits range", min_nits, max_nits);
    return s;
}

DisplayColorSpace query_display_colorspace([[maybe_unused]] void *window)
{
    DisplayColorSpace cs;

#if defined(GLFW_FLOATBUFFER)
    // These queries are window-scoped, so they need the real window/context to exist. They exist only in
    // HDR-enabling GLFW forks (see the Tom94/glfw pin in CMakeLists.txt); against stock GLFW this whole
    // block compiles out and the plain-SDR default above is returned, making the colorpass inert.
    auto *w = (GLFWwindow *)window;
    if (!w)
        return cs;

    cs.tf     = transfer_from_wp(glfwGetWindowTransfer(w));
    cs.chroma = chromaticities_from_wp(glfwGetWindowPrimaries(w));

#if defined(_WIN32)
    // On Windows this goes through QueryDisplayConfig, which is far too expensive to call every frame
    // (nanogui gives up and caches it once at startup). Re-query a few times a second instead: still fast
    // enough to follow the "SDR content brightness" slider live, without the per-frame cost.
    static auto  last_query       = std::chrono::steady_clock::now() - std::chrono::hours(1);
    static float cached_sdr_white = 0.f;

    auto now = std::chrono::steady_clock::now();
    if (now - last_query >= std::chrono::milliseconds(250))
    {
        cached_sdr_white = glfwGetWindowSdrWhiteLevel(w);
        last_query       = now;
    }
    float sdr_white = cached_sdr_white;
#else
    float sdr_white = glfwGetWindowSdrWhiteLevel(w);
#endif

    // 0 from GLFW means "unknown", not "no limit".
    cs.sdr_white_nits = sdr_white > 0.f ? sdr_white : 80.f;
    cs.min_nits       = glfwGetWindowMinLuminance(w);
    cs.max_nits       = glfwGetWindowMaxLuminance(w);
#endif

    return cs;
}
