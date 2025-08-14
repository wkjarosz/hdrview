/** \file app.cpp
    \author Wojciech Jarosz
*/

#include "app.h"

#include "hello_imgui/dpi_aware.h"
#include "hello_imgui/hello_imgui.h"
#include "imcmd_command_palette.h"
#include "imgui_internal.h"
#include "implot.h"

#include "fonts.h"

#include "colormap.h"

#include "texture.h"

#include "opengl_check.h"

#include "colorspace.h"

#include "json.h"
#include "version.h"

#include <ImfHeader.h>
#include <ImfThreading.h>

#include <spdlog/mdc.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <fmt/core.h>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <utility>

#include "emscripten_utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten_browser_file.h>
#else
#include "portable-file-dialogs.h"
#endif

#ifdef HELLOIMGUI_USE_SDL2
#include <SDL.h>
#endif

#ifdef HELLOIMGUI_USE_GLFW3
#include <GLFW/glfw3.h>
#ifdef __APPLE__
// on macOS, we need to include this to get the NS api for opening files
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif
#endif

using std::to_string;
using std::unique_ptr;

#if defined(__EMSCRIPTEN__)
static float g_scroll_multiplier = 10.0f;
#else
static float g_scroll_multiplier = 1.0f;
#endif

struct WatchedPixel
{
    int2 pixel;
    int3 color_mode{0, 0, 0}; //!< Color mode for current, reference, and composite pixels
};

static vector<WatchedPixel> g_watched_pixels;

static std::mt19937    g_rand(53);
static constexpr float MIN_ZOOM       = 0.01f;
static constexpr float MAX_ZOOM       = 512.f;
static bool            g_show_help    = false;
static bool            g_help_is_open = false;
static json            g_settings;
static int             g_theme                               = -1;
static constexpr int   CUSTOM_THEME                          = -3; // Custom theme
static bool            g_show_command_palette                = false;
static bool            g_show_developer_menu                 = false;
static bool            g_show_tweak_window                   = false;
static bool            g_show_demo_window                    = false;
static bool            g_show_debug_window                   = false;
static bool            g_show_bg_color_picker                = false;
static char            g_filter_buffer[256]                  = {0};
static int             g_file_list_mode                      = 1; // 0: images only; 1: list; 2: tree;
static bool            g_request_sort                        = false;
static bool            g_short_names                         = false;
static MouseMode_      g_mouse_mode                          = MouseMode_PanZoom;
static bool            g_mouse_mode_enabled[MouseMode_COUNT] = {true, false, false};
static bool            g_play_forward                        = false;
static bool            g_play_backward                       = false;
static bool            g_play_stopped                        = true;
static float           g_playback_speed                      = 24.f;
static int             g_status_color_mode                   = 0;
static bool            g_reverse_colormap                    = false;

#define g_blank_icon ""

static HDRViewApp *g_hdrview = nullptr;

static void apply_hdrview_dark_theme()
{
    // Apply default style
    ImGuiStyle &style = ImGui::GetStyle();
    style             = ImGuiStyle(); // resets all fields to default values

    style.FontSizeBase           = 14.0f;              // base font size
    style._NextFrameFontSizeBase = style.FontSizeBase; // FIXME: Temporary hack until we finish remaining work.
    // make things like radio buttons look nice and round
    style.CircleTessellationMaxError = 0.1f;

    // Then apply modifications to ImGui style
    style.DisabledAlpha            = 0.5f;
    style.WindowRounding           = 0.f;
    style.WindowBorderSize         = 1.f;
    style.FrameBorderSize          = 1.f;
    style.WindowMenuButtonPosition = ImGuiDir_Right;
    style.WindowPadding            = ImVec2(8, 8);
    style.FrameRounding            = 3;
    style.PopupRounding            = 4;
    style.GrabRounding             = 2;
    style.ScrollbarRounding        = 4;
    style.TabRounding              = 4;
    style.WindowRounding           = 6;
    style.DockingSeparatorSize     = 2;
    style.SeparatorTextBorderSize  = 1;
    style.TabBarBorderSize         = 2;
    style.FramePadding             = ImVec2(4, 4);

    ImVec4 *colors                             = style.Colors;
    colors[ImGuiCol_Text]                      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]              = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]                  = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ChildBg]                   = ImVec4(0.04f, 0.04f, 0.04f, 0.20f);
    colors[ImGuiCol_PopupBg]                   = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Border]                    = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_BorderShadow]              = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
    colors[ImGuiCol_FrameBg]                   = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]            = ImVec4(1.00f, 1.00f, 1.00f, 0.13f);
    colors[ImGuiCol_FrameBgActive]             = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBg]                   = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgActive]             = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_MenuBarBg]                 = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]             = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]      = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]       = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_CheckMark]                 = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]                = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]          = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_Button]                    = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_ButtonHovered]             = ImVec4(1.00f, 1.00f, 1.00f, 0.13f);
    colors[ImGuiCol_ButtonActive]              = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_Header]                    = ImVec4(0.18f, 0.34f, 0.59f, 1.00f);
    colors[ImGuiCol_HeaderHovered]             = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_HeaderActive]              = ImVec4(0.29f, 0.58f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
    colors[ImGuiCol_SeparatorHovered]          = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_SeparatorActive]           = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_ResizeGrip]                = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]         = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]          = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_TabHovered]                = ImVec4(0.30f, 0.58f, 1.00f, 1.00f);
    colors[ImGuiCol_Tab]                       = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_TabSelected]               = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]       = ImVec4(0.30f, 0.58f, 1.00f, 0.00f);
    colors[ImGuiCol_TabDimmed]                 = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]         = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.30f, 0.58f, 1.00f, 0.00f);
    colors[ImGuiCol_DockingPreview]            = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_DockingEmptyBg]            = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_PlotLines]                 = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]          = ImVec4(1.00f, 0.39f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]             = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]      = ImVec4(1.00f, 0.39f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]             = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]         = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TableBorderLight]          = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_TableRowBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]             = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    colors[ImGuiCol_TextLink]                  = ImVec4(0.30f, 0.58f, 1.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]            = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
    colors[ImGuiCol_DragDropTarget]            = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_NavCursor]                 = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]     = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_NavWindowingDimBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);
    colors[ImGuiCol_ModalWindowDimBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);
}

static void apply_hdrview_light_theme()
{
    // Apply default style
    ImGuiStyle &style = ImGui::GetStyle();
    style             = ImGuiStyle(); // resets all fields to default values

    style.FontSizeBase           = 14.0f;              // base font size
    style._NextFrameFontSizeBase = style.FontSizeBase; // FIXME: Temporary hack until we finish remaining work.
    // make things like radio buttons look nice and round
    style.CircleTessellationMaxError = 0.1f;

    // Then apply modifications to ImGui style
    style.DisabledAlpha            = 0.5f;
    style.WindowRounding           = 0.f;
    style.WindowBorderSize         = 1.f;
    style.FrameBorderSize          = 1.f;
    style.WindowMenuButtonPosition = ImGuiDir_Right;
    style.WindowPadding            = ImVec2(8, 8);
    style.FrameRounding            = 3;
    style.PopupRounding            = 4;
    style.GrabRounding             = 2;
    style.ScrollbarRounding        = 4;
    style.TabRounding              = 4;
    style.WindowRounding           = 6;
    style.DockingSeparatorSize     = 2;
    style.SeparatorTextBorderSize  = 1;
    style.TabBarBorderSize         = 2;
    style.FramePadding             = ImVec2(4, 4);

    ImVec4 *colors                             = style.Colors;
    colors[ImGuiCol_Text]                      = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]              = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]                  = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_ChildBg]                   = ImVec4(0.04f, 0.04f, 0.04f, 0.20f);
    colors[ImGuiCol_PopupBg]                   = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
    colors[ImGuiCol_Border]                    = ImVec4(0.20f, 0.20f, 0.20f, 0.43f);
    colors[ImGuiCol_BorderShadow]              = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_FrameBg]                   = ImVec4(1.00f, 1.00f, 1.00f, 0.29f);
    colors[ImGuiCol_FrameBgHovered]            = ImVec4(1.00f, 1.00f, 1.00f, 0.39f);
    colors[ImGuiCol_FrameBgActive]             = ImVec4(0.34f, 0.50f, 0.76f, 1.00f);
    colors[ImGuiCol_TitleBg]                   = ImVec4(0.00f, 0.00f, 0.00f, 0.16f);
    colors[ImGuiCol_TitleBgActive]             = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
    colors[ImGuiCol_TitleBgCollapsed]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_MenuBarBg]                 = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]               = ImVec4(0.20f, 0.20f, 0.20f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab]             = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]      = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]       = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_CheckMark]                 = ImVec4(0.32f, 0.51f, 0.75f, 1.00f);
    colors[ImGuiCol_SliderGrab]                = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]          = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_Button]                    = ImVec4(1.00f, 1.00f, 1.00f, 0.55f);
    colors[ImGuiCol_ButtonHovered]             = ImVec4(0.34f, 0.50f, 0.76f, 1.00f);
    colors[ImGuiCol_ButtonActive]              = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_Header]                    = ImVec4(0.44f, 0.55f, 0.72f, 1.00f);
    colors[ImGuiCol_HeaderHovered]             = ImVec4(0.34f, 0.50f, 0.76f, 1.00f);
    colors[ImGuiCol_HeaderActive]              = ImVec4(0.29f, 0.58f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
    colors[ImGuiCol_SeparatorHovered]          = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_SeparatorActive]           = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_ResizeGrip]                = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]         = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]          = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_InputTextCursor]           = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TabHovered]                = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
    colors[ImGuiCol_Tab]                       = ImVec4(0.53f, 0.53f, 0.53f, 1.00f);
    colors[ImGuiCol_TabSelected]               = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]       = ImVec4(0.30f, 0.58f, 1.00f, 0.00f);
    colors[ImGuiCol_TabDimmed]                 = ImVec4(0.53f, 0.53f, 0.53f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]         = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.30f, 0.58f, 1.00f, 0.00f);
    colors[ImGuiCol_DockingPreview]            = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_DockingEmptyBg]            = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_PlotLines]                 = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]          = ImVec4(1.00f, 0.39f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]             = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]      = ImVec4(1.00f, 0.39f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]             = ImVec4(0.64f, 0.64f, 0.64f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]         = ImVec4(0.12f, 0.12f, 0.12f, 0.39f);
    colors[ImGuiCol_TableBorderLight]          = ImVec4(0.24f, 0.24f, 0.24f, 0.06f);
    colors[ImGuiCol_TableRowBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
    colors[ImGuiCol_TableRowBgAlt]             = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
    colors[ImGuiCol_TextLink]                  = ImVec4(0.30f, 0.58f, 1.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]            = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
    colors[ImGuiCol_TreeLines]                 = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_DragDropTarget]            = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_NavCursor]                 = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]     = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_NavWindowingDimBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);
    colors[ImGuiCol_ModalWindowDimBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);
}

static string theme_name(int t)
{
    if (t >= 0 && t < ImGuiTheme::ImGuiTheme_Count)
        return ImGuiTheme::ImGuiTheme_Name((ImGuiTheme::ImGuiTheme_)t);
    else if (t == -1)
        return "HDRView dark";
    else if (t == -2)
        return "HDRView light";
    else
        return "Custom";
}

static void apply_theme(int t)
{
    spdlog::info("Applying theme: '{}'", theme_name(t));
    if (t >= ImGuiTheme::ImGuiTheme_Count)
    {
        spdlog::error("Invalid theme index: {}. Using default theme.", t);
        t = -1;
    }

    g_theme = t;

    if (t >= 0)
    {
        ImGuiTheme::ImGuiTheme_ theme                                       = (ImGuiTheme::ImGuiTheme_)t;
        HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme.Theme = theme;
        ImGuiTheme::ApplyTheme(theme);
    }
    else if (t == -1)
        apply_hdrview_dark_theme();
    else if (t == -2)
        apply_hdrview_light_theme();

    // otherwise, its a custom theme, and we keep the parameters that were read from the config file
}

static void load_theme(json j)
{
    if (j.contains("theme"))
    {
        spdlog::info("Restoring theme: '{}'", j["theme"].get<string>());
        auto name = j["theme"].get<string>();
        if (name == "HDRView dark")
            g_theme = -1;
        else if (name == "HDRView light")
            g_theme = -2;
        else if (name == "Custom")
            g_theme = CUSTOM_THEME;
        else
            g_theme = HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme.Theme =
                ImGuiTheme::ImGuiTheme_FromName(name.c_str());
    }
    else
        g_theme = -1;

    if (g_theme >= 0)
        ApplyTweakedTheme(HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme);

    if (g_theme == -2 && j.contains("style"))
    {
        spdlog::debug("Restoring custom ImGui style values from settings:\n{}", j["style"].dump(2));
        json   &j_style    = j["style"];
        auto   &style      = ImGui::GetStyle();
        ImVec4 *colors     = ImGui::GetStyle().Colors;
        auto    read_color = [&](const char *key, ImGuiCol idx)
        {
            if (j_style.contains(key) && j_style[key].is_array() && j_style[key].size() == 4)
                colors[idx] = ImVec4(j_style[key][0].get<float>(), j_style[key][1].get<float>(),
                                     j_style[key][2].get<float>(), j_style[key][3].get<float>());
            // else: leave as is (current style value)
        };

        // Loop over all ImGuiCol values and try to read each from the JSON
        for (int col = 0; col < ImGuiCol_COUNT; ++col)
        {
            // Get the enum name as a string, e.g., "ImGuiCol_Text"
            const char *col_name = ImGui::GetStyleColorName(col);
            if (col_name)
                read_color(col_name, (ImGuiCol)col);
        }

        // ImGuiStyle scalar/vector members
        auto read_float = [&](const char *key, float &val)
        {
            if (j_style.contains(key))
                val = j_style[key].get<float>();
        };
        auto read_vec2 = [&](const char *key, ImVec2 &val)
        {
            if (j_style.contains(key) && j_style[key].is_array() && j_style[key].size() == 2)
                val = ImVec2(j_style[key][0].get<float>(), j_style[key][1].get<float>());
        };

        read_float("Alpha", style.Alpha);
        read_float("DisabledAlpha", style.DisabledAlpha);
        read_vec2("WindowPadding", style.WindowPadding);
        read_float("WindowRounding", style.WindowRounding);
        read_float("WindowBorderSize", style.WindowBorderSize);
        read_vec2("WindowMinSize", style.WindowMinSize);
        read_vec2("WindowTitleAlign", style.WindowTitleAlign);
        read_float("ChildRounding", style.ChildRounding);
        read_float("ChildBorderSize", style.ChildBorderSize);
        read_float("PopupRounding", style.PopupRounding);
        read_float("PopupBorderSize", style.PopupBorderSize);
        read_vec2("FramePadding", style.FramePadding);
        read_float("FrameRounding", style.FrameRounding);
        read_float("FrameBorderSize", style.FrameBorderSize);
        read_vec2("ItemSpacing", style.ItemSpacing);
        read_vec2("ItemInnerSpacing", style.ItemInnerSpacing);
        read_float("IndentSpacing", style.IndentSpacing);
        read_vec2("CellPadding", style.CellPadding);
        read_float("ScrollbarSize", style.ScrollbarSize);
        read_float("ScrollbarRounding", style.ScrollbarRounding);
        read_float("GrabMinSize", style.GrabMinSize);
        read_float("GrabRounding", style.GrabRounding);
        read_float("ImageBorderSize", style.ImageBorderSize);
        read_float("TabRounding", style.TabRounding);
        read_float("TabBorderSize", style.TabBorderSize);
        read_float("TabBarBorderSize", style.TabBarBorderSize);
        read_float("TabBarOverlineSize", style.TabBarOverlineSize);
        read_float("TableAngledHeadersAngle", style.TableAngledHeadersAngle);
        read_vec2("TableAngledHeadersTextAlign", style.TableAngledHeadersTextAlign);
        read_float("TreeLinesSize", style.TreeLinesSize);
        read_float("TreeLinesRounding", style.TreeLinesRounding);
        read_vec2("ButtonTextAlign", style.ButtonTextAlign);
        read_vec2("SelectableTextAlign", style.SelectableTextAlign);
        read_float("SeparatorTextBorderSize", style.SeparatorTextBorderSize);
        read_vec2("SeparatorTextAlign", style.SeparatorTextAlign);
        read_vec2("SeparatorTextPadding", style.SeparatorTextPadding);
        read_float("DockingSeparatorSize", style.DockingSeparatorSize);
        read_float("FontSizeBase", style.FontSizeBase);
        read_float("FontScaleMain", style.FontScaleMain);
        read_float("FontScaleDpi", style.FontScaleDpi);
        read_float("CircleTessellationMaxError", style.CircleTessellationMaxError);
        if (j_style.contains("WindowMenuButtonPosition"))
            style.WindowMenuButtonPosition = (ImGuiDir)j_style["WindowMenuButtonPosition"].get<int>();
    }
}

static void save_theme(json &j)
{
    // Save ImGui theme tweaks
    j["theme"] = theme_name(g_theme);

    if (g_theme != -2)
        return;

    auto   &j_style = j["style"];
    ImVec4 *colors  = ImGui::GetStyle().Colors;
    // Save all ImGuiCol colors
    for (int col = 0; col < ImGuiCol_COUNT; ++col)
    {
        const ImVec4 &c        = colors[col];
        const char   *col_name = ImGui::GetStyleColorName(col);
        if (col_name)
            j_style[col_name] = {c.x, c.y, c.z, c.w};
    }

    const auto &style                      = ImGui::GetStyle();
    j_style["Alpha"]                       = style.Alpha;
    j_style["DisabledAlpha"]               = style.DisabledAlpha;
    j_style["WindowPadding"]               = {style.WindowPadding.x, style.WindowPadding.y};
    j_style["WindowRounding"]              = style.WindowRounding;
    j_style["WindowBorderSize"]            = style.WindowBorderSize;
    j_style["WindowMinSize"]               = {style.WindowMinSize.x, style.WindowMinSize.y};
    j_style["WindowTitleAlign"]            = {style.WindowTitleAlign.x, style.WindowTitleAlign.y};
    j_style["ChildRounding"]               = style.ChildRounding;
    j_style["ChildBorderSize"]             = style.ChildBorderSize;
    j_style["PopupRounding"]               = style.PopupRounding;
    j_style["PopupBorderSize"]             = style.PopupBorderSize;
    j_style["FramePadding"]                = {style.FramePadding.x, style.FramePadding.y};
    j_style["FrameRounding"]               = style.FrameRounding;
    j_style["FrameBorderSize"]             = style.FrameBorderSize;
    j_style["ItemSpacing"]                 = {style.ItemSpacing.x, style.ItemSpacing.y};
    j_style["ItemInnerSpacing"]            = {style.ItemInnerSpacing.x, style.ItemInnerSpacing.y};
    j_style["IndentSpacing"]               = style.IndentSpacing;
    j_style["CellPadding"]                 = {style.CellPadding.x, style.CellPadding.y};
    j_style["ScrollbarSize"]               = style.ScrollbarSize;
    j_style["ScrollbarRounding"]           = style.ScrollbarRounding;
    j_style["GrabMinSize"]                 = style.GrabMinSize;
    j_style["GrabRounding"]                = style.GrabRounding;
    j_style["ImageBorderSize"]             = style.ImageBorderSize;
    j_style["TabRounding"]                 = style.TabRounding;
    j_style["TabBorderSize"]               = style.TabBorderSize;
    j_style["TabBarBorderSize"]            = style.TabBarBorderSize;
    j_style["TabBarOverlineSize"]          = style.TabBarOverlineSize;
    j_style["TableAngledHeadersAngle"]     = style.TableAngledHeadersAngle;
    j_style["TableAngledHeadersTextAlign"] = {style.TableAngledHeadersTextAlign.x, style.TableAngledHeadersTextAlign.y};
    j_style["TreeLinesSize"]               = style.TreeLinesSize;
    j_style["TreeLinesRounding"]           = style.TreeLinesRounding;
    j_style["ButtonTextAlign"]             = {style.ButtonTextAlign.x, style.ButtonTextAlign.y};
    j_style["SelectableTextAlign"]         = {style.SelectableTextAlign.x, style.SelectableTextAlign.y};
    j_style["SeparatorTextBorderSize"]     = style.SeparatorTextBorderSize;
    j_style["SeparatorTextAlign"]          = {style.SeparatorTextAlign.x, style.SeparatorTextAlign.y};
    j_style["SeparatorTextPadding"]        = {style.SeparatorTextPadding.x, style.SeparatorTextPadding.y};
    j_style["DockingSeparatorSize"]        = style.DockingSeparatorSize;
    j_style["FontSizeBase"]                = style.FontSizeBase;
    j_style["FontScaleMain"]               = style.FontScaleMain;
    j_style["FontScaleDpi"]                = style.FontScaleDpi;
    j_style["CircleTessellationMaxError"]  = style.CircleTessellationMaxError;
    j_style["WindowMenuButtonPosition"]    = (int)style.WindowMenuButtonPosition;

    spdlog::debug("Saved custom ImGui style values to settings:\n{}", j["style"].dump(2));
}

void init_hdrview(std::optional<float> exposure, std::optional<float> gamma, std::optional<bool> dither,
                  std::optional<bool> force_sdr, std::optional<bool> apple_keys, const vector<string> &in_files)
{
    if (g_hdrview)
    {
        spdlog::critical("HDRView already created!");
        exit(EXIT_FAILURE);
    }

    if (in_files.empty())
        g_show_help = true;

    spdlog::info("Overriding exposure: {}", exposure.has_value());
    spdlog::info("Overriding gamma: {}", gamma.has_value());
    spdlog::info("Overriding dither: {}", dither.has_value());
    spdlog::info("Forcing SDR: {}", force_sdr.has_value());
    spdlog::info("Overriding Apple-keyboard behavior: {}", apple_keys.has_value());

    g_hdrview = new HDRViewApp(exposure, gamma, dither, force_sdr, apple_keys, in_files);
}

HDRViewApp *hdrview() { return g_hdrview; }

HDRViewApp::HDRViewApp(std::optional<float> force_exposure, std::optional<float> force_gamma,
                       std::optional<bool> force_dither, std::optional<bool> force_sdr,
                       std::optional<bool> force_apple_keys, vector<string> in_files)
{
#if defined(__EMSCRIPTEN__) && !defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    // if threading is disabled, create no threads
    unsigned threads = 0;
#elif defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    // if threading is enabled in emscripten, then use just 1 thread
    unsigned threads = 1;
#else
    unsigned threads = std::thread::hardware_concurrency();
#endif

    spdlog::debug("Setting global OpenEXR thread count to {}", threads);
    Imf::setGlobalThreadCount(threads);
    spdlog::debug("OpenEXR reports global thread count as {}", Imf::globalThreadCount());

#if defined(__APPLE__)
    // if there is a screen with a non-retina resolution connected to an otherwise retina mac, the fonts may
    // look blurry. Here we force that macs always use the 2X retina scale factor for fonts. Produces crisp
    // fonts on the retina screen, at the cost of more jagged fonts on screen set to a non-retina resolution.
    m_params.dpiAwareParams.dpiWindowSizeFactor = 1.f;
    // m_params.dpiAwareParams.fontRenderingScale  = 0.5f;
#endif

    bool use_edr = HelloImGui::hasEdrSupport() && !force_sdr;

    if (force_sdr)
        spdlog::info("Forcing SDR display mode (display {} support EDR mode)",
                     HelloImGui::hasEdrSupport() ? "would otherwise" : "would not anyway");

    m_params.rendererBackendOptions.requestFloatBuffer = use_edr;
    spdlog::info("Launching GUI with {} display support.", use_edr ? "EDR" : "SDR");
    spdlog::info("Creating a {} framebuffer.", m_params.rendererBackendOptions.requestFloatBuffer
                                                   ? "floating-point precision"
                                                   : "standard precision");

    // set up HelloImGui parameters
    m_params.appWindowParams.windowGeometry.size     = {1200, 800};
    m_params.appWindowParams.windowTitle             = "HDRView";
    m_params.appWindowParams.restorePreviousGeometry = true;

    // Setting this to true allows multiple viewports where you can drag windows outside out the main window in
    // order to put their content into new native windows
    m_params.imGuiWindowParams.enableViewports        = false;
    m_params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    // m_params.imGuiWindowParams.backgroundColor        = float4{0.15f, 0.15f, 0.15f, 1.f};

    // Load additional font
    m_params.callbacks.LoadAdditionalFonts = [this]() { load_fonts(); };

    //
    // Menu bar
    //
    // Here, we fully customize the menu bar:
    // by setting `showMenuBar` to true, and `showMenu_App` and `showMenu_View` to false,
    // HelloImGui will display an empty menu bar, which we can fill with our own menu items via the callback
    // `ShowMenus`
    m_params.imGuiWindowParams.showMenuBar   = true;
    m_params.imGuiWindowParams.showMenu_App  = false;
    m_params.imGuiWindowParams.showMenu_View = false;
    // Inside `ShowMenus`, we can call `HelloImGui::ShowViewMenu` and `HelloImGui::ShowAppMenu` if desired
    m_params.callbacks.ShowMenus = [this]() { draw_menus(); };

    //
    // Toolbars
    //
    // ImGui::GetFrameHeight() * 1.4f
    m_top_toolbar_options.sizeEm = 2.2f;
    // HelloImGui::PixelSizeToEm(ImGui::GetFrameHeight());
    m_top_toolbar_options.WindowPaddingEm = ImVec2(0.7f, 0.35f);
    m_params.callbacks.AddEdgeToolbar(
        HelloImGui::EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);

    //
    // Status bar
    //
    // We use the default status bar of Hello ImGui
    m_params.imGuiWindowParams.showStatusBar  = false;
    m_params.imGuiWindowParams.showStatus_Fps = false;
    m_params.callbacks.ShowStatus             = [this]() { draw_status_bar(); };

    //
    // Dockable windows
    //

    HelloImGui::DockableWindow histogram_window;
    histogram_window.label             = "Histogram";
    histogram_window.dockSpaceName     = "HistogramSpace";
    histogram_window.isVisible         = true;
    histogram_window.rememberIsVisible = true;
    histogram_window.GuiFunction       = [this]
    {
        if (auto img = current_image())
            img->draw_histogram();
    };

    HelloImGui::DockableWindow channel_stats_window;
    channel_stats_window.label             = "Channel statistics";
    channel_stats_window.dockSpaceName     = "HistogramSpace";
    channel_stats_window.isVisible         = true;
    channel_stats_window.rememberIsVisible = true;
    channel_stats_window.GuiFunction       = [this]
    {
        if (auto img = current_image())
            return img->draw_channel_stats();
    };

    HelloImGui::DockableWindow file_window;
    file_window.label             = "Images";
    file_window.dockSpaceName     = "ImagesSpace";
    file_window.isVisible         = true;
    file_window.rememberIsVisible = true;
    file_window.GuiFunction       = [this] { draw_file_window(); };

    HelloImGui::DockableWindow info_window;
    info_window.label             = "Info";
    info_window.dockSpaceName     = "RightSpace";
    info_window.isVisible         = true;
    info_window.rememberIsVisible = true;
    // info_window.imGuiWindowFlags  = ImGuiWindowFlags_HorizontalScrollbar;
    info_window.GuiFunction = [this]
    {
        if (auto img = current_image())
            return img->draw_info();
    };

    HelloImGui::DockableWindow colorspace_window;
    colorspace_window.label             = "Colorspace";
    colorspace_window.dockSpaceName     = "RightSpace";
    colorspace_window.isVisible         = true;
    colorspace_window.rememberIsVisible = true;
    colorspace_window.imGuiWindowFlags =
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar;
    colorspace_window.GuiFunction = [this]
    {
        if (auto img = current_image())
            return img->draw_colorspace();
    };

    HelloImGui::DockableWindow pixel_inspector_window;
    pixel_inspector_window.label             = "Pixel inspector";
    pixel_inspector_window.dockSpaceName     = "RightBottomSpace";
    pixel_inspector_window.isVisible         = true;
    pixel_inspector_window.rememberIsVisible = true;
    pixel_inspector_window.GuiFunction       = [this] { draw_pixel_inspector_window(); };

    HelloImGui::DockableWindow channel_window;
    channel_window.label             = "Channels";
    channel_window.dockSpaceName     = "ImagesSpace";
    channel_window.isVisible         = true;
    channel_window.rememberIsVisible = true;
    channel_window.GuiFunction       = [this]
    {
        if (auto img = current_image())
            img->draw_channels_list(m_reference == m_current, true);
    };

    // the window showing the spdlog messages
    HelloImGui::DockableWindow log_window;
    log_window.label             = "Log";
    log_window.dockSpaceName     = "LogSpace";
    log_window.isVisible         = false;
    log_window.rememberIsVisible = true;
    log_window.GuiFunction       = [this]
    { ImGui::GlobalSpdLogWindow().draw(font("mono regular"), ImGui::GetStyle().FontSizeBase); };

#ifdef _WIN32
    ImGuiKey modKey = ImGuiMod_Alt;
#else
    ImGuiKey modKey = ImGuiMod_Super;
#endif

    // docking layouts
    m_params.dockingParams.layoutName      = "Standard";
    m_params.dockingParams.dockableWindows = {histogram_window,  channel_stats_window,   file_window,    info_window,
                                              colorspace_window, pixel_inspector_window, channel_window, log_window};
    struct DockableWindowExtraInfo
    {
        ImGuiKeyChord chord = ImGuiKey_None;
        const char   *icon  = nullptr;
    };
    vector<DockableWindowExtraInfo> window_info = {
        {ImGuiKey_F5, ICON_MY_HISTOGRAM_WINDOW},  {ImGuiKey_F6, ICON_MY_STATISTICS_WINDOW},
        {ImGuiKey_F7, ICON_MY_FILES_WINDOW},      {ImGuiMod_Ctrl | ImGuiKey_I, ICON_MY_INFO_WINDOW},
        {ImGuiKey_F8, ICON_MY_COLORSPACE_WINDOW}, {ImGuiKey_F9, ICON_MY_INSPECTOR_WINDOW},
        {ImGuiKey_F10, ICON_MY_CHANNELS_WINDOW},  {modKey | ImGuiKey_GraveAccent, ICON_MY_LOG_WINDOW},
        {ImGuiKey_F11, ICON_MY_SETTINGS_WINDOW}};
    m_params.dockingParams.dockingSplits = {
        HelloImGui::DockingSplit{"MainDockSpace", "HistogramSpace", ImGuiDir_Left, 0.2f},
        HelloImGui::DockingSplit{"HistogramSpace", "ImagesSpace", ImGuiDir_Down, 0.75f},
        HelloImGui::DockingSplit{"MainDockSpace", "LogSpace", ImGuiDir_Down, 0.25f},
        HelloImGui::DockingSplit{"MainDockSpace", "RightSpace", ImGuiDir_Right, 0.25f},
        HelloImGui::DockingSplit{"RightSpace", "RightBottomSpace", ImGuiDir_Down, 0.5f}};

#if defined(HELLOIMGUI_USE_GLFW3)
    m_params.callbacks.PostInit_AddPlatformBackendCallbacks = [this, in_files]
    {
        spdlog::trace("Registering glfw drop callback");
        // spdlog::trace("m_params.backendPointers.glfwWindow: {}", m_params.backendPointers.glfwWindow);
        glfwSetDropCallback((GLFWwindow *)m_params.backendPointers.glfwWindow,
                            [](GLFWwindow *, int count, const char **filenames)
                            {
                                spdlog::debug("Received glfw drop event");
                                vector<string> arg(count);
                                for (int i = 0; i < count; ++i) arg[i] = filenames[i];
                                hdrview()->load_images(arg);
                            });
#ifdef __APPLE__
        // On macOS, the mechanism for opening an application passes filenames
        // through the NS api rather than CLI arguments, which means we need
        // special handling of these through GLFW.
        // There are two components to this special handling:
        // (both of which need to happen here instead of HDRViewApp() because GLFW needs to have been
        // initialized first)

        // 1) Check if any filenames were passed via the NS api when the first instance of HDRView is launched.
        // However, this also seemingly returns (just the last) command-line argument, so if in_files is not empty, we
        // ignore it and rely on that mechanism to load the files
        if (in_files.empty())
        {
            const char *const *opened_files = glfwGetCocoaOpenedFilenames();
            if (opened_files)
            {
                spdlog::debug("Passing files in through the NS api...");
                vector<string> args;
                for (auto p = opened_files; *p; ++p) { args.emplace_back(string(*p)); }
                load_images(args);
            }
        }

        // 2) Register a callback on the running instance of HDRView for when the user:
        //    a) drags a file onto the HDRView app icon in the dock, and/or
        //    b) launches HDRView with files (either from the command line or Finder) when another instance is
        //    already
        //       running
        glfwSetCocoaOpenedFilenamesCallback(
            [](const char *image_file)
            {
                spdlog::debug("Receiving an app drag-drop event through the NS api for file '{}'", image_file);
                hdrview()->load_images({string(image_file)});
            });
#endif
    };
#endif

    //
    // Load user settings at `PostInit` and save them at `BeforeExit`
    //
    m_params.iniFolderType      = HelloImGui::IniFolderType::AppUserConfigFolder;
    m_params.iniFilename        = "HDRView/settings.ini";
    m_params.callbacks.PostInit = [this, force_exposure, force_gamma, force_dither, force_apple_keys]
    {
        setup_imgui_clipboard();
        load_settings();
        setup_rendering();

        if (force_exposure.has_value())
            m_exposure_live = m_exposure = *force_exposure;
        if (force_gamma.has_value())
            m_gamma_live = m_gamma = *force_gamma;
        if (force_dither.has_value())
            m_dither = *force_dither;

        auto is_safari = host_is_safari();
        auto is_apple  = host_is_apple();
        spdlog::info("Host is Apple: {}", is_apple);
        spdlog::info("Running in Safari: {}", is_safari);

        ImGui::GetIO().ConfigMacOSXBehaviors = is_apple;
        if (force_apple_keys.has_value())
            ImGui::GetIO().ConfigMacOSXBehaviors = *force_apple_keys;

        spdlog::info("Using {}-style keyboard behavior",
                     ImGui::GetIO().ConfigMacOSXBehaviors ? "Apple" : "Windows/Linux");
    };
    m_params.callbacks.BeforeExit = [this]
    {
        Image::cleanup_default_textures();
        Colormap::cleanup();
        save_settings();
    };

    // Change style
    m_params.callbacks.SetupImGuiStyle = []()
    {
        spdlog::info("Setting up ImGui Style: '{}'", theme_name(g_theme));
        load_theme(g_settings);
        apply_theme(g_theme);
    };

    m_params.callbacks.ShowGui = [this]()
    {
        m_image_loader.get_loaded_images(
            [this](ImagePtr new_image, ImagePtr to_replace, bool should_select)
            {
                int idx = (to_replace) ? image_index(to_replace) : -1;
                if (is_valid(idx))
                    m_images[idx] = new_image;
                else
                    m_images.push_back(new_image);

                if (should_select)
                    m_current = is_valid(idx) ? idx : int(m_images.size() - 1);

#if !defined(__EMSCRIPTEN__)
                m_active_directories.insert(fs::canonical(new_image->filename).parent_path());
#endif
                update_visibility(); // this also calls set_image_textures();
                g_request_sort = true;
            });

        draw_about_dialog();

        draw_command_palette();

        // popup version of the below; commented out because it doesn't allow right-clicking to change the color
        // picker type
        //
        // if (g_show_bg_color_picker)
        //     ImGui::OpenPopup("Background color");
        // g_show_bg_color_picker = false;
        // ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 5.f * HelloImGui::EmSize()),
        //                         ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
        // ImGui::SetNextWindowFocus();
        // if (ImGui::BeginPopup("Background color", ImGuiWindowFlags_NoSavedSettings |
        // ImGuiWindowFlags_AlwaysAutoResize))
        // {
        //     ImGui::TextUnformatted("Choose custom background color");
        //     ImGui::ColorPicker3("##Custom background color", (float *)&m_bg_color,
        //                         ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        //     ImGui::EndPopup();
        // }

        // window version of the above
        if (g_show_bg_color_picker)
        {
            // Center window horizontally, align near top vertically
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 5.f * HelloImGui::EmSize()),
                                    ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
            if (ImGui::Begin("Choose custom background color", &g_show_bg_color_picker,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking))
            {
                static float4 previous_bg_color = m_bg_color;
                if (ImGui::IsWindowAppearing())
                    previous_bg_color = m_bg_color;
                ImGui::ColorPicker4("##Custom background color", (float *)&m_bg_color,
                                    ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha,
                                    (float *)&previous_bg_color);

                ImGui::Dummy(HelloImGui::EmToVec2(1.f, 0.5f));
                if (ImGui::Button("OK", HelloImGui::EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Enter))
                    g_show_bg_color_picker = false;
                ImGui::SameLine();
                if (ImGui::Button("Cancel", HelloImGui::EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape))
                {
                    m_bg_color             = previous_bg_color;
                    g_show_bg_color_picker = false;
                }
            }
            ImGui::End();
        }

        if (g_show_tweak_window)
        {
            // auto &tweakedTheme = HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme;
            ImGui::SetNextWindowSize(HelloImGui::EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Theme Tweaks", &g_show_tweak_window))
            {
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f);
                if (ImGui::BeginCombo("Theme", theme_name(g_theme).c_str(), ImGuiComboFlags_HeightLargest))
                {
                    for (int t = -2; t < ImGuiTheme::ImGuiTheme_Count; ++t)
                    {
                        const bool is_selected = t == g_theme;
                        if (ImGui::Selectable(theme_name(t).c_str(), is_selected))
                            apply_theme(t);

                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGuiStyle previous = ImGui::GetStyle();

                ImGui::ShowStyleEditor(nullptr);

                bool theme_changed = memcmp(&previous, &ImGui::GetStyle(), sizeof(ImGuiStyle) - 2 * sizeof(float)) != 0;

                if (theme_changed)
                    g_theme = CUSTOM_THEME; // Custom theme
            }
            ImGui::End();
        }

        if (g_show_demo_window)
            ImGui::ShowDemoWindow(&g_show_demo_window);

        if (g_show_debug_window)
        {
            ImGui::SetNextWindowSize(HelloImGui::EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Debug", &g_show_debug_window))
            {
                if (ImGui::BeginTabBar("Debug tabs", ImGuiTabBarFlags_None))
                {
                    if (ImGui::BeginTabItem("Transfer functions"))
                    {
                        static float            gamma = 2.2f;
                        static TransferFunction tf    = TransferFunction_Linear;
                        ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.f);
                        if (ImGui::BeginCombo("##transfer function", transfer_function_name(tf, 1.f / gamma).c_str(),
                                              ImGuiComboFlags_HeightLargest))
                        {
                            for (TransferFunction_ n = TransferFunction_Linear; n < TransferFunction_Count; ++n)
                            {
                                const bool is_selected = (tf == n);
                                if (ImGui::Selectable(transfer_function_name((TransferFunction)n, 1.f / gamma).c_str(),
                                                      is_selected))
                                    tf = (TransferFunction)n;

                                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                                if (is_selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImPlot::BeginPlot("Transfer functions"))
                        {
                            ImPlot::SetupAxes("input", "encoded", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.f);
                            ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2.f);

                            auto f = [](float x) { return to_linear(x, tf, 1.f / gamma); };
                            auto g = [](float y) { return from_linear(y, tf, 1.f / gamma); };

                            const int    N = 101;
                            static float xs1[N], ys1[N];
                            for (int i = 0; i < N; ++i)
                            {
                                xs1[i] = i / float(N - 1);
                                ys1[i] = f(xs1[i]);
                            }
                            static float xs2[N], ys2[N];
                            for (int i = 0; i < N; ++i)
                            {
                                ys2[i] = lerp(0.0f, ys1[N - 1], i / float(N - 1));
                                xs2[i] = g(ys2[i]);
                            }

                            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                            ImPlot::PlotLine("to_linear", xs1, ys1, N);
                            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square);
                            ImPlot::PlotLine("from_linear", xs2, ys2, N);

                            ImPlot::PopStyleVar(2);
                            ImPlot::EndPlot();
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Illuminant spectra"))
                    {
                        if (ImPlot::BeginPlot("Illuminant spectra"))
                        {
                            ImPlot::SetupAxes("Wavelength", "Intensity", ImPlotAxisFlags_AutoFit,
                                              ImPlotAxisFlags_AutoFit);

                            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.f);
                            ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2.f);
                            ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Circle);

                            for (WhitePoint_ n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                            {
                                WhitePoint wp{n};
                                auto       spectrum = white_point_spectrum(wp);
                                if (spectrum.values.empty())
                                    continue;
                                string name{white_point_name(wp)};
                                ImPlot::PlotLine(name.c_str(), spectrum.values.data(), spectrum.values.size(),
                                                 (spectrum.max_wavelength - spectrum.min_wavelength) /
                                                     (spectrum.values.size() - 1),
                                                 spectrum.min_wavelength);
                            }
                            ImPlot::PopStyleVar(3);
                            ImPlot::EndPlot();
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("CIE 1931 XYZ"))
                    {
                        if (ImPlot::BeginPlot("CIE 1931 XYZ color matching functions"))
                        {
                            ImPlot::SetupAxes("Wavelength", "Intensity", ImPlotAxisFlags_AutoFit,
                                              ImPlotAxisFlags_AutoFit);

                            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.f);
                            ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2.f);
                            ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Circle);

                            auto &xyz       = CIE_XYZ_spectra();
                            auto  increment = (xyz.max_wavelength - xyz.min_wavelength) / xyz.values.size();
                            ImPlot::PlotLine("X", (const float *)&xyz.values[0].x, xyz.values.size(), increment,
                                             xyz.min_wavelength, ImPlotLineFlags_None, 0, sizeof(float3));
                            ImPlot::PlotLine("Y", (const float *)&xyz.values[0].y, xyz.values.size(), increment,
                                             xyz.min_wavelength, ImPlotLineFlags_None, 0, sizeof(float3));
                            ImPlot::PlotLine("Z", (const float *)&xyz.values[0].z, xyz.values.size(), increment,
                                             xyz.min_wavelength, ImPlotLineFlags_None, 0, sizeof(float3));

                            ImPlot::PopStyleVar(3);
                            ImPlot::EndPlot();
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }

            ImGui::End();
        }
    };
    m_params.callbacks.CustomBackground        = [this]() { draw_background(); };
    m_params.callbacks.AnyBackendEventCallback = [this](void *event) { return process_event(event); };

    //
    // Actions and command palette
    //
    {
        const auto always_enabled = []() { return true; };
        auto       add_action     = [this](const ImGui::Action &a)
        {
            m_action_map[a.name] = m_actions.size();
            m_actions.push_back(a);
        };
        add_action({"Open image...", ICON_MY_OPEN_IMAGE, ImGuiMod_Ctrl | ImGuiKey_O, 0, [this]() { open_image(); }});

#if !defined(__EMSCRIPTEN__)
        add_action({"Open folder...", ICON_MY_OPEN_FOLDER, ImGuiKey_None, 0, [this]() { open_folder(); }});
#endif

#if defined(__EMSCRIPTEN__)
        add_action({"Open URL...", ICON_MY_OPEN_IMAGE, ImGuiKey_None, 0,
                    [this]()
                    {
                        string url;
                        if (ImGui::InputTextWithHint("##URL", "Enter an image URL and press <return>", &url,
                                                     ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            ImGui::CloseCurrentPopup();
                            load_url(url);
                        }
                    },
                    always_enabled, true});
#endif

        add_action(
            {"Help", ICON_MY_ABOUT, ImGuiMod_Shift | ImGuiKey_Slash, 0, []() {}, always_enabled, false, &g_show_help});
        add_action({"Quit", ICON_MY_QUIT, ImGuiMod_Ctrl | ImGuiKey_Q, 0, [this]() { m_params.appShallExit = true; }});

        add_action({"Command palette...", ICON_MY_COMMAND_PALETTE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, 0,
                    []() {}, always_enabled, false, &g_show_command_palette});

        static bool toolbar_on = m_params.callbacks.edgesToolbars.find(HelloImGui::EdgeToolbarType::Top) !=
                                 m_params.callbacks.edgesToolbars.end();
        add_action({"Show top toolbar", ICON_MY_TOOLBAR, 0, 0,
                    [this]()
                    {
                        if (!toolbar_on)
                            m_params.callbacks.edgesToolbars.erase(HelloImGui::EdgeToolbarType::Top);
                        else
                            m_params.callbacks.AddEdgeToolbar(
                                HelloImGui::EdgeToolbarType::Top, [this]() { draw_top_toolbar(); },
                                m_top_toolbar_options);
                    },
                    always_enabled, false, &toolbar_on});
        add_action({"Show menu bar", ICON_MY_HIDE_ALL_WINDOWS, 0, 0, []() {}, always_enabled, false,
                    &m_params.imGuiWindowParams.showMenuBar});
        add_action({"Show status bar", ICON_MY_STATUSBAR, 0, 0, []() {}, always_enabled, false,
                    &m_params.imGuiWindowParams.showStatusBar});
        add_action({"Show FPS in status bar", ICON_MY_FPS, 0, 0, []() {}, always_enabled, false, &m_show_FPS});
        add_action(
            {"Enable idling", g_blank_icon, 0, 0, []() {}, always_enabled, false, &m_params.fpsIdling.enableIdling});

        auto any_window_hidden = [this]()
        {
            for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu && !dockableWindow.isVisible)
                    return true;
            return false;
        };

        add_action({"Show all windows", ICON_MY_SHOW_ALL_WINDOWS, ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = true;
                    },
                    any_window_hidden});

        add_action({"Hide all windows", ICON_MY_HIDE_ALL_WINDOWS, ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = false;
                    },
                    [any_window_hidden]() { return !any_window_hidden(); }});

        add_action({"Show entire GUI", ICON_MY_SHOW_ALL_WINDOWS, ImGuiMod_Shift | ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = true;
                        m_params.imGuiWindowParams.showMenuBar   = true;
                        m_params.imGuiWindowParams.showStatusBar = true;
                        m_params.callbacks.AddEdgeToolbar(
                            HelloImGui::EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);
                    },
                    [this, any_window_hidden]()
                    {
                        return any_window_hidden() || !m_params.imGuiWindowParams.showMenuBar ||
                               !m_params.imGuiWindowParams.showStatusBar || !toolbar_on;
                    }});

        add_action({"Hide entire GUI", ICON_MY_HIDE_GUI, ImGuiMod_Shift | ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = false;
                        m_params.imGuiWindowParams.showMenuBar   = false;
                        m_params.imGuiWindowParams.showStatusBar = false;
                        m_params.callbacks.edgesToolbars.erase(HelloImGui::EdgeToolbarType::Top);
                    },
                    [this, any_window_hidden]()
                    {
                        return !any_window_hidden() || m_params.imGuiWindowParams.showMenuBar ||
                               m_params.imGuiWindowParams.showStatusBar || toolbar_on;
                    }});

        add_action({"Restore default layout", ICON_MY_RESTORE_LAYOUT, 0, 0,
                    [this]() { m_params.dockingParams.layoutReset = true; },
                    [this]() { return !m_params.dockingParams.dockableWindows.empty(); }});

        add_action({"Show developer menu", ICON_MY_DEVELOPER_WINDOW, 0, 0, []() {}, always_enabled, false,
                    &g_show_developer_menu});
        add_action(
            {"Show Dear ImGui demo window", g_blank_icon, 0, 0, []() {}, always_enabled, false, &g_show_demo_window});
        add_action({"Show debug window", g_blank_icon, 0, 0, []() {}, always_enabled, false, &g_show_debug_window});
        add_action(
            {"Theme tweak window", ICON_MY_TWEAK_THEME, 0, 0, []() {}, always_enabled, false, &g_show_tweak_window});

        for (size_t i = 0; i < m_params.dockingParams.dockableWindows.size(); ++i)
        {
            HelloImGui::DockableWindow &w = m_params.dockingParams.dockableWindows[i];
            add_action({fmt::format("Show {} window", w.label).c_str(), window_info[i].icon, window_info[i].chord, 0,
                        []() {}, [&w]() { return w.canBeClosed; }, false, &w.isVisible});
        }

        add_action({"Decrease exposure", ICON_MY_DECREASE_EXPOSURE, ImGuiKey_E, ImGuiInputFlags_Repeat,
                    [this]() { m_exposure_live = m_exposure -= 0.25f; }});
        add_action({"Increase exposure", ICON_MY_INCREASE_EXPOSURE, ImGuiMod_Shift | ImGuiKey_E, ImGuiInputFlags_Repeat,
                    [this]() { m_exposure_live = m_exposure += 0.25f; }});
        add_action({"Reset tonemapping", ICON_MY_RESET_TONEMAPPING, 0, 0,
                    [this]()
                    {
                        m_exposure_live = m_exposure = 0.f;
                        m_offset_live = m_offset = 0.f;
                        m_gamma_live = m_gamma = 1.f;
                        m_tonemap              = Tonemap_Gamma;
                    },
                    always_enabled, false, nullptr, "Reset the exposure and blackpoint offset to 0."});
        add_action(
            {"Reverse colormap", ICON_MY_INVERT_COLORMAP, 0, 0, []() {}, always_enabled, false, &g_reverse_colormap});
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            add_action({"Clamp to LDR", ICON_MY_CLAMP_TO_LDR, ImGuiMod_Ctrl | ImGuiKey_L, 0, []() {}, always_enabled,
                        false, &m_clamp_to_LDR});
        add_action({"Dither", ICON_MY_DITHER, 0, 0, []() {}, always_enabled, false, &m_dither});
        add_action(
            {"Clip warnings", ICON_MY_ZEBRA_STRIPES, 0, 0, []() {}, always_enabled, false, &m_draw_clip_warnings});

        add_action({"Draw pixel grid", ICON_MY_SHOW_GRID, ImGuiMod_Ctrl | ImGuiKey_G, 0, []() {}, always_enabled, false,
                    &m_draw_grid});
        add_action({"Draw pixel values", ICON_MY_SHOW_PIXEL_VALUES, ImGuiMod_Ctrl | ImGuiKey_P, 0, []() {},
                    always_enabled, false, &m_draw_pixel_info});

        add_action({"Draw data window", ICON_MY_DATA_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                    &m_draw_data_window});
        add_action({"Draw display window", ICON_MY_DISPLAY_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                    &m_draw_display_window});

        add_action({"Decrease gamma/Previous colormap", ICON_MY_DECREASE_GAMMA, ImGuiKey_G, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        switch (m_tonemap)
                        {
                        default: [[fallthrough]];
                        case Tonemap_Gamma: m_gamma_live = m_gamma = std::max(0.02f, m_gamma - 0.02f); break;
                        case Tonemap_FalseColor: [[fallthrough]];
                        case Tonemap_PositiveNegative:
                            m_colormap_index = mod(m_colormap_index - 1, (int)m_colormaps.size());
                            break;
                        }
                    },
                    always_enabled});
        add_action({"Increase gamma/Next colormap", ICON_MY_INCREASE_GAMMA, ImGuiMod_Shift | ImGuiKey_G,
                    ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        switch (m_tonemap)
                        {
                        default: [[fallthrough]];
                        case Tonemap_Gamma: m_gamma_live = m_gamma = std::max(0.02f, m_gamma + 0.02f); break;
                        case Tonemap_FalseColor: [[fallthrough]];
                        case Tonemap_PositiveNegative:
                            m_colormap_index = mod(m_colormap_index + 1, (int)m_colormaps.size());
                            break;
                        }
                    },
                    always_enabled});

        add_action({"Pan and zoom", ICON_MY_PAN_ZOOM_TOOL, ImGuiKey_P, 0,
                    []()
                    {
                        for (int i = 0; i < MouseMode_COUNT; ++i) g_mouse_mode_enabled[i] = false;
                        g_mouse_mode                            = MouseMode_PanZoom;
                        g_mouse_mode_enabled[MouseMode_PanZoom] = true;
                    },
                    always_enabled, false, &g_mouse_mode_enabled[MouseMode_PanZoom]});
        add_action({"Rectangular select", ICON_MY_SELECT, ImGuiKey_M, 0,
                    []()
                    {
                        for (int i = 0; i < MouseMode_COUNT; ++i) g_mouse_mode_enabled[i] = false;
                        g_mouse_mode                                         = MouseMode_RectangularSelection;
                        g_mouse_mode_enabled[MouseMode_RectangularSelection] = true;
                    },
                    always_enabled, false, &g_mouse_mode_enabled[MouseMode_RectangularSelection]});
        add_action({"Pixel/color inspector", ICON_MY_WATCHED_PIXEL, ImGuiKey_I, 0,
                    []()
                    {
                        for (int i = 0; i < MouseMode_COUNT; ++i) g_mouse_mode_enabled[i] = false;
                        g_mouse_mode                                   = MouseMode_ColorInspector;
                        g_mouse_mode_enabled[MouseMode_ColorInspector] = true;
                    },
                    always_enabled, false, &g_mouse_mode_enabled[MouseMode_ColorInspector]});

        auto if_img = [this]() { return current_image() != nullptr; };

        // below actions are only available if there is an image

#if !defined(__EMSCRIPTEN__)
        add_action({"Reload image", ICON_MY_RELOAD, ImGuiMod_Ctrl | ImGuiKey_R, 0,
                    [this]() { reload_image(current_image()); }, if_img});
        add_action({"Reload all images", ICON_MY_RELOAD, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R, 0,
                    [this]()
                    {
                        for (auto &i : m_images) reload_image(i);
                    },
                    if_img});
        add_action({"Watch folders for changes", ICON_MY_WATCH_FOLDER, ImGuiKey_None, 0, []() {}, always_enabled, false,
                    &m_watch_files_for_changes});

        add_action({"Save as...", ICON_MY_SAVE_AS, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, 0,
                    [this]()
                    {
                        string filename =
                            pfd::save_file("Save as", g_blank_icon,
                                           {
                                               "Supported image files",
                                               fmt::format("*.{}", fmt::join(Image::savable_formats(), "*.")),
                                           })
                                .result();

                        if (!filename.empty())
                            save_as(filename);
                    },
                    if_img});
        add_action({"Export image as...", ICON_MY_SAVE_AS, ImGuiKey_None, 0,
                    [this]()
                    {
                        string filename =
                            pfd::save_file("Export image as", g_blank_icon,
                                           {
                                               "Supported image files",
                                               fmt::format("*.{}", fmt::join(Image::savable_formats(), "*.")),
                                           })
                                .result();

                        if (!filename.empty())
                            export_as(filename);
                    },
                    if_img});

#else
        add_action({"Save as...", ICON_MY_SAVE_AS, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, 0,
                    [this]()
                    {
                        string filename;
                        string filter = fmt::format("*.{}", fmt::join(Image::savable_formats(), " *."));
                        ImGui::TextUnformatted(
                            "Please enter a filename. Format is deduced from the accepted extensions:");
                        ImGui::TextFmt("\t{}", filter);
                        ImGui::Separator();
                        if (ImGui::InputTextWithHint("##Filename", "Enter a filename and press <return>", &filename,
                                                     ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            ImGui::CloseCurrentPopup();

                            if (!filename.empty())
                                save_as(filename);
                        }
                    },
                    if_img, true});
        add_action({"Export image as...", ICON_MY_SAVE_AS, ImGuiKey_None, 0,
                    [this]()
                    {
                        string filename;
                        string filter = fmt::format("*.{}", fmt::join(Image::savable_formats(), " *."));
                        ImGui::TextUnformatted(
                            "Please enter a filename. Format is deduced from the accepted extensions:");
                        ImGui::TextFmt("\t{}", filter);
                        ImGui::Separator();
                        if (ImGui::InputTextWithHint("##Filename", "Enter a filename and press <return>", &filename,
                                                     ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            ImGui::CloseCurrentPopup();

                            if (!filename.empty())
                                export_as(filename);
                        }
                    },
                    if_img, true});
#endif

        add_action({"Normalize exposure", ICON_MY_NORMALIZE_EXPOSURE, ImGuiKey_N, 0,
                    [this]()
                    {
                        if (auto img = current_image())
                        {
                            float minimum = std::numeric_limits<float>::max();
                            float maximum = std::numeric_limits<float>::min();
                            auto &group   = img->groups[img->selected_group];

                            bool3 should_include[NUM_CHANNELS] = {
                                {true, true, true},   // RGB
                                {true, false, false}, // RED
                                {false, true, false}, // GREEN
                                {false, false, true}, // BLUE
                                {true, true, true},   // ALPHA
                                {true, true, true}    // Y
                            };
                            for (int c = 0; c < std::min(group.num_channels, 3); ++c)
                            {
                                if (group.num_channels >= 3 && !should_include[m_channel][c])
                                    continue;
                                minimum =
                                    std::min(minimum, img->channels[group.channels[c]].get_stats()->summary.minimum);
                                maximum =
                                    std::max(maximum, img->channels[group.channels[c]].get_stats()->summary.maximum);
                            }

                            float factor    = 1.0f / (maximum - minimum);
                            m_exposure_live = m_exposure = log2(factor);
                            m_offset_live = m_offset = -minimum * factor;
                        }
                    },
                    if_img, false, nullptr,
                    "Adjust the exposure and blackpoint offset to fit image values to the range [0, 1]."});

        add_action({"Play forward", ICON_MY_PLAY_FORWARD, ImGuiKey_Space, 0,
                    [this]
                    {
                        g_play_backward &= !g_play_forward;
                        g_play_stopped                  = !(g_play_forward || g_play_backward);
                        m_params.fpsIdling.enableIdling = false;
                    },
                    always_enabled, false, &g_play_forward});
        add_action({"Stop playback", ICON_MY_STOP, ImGuiKey_Space, 0,
                    [this]
                    {
                        g_play_forward &= !g_play_stopped;
                        g_play_backward &= !g_play_stopped;
                        m_params.fpsIdling.enableIdling = true;
                    },
                    [] { return g_play_forward || g_play_backward; }, false, &g_play_stopped});
        add_action({"Play backward", ICON_MY_PLAY_BACKWARD, ImGuiMod_Shift | ImGuiKey_Space, 0,
                    [this]
                    {
                        g_play_forward &= !g_play_backward;
                        g_play_stopped                  = !(g_play_forward || g_play_backward);
                        m_params.fpsIdling.enableIdling = false;
                    },
                    always_enabled, false, &g_play_backward});

        // switch the current image using the image number (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Go to image {}", n), ICON_MY_IMAGE, ImGuiKey_0 + mod(n, 10), 0,
                        [this, n]() { set_current_image_index(nth_visible_image_index(mod(n - 1, 10))); },
                        [this, n]()
                        {
                            auto i = nth_visible_image_index(mod(n - 1, 10));
                            return is_valid(i) && i != m_current;
                        }});

        // select the reference image using Cmd + image number (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Set image {} as reference", n), ICON_MY_REFERENCE_IMAGE,
                        ImGuiMod_Ctrl | (ImGuiKey_0 + mod(n, 10)), 0,
                        [this, n]()
                        {
                            auto nth_visible = nth_visible_image_index(mod(n - 1, 10));
                            if (m_reference == nth_visible)
                                m_reference = -1;
                            else
                                set_reference_image_index(nth_visible);
                        },
                        [this, n]()
                        {
                            auto i = nth_visible_image_index(mod(n - 1, 10));
                            return is_valid(i);
                        }});

        // switch the selected channel group using Ctrl + number key (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Go to channel group {}", n), ICON_MY_CHANNEL_GROUP,
                        modKey | ImGuiKey(ImGuiKey_0 + mod(n, 10)), 0,
                        [this, n]()
                        {
                            auto img            = current_image();
                            img->selected_group = img->nth_visible_group_index(mod(n - 1, 10));
                        },
                        [this, n]()
                        {
                            if (auto img = current_image())
                            {
                                auto i = img->nth_visible_group_index(mod(n - 1, 10));
                                return img->is_valid_group(i) && i != img->selected_group;
                            }
                            return false;
                        }});
        // switch the reference channel group using Shift + Ctrl + number key (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Set channel group {} as reference", n), ICON_MY_REFERENCE_IMAGE,
                        ImGuiMod_Shift | modKey | ImGuiKey(ImGuiKey_0 + mod(n, 10)), 0,
                        [this, n]()
                        {
                            auto img         = current_image();
                            auto nth_visible = img->nth_visible_group_index(mod(n - 1, 10));
                            if (img->reference_group == nth_visible)
                            {
                                img->reference_group = -1;
                                m_reference          = -1;
                            }
                            else
                            {
                                img->reference_group = nth_visible;
                                m_reference          = m_current;
                            }
                        },
                        [this, n]()
                        {
                            if (auto img = current_image())
                            {
                                auto i = img->nth_visible_group_index(mod(n - 1, 10));
                                return img->is_valid_group(i);
                            }
                            return false;
                        }});

        add_action({"Close", ICON_MY_CLOSE, ImGuiMod_Ctrl | ImGuiKey_W, ImGuiInputFlags_Repeat,
                    [this]() { close_image(); }, if_img});
        add_action({"Close all", ICON_MY_CLOSE_ALL, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_W, 0,
                    [this]() { close_all_images(); }, if_img});

        add_action({"Go to next image", g_blank_icon, ImGuiKey_DownArrow, ImGuiInputFlags_Repeat,
                    [this]() { set_current_image_index(next_visible_image_index(m_current, Forward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_current, Forward);
                        return is_valid(i) && i != m_current;
                    }});
        add_action({"Go to previous image", g_blank_icon, ImGuiKey_UpArrow, ImGuiInputFlags_Repeat,
                    [this]() { set_current_image_index(next_visible_image_index(m_current, Backward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_current, Backward);
                        return is_valid(i) && i != m_current;
                    }});
        add_action({"Make next image the reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_DownArrow,
                    ImGuiInputFlags_Repeat,
                    [this]() { set_reference_image_index(next_visible_image_index(m_reference, Forward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_reference, Forward);
                        return is_valid(i) && i != m_reference;
                    }});
        add_action({"Make previous image the reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_UpArrow,
                    ImGuiInputFlags_Repeat,
                    [this]() { set_reference_image_index(next_visible_image_index(m_reference, Backward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_reference, Backward);
                        return is_valid(i) && i != m_reference;
                    }});
        add_action({"Go to next channel group", g_blank_icon, ImGuiKey_RightArrow, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        auto img            = current_image();
                        img->selected_group = img->next_visible_group_index(img->selected_group, Forward);
                    },
                    [this]() { return current_image() != nullptr; }});
        add_action({"Go to previous channel group", g_blank_icon, ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        auto img            = current_image();
                        img->selected_group = img->next_visible_group_index(img->selected_group, Backward);
                    },
                    [this]() { return current_image() != nullptr; }});
        add_action({"Go to next channel group in reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_RightArrow,
                    ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        // if no reference image is selected, use the current image
                        if (!reference_image())
                            m_reference = m_current;
                        auto img             = reference_image();
                        img->reference_group = img->next_visible_group_index(img->reference_group, Forward);
                    },
                    [this]() { return reference_image() || current_image(); }});
        add_action({"Go to previous channel group in reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_LeftArrow,
                    ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        // if no reference image is selected, use the current image
                        if (!reference_image())
                            m_reference = m_current;
                        auto img             = reference_image();
                        img->reference_group = img->next_visible_group_index(img->reference_group, Backward);
                    },
                    [this]() { return reference_image() || current_image(); }});

        add_action({"Zoom out", ICON_MY_ZOOM_OUT, ImGuiKey_Minus, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        zoom_out();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Zoom in", ICON_MY_ZOOM_IN, ImGuiKey_Equal, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        zoom_in();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"100%", ICON_MY_ZOOM_100, 0, 0,
                    [this]()
                    {
                        set_zoom_level(0.f);
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Center", ICON_MY_CENTER, ImGuiKey_C, 0,
                    [this]()
                    {
                        center();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiKey_F, 0,
                    [this]()
                    {
                        fit_display_window();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Auto fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiKey_F, 0,
                    [this]() { m_auto_fit_selection = m_auto_fit_data = false; }, if_img, false, &m_auto_fit_display});
        add_action({"Fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Alt | ImGuiKey_F, 0,
                    [this]()
                    {
                        fit_data_window();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Auto fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_F, 0,
                    [this]() { m_auto_fit_selection = m_auto_fit_display = false; }, if_img, false, &m_auto_fit_data});
        add_action({"Fit selection", ICON_MY_FIT_TO_WINDOW, ImGuiKey_None, 0,
                    [this]()
                    {
                        fit_selection();
                        cancel_autofit();
                    },
                    [if_img, this]() { return if_img() && m_roi.has_volume(); }});
        add_action({"Auto fit selection", ICON_MY_FIT_TO_WINDOW, ImGuiKey_None, 0,
                    [this]() { m_auto_fit_display = m_auto_fit_data = false; },
                    [if_img, this]() { return if_img() && m_roi.has_volume(); }, false, &m_auto_fit_selection});
        add_action({"Flip horizontally", ICON_MY_FLIP_HORIZ, ImGuiKey_H, 0, []() {}, if_img, false, &m_flip.x});
        add_action({"Flip vertically", ICON_MY_FLIP_VERT, ImGuiKey_V, 0, []() {}, if_img, false, &m_flip.y});
    }

    // load any passed-in images
    load_images(in_files);
}

void HDRViewApp::setup_rendering()
{
    try
    {
        m_render_pass = new RenderPass(false, true);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);
        m_render_pass->set_depth_test(RenderPass::DepthTest::Always, false);
        m_render_pass->set_clear_color(float4(0.15f, 0.15f, 0.15f, 1.f));

        m_shader = new Shader(
            m_render_pass,
            /* An identifying name */
            "ImageView", Shader::from_asset("shaders/image-shader_vert"),
            Shader::prepend_includes(Shader::from_asset("shaders/image-shader_frag"), {"shaders/colorspaces"}),
            Shader::BlendMode::AlphaBlend);

        const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

        m_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);

        Image::make_default_textures();
        Colormap::initialize();

        m_shader->set_texture("dither_texture", Image::dither_texture());
        set_image_textures();
        spdlog::info("Successfully initialized graphics API!");
    }
    catch (const std::exception &e)
    {
        spdlog::error("Shader initialization failed!:\n\t{}.", e.what());
    }
}

void HDRViewApp::load_settings()
{
    spdlog::info("Loading user settings from '{}'", HelloImGui::IniSettingsLocation(m_params));

    auto s = HelloImGui::LoadUserPref("UserSettings");
    if (s.empty())
    {
        spdlog::warn("No user settings found, using defaults.");
        return;
    }

    try
    {
        json j = json::parse(s);
        spdlog::debug("Restoring recent file list...");
        m_image_loader.set_recent_files(j.value<vector<string>>("recent files", {}));
        m_bg_mode =
            (EBGMode)std::clamp(j.value<int>("background mode", (int)m_bg_mode), (int)BG_BLACK, (int)NUM_BG_MODES - 1);
        m_bg_color.xyz() = j.value<float3>("background color", m_bg_color.xyz());

        m_draw_data_window    = j.value<bool>("draw data window", m_draw_data_window);
        m_draw_display_window = j.value<bool>("draw display window", m_draw_display_window);
        m_auto_fit_data       = j.value<bool>("auto fit data window", m_auto_fit_data);
        m_auto_fit_display    = j.value<bool>("auto fit display window", m_auto_fit_display);
        m_auto_fit_selection  = j.value<bool>("auto fit selection", m_auto_fit_selection);
        m_draw_pixel_info     = j.value<bool>("draw pixel info", m_draw_pixel_info);
        m_draw_grid           = j.value<bool>("draw pixel grid", m_draw_grid);
        m_exposure_live = m_exposure = j.value<float>("exposure", m_exposure);
        m_gamma_live = m_gamma = j.value<float>("gamma", m_gamma);
        m_tonemap              = j.value<Tonemap>("tonemap", m_tonemap);
        m_clamp_to_LDR         = j.value<bool>("clamp to LDR", m_clamp_to_LDR);
        m_dither               = j.value<bool>("dither", m_dither);
        g_file_list_mode       = j.value<int>("file list mode", g_file_list_mode);
        g_short_names          = j.value<bool>("short names", g_short_names);
        m_draw_clip_warnings   = j.value<bool>("draw clip warnings", m_draw_clip_warnings);
        m_show_FPS             = j.value<bool>("show FPS", m_show_FPS);
        m_clip_range           = j.value<float2>("clip range", m_clip_range);
        g_playback_speed       = j.value<float>("playback speed", g_playback_speed);

        g_show_developer_menu = j.value<bool>("show developer menu", g_show_developer_menu);

        // save settings so we can call load_theme from SetupImGuiStyle
        g_settings = j;
    }
    catch (json::exception &e)
    {
        spdlog::error("Error while parsing user settings: {}", e.what());
    }
}

void HDRViewApp::save_settings()
{
    spdlog::info("Saving user settings to '{}'", HelloImGui::IniSettingsLocation(m_params));

    json j;
    j["recent files"]            = m_image_loader.recent_files();
    j["background mode"]         = (int)m_bg_mode;
    j["background color"]        = m_bg_color.xyz();
    j["draw data window"]        = m_draw_data_window;
    j["draw display window"]     = m_draw_display_window;
    j["auto fit data window"]    = m_auto_fit_data;
    j["auto fit display window"] = m_auto_fit_display;
    j["auto fit selection"]      = m_auto_fit_selection;
    j["draw pixel info"]         = m_draw_pixel_info;
    j["draw pixel grid"]         = m_draw_grid;
    j["exposure"]                = m_exposure;
    j["gamma"]                   = m_gamma;
    j["tonemap"]                 = m_tonemap;
    j["clamp to LDR"]            = m_clamp_to_LDR;
    j["dither"]                  = m_dither;
    j["verbosity"]               = spdlog::get_level();
    j["file list mode"]          = g_file_list_mode;
    j["short names"]             = g_short_names;
    j["draw clip warnings"]      = m_draw_clip_warnings;
    j["show FPS"]                = m_show_FPS;
    j["clip range"]              = m_clip_range;
    j["show developer menu"]     = g_show_developer_menu;
    j["playback speed"]          = g_playback_speed;

    save_theme(j);

    HelloImGui::SaveUserPref("UserSettings", j.dump(4));
}

static void pixel_color_widget(const int2 &pixel, int &color_mode, int which_image, bool allow_copy = false,
                               float width = 0.f)
{
    float4   color32         = hdrview()->pixel_value(pixel, true, which_image);
    float4   displayed_color = linear_to_sRGB(hdrview()->pixel_value(pixel, false, which_image));
    uint32_t hex             = color_f128_to_u32(color_u32_to_f128(color_f128_to_u32(displayed_color)));
    int4     ldr_color       = int4{float4{color_u32_to_f128(hex)} * 255.f};
    bool3    inside          = {false, false, false};

    float start_x = ImGui::GetCursorPosX();

    int    components       = 4;
    string channel_names[4] = {"R", "G", "B", "A"};
    if (which_image != 2)
    {
        ConstImagePtr img;
        ChannelGroup  group;
        if (which_image == 0)
        {
            if (!hdrview()->current_image())
                return;
            img        = hdrview()->current_image();
            components = color_mode == 0 ? img->groups[img->selected_group].num_channels : 4;
            group      = img->groups[img->selected_group];
            inside[0]  = img->contains(pixel);
        }
        else if (which_image == 1)
        {
            if (!hdrview()->reference_image())
                return;
            img        = hdrview()->reference_image();
            components = color_mode == 0 ? img->groups[img->reference_group].num_channels : 4;
            group      = img->groups[img->reference_group];
            inside[1]  = img->contains(pixel);
        }

        if (color_mode == 0)
            for (int c = 0; c < components; ++c)
                channel_names[c] = Channel::tail(img->channels[group.channels[c]].name);
    }
    inside[2] = inside[0] || inside[1];

    ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf;
    if (ImGui::ColorButton("colorbutton", displayed_color, color_flags))
        ImGui::OpenPopup("dropdown");
    ImGui::SetItemTooltip("Click to change value format%s", allow_copy ? " or copy to clipboard." : ".");

    // ImGui::PushFont(sans_font);
    if (ImGui::BeginPopup("dropdown"))
    {
        if (allow_copy && ImGui::Selectable("Copy to clipboard"))
        {
            string buf;
            if (color_mode == 0)
            {
                if (components == 4)
                    buf = fmt::format("({:g}, {:g}, {:g}, {:g})", color32.x, color32.y, color32.z, color32.w);
                else if (components == 3)
                    buf = fmt::format("({:g}, {:g}, {:g})", color32.x, color32.y, color32.z);
                else if (components == 2)
                    buf = fmt::format("({:g}, {:g})", color32.x, color32.y);
                else
                    buf = fmt::format("{:g}", color32.x);
            }
            else if (color_mode == 1)
                buf = fmt::format("({:g}, {:g}, {:g}, {:g})", displayed_color.x, displayed_color.y, displayed_color.z,
                                  displayed_color.w);
            else if (color_mode == 2)
                buf = fmt::format("({:d}, {:d}, {:d}, {:d})", ldr_color.x, ldr_color.y, ldr_color.z, ldr_color.w);
            else if (color_mode == 3)
                buf = fmt::format("#{:02X}{:02X}{:02X}{:02X}", ldr_color.x, ldr_color.y, ldr_color.z, ldr_color.w);
            ImGui::SetClipboardText(buf.c_str());
        }
        ImGui::SeparatorText("Display as:");
        if (ImGui::Selectable("Raw values", color_mode == 0))
            color_mode = 0;
        if (ImGui::Selectable("Displayed color (32-bit)", color_mode == 1))
            color_mode = 1;
        if (ImGui::Selectable("Displayed color (8-bit)", color_mode == 2))
            color_mode = 2;
        if (ImGui::Selectable("Displayed color (hex)", color_mode == 3))
            color_mode = 3;

        ImGui::EndPopup();
    }

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    float w_full = (width == 0.f) ? ImGui::GetContentRegionAvail().x : width - (ImGui::GetCursorPosX() - start_x);
    // width available to all items (without spacing)
    float w_items    = w_full - ImGui::GetStyle().ItemInnerSpacing.x * (components - 1);
    float prev_split = w_items;
    // distributes the available width without jitter during resize
    auto set_item_width = [&prev_split, w_items, components](int c)
    {
        float next_split = ImMax(IM_TRUNC(w_items * (components - 1 - c) / components), 1.f);
        ImGui::SetNextItemWidth(ImMax(prev_split - next_split, 1.0f));
        prev_split = next_split;
    };

    ImGui::BeginDisabled(!inside[which_image]);
    ImGui::BeginGroup();
    if (color_mode == 0)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputFloat(fmt::format("##component {}", c).c_str(), &color32[c], 0.f, 0.f,
                              fmt::format("{}: %g", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 1)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputFloat(fmt::format("##component {}", c).c_str(), &displayed_color[c], 0.f, 0.f,
                              fmt::format("{}: %g", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 2)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputScalarN(fmt::format("##component {}", c).c_str(), ImGuiDataType_S32, &ldr_color[c], 1, NULL,
                                NULL, fmt::format("{}: %d", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 3)
    {
        ImGui::SetNextItemWidth(IM_TRUNC(w_full));
        ImGui::InputScalar("##hex color", ImGuiDataType_S32, &hex, NULL, NULL, "#%08X", ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::EndGroup();
    ImGui::EndDisabled();
}

void HDRViewApp::draw_status_bar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
    if (auto num = m_image_loader.num_pending_images())
    {
        // ImGui::PushFont(m_sans_regular, 8.f);
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), HelloImGui::EmToVec2(15.f, 0.f),
                           fmt::format("Loading {} image{}", num, num > 1 ? "s" : "").c_str());
        ImGui::SameLine();
        // ImGui::PopFont();
    }
    else if (m_remaining_download > 0)
    {
        ImGui::ScopedFont{nullptr, 4.0f};
        ImGui::ProgressBar((100 - m_remaining_download) / 100.f, HelloImGui::EmToVec2(15.f, 0.f), "Downloading image");
        ImGui::SameLine();
    }

    float x = ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x;

    auto sized_text = [&](float em_size, const string &text, float align = 1.f)
    {
        float item_width = HelloImGui::EmSize(em_size);
        float text_width = ImGui::CalcTextSize(text.c_str()).x;
        float spacing    = ImGui::GetStyle().ItemInnerSpacing.x;

        ImGui::SameLine(x + align * (item_width - text_width));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        x += item_width + spacing;
    };

    if (auto img = current_image())
    {
        auto &io          = ImGui::GetIO();
        auto  ref         = reference_image();
        bool  in_viewport = vp_pos_in_viewport(vp_pos_at_app_pos(io.MousePos));

        auto hovered_pixel = int2{pixel_at_app_pos(io.MousePos)};

        float4 top   = img->raw_pixel(hovered_pixel);
        float4 pixel = top;
        if (ref && ref->data_window.contains(hovered_pixel))
        {
            float4 bottom = ref->raw_pixel(hovered_pixel);
            // blend with reference image if available
            pixel = float4{blend(top.x, bottom.x, m_blend_mode), blend(top.y, bottom.y, m_blend_mode),
                           blend(top.z, bottom.z, m_blend_mode), blend(top.w, bottom.w, m_blend_mode)};
        }

        ImGui::BeginDisabled(!in_viewport);
        ImGui::SameLine();
        auto  fpy       = ImGui::GetStyle().FramePadding.y;
        float drag_size = HelloImGui::EmSize(5.f);
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
        auto y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel x coordinates", &hovered_pixel.x, 1.f, 0, 0, "X: %d", ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetCursorPosY(y + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel y coordinates", &hovered_pixel.y, 1.f, 0, 0, "Y: %d", ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();

        x += 2.f * drag_size + 2.f * ImGui::GetStyle().ItemInnerSpacing.x;

        sized_text(0.5f, "=", 0.5f);

        ImGui::PushID("Current");
        ImGui::SameLine(x);
        pixel_color_widget(hovered_pixel, g_status_color_mode, 2, false, HelloImGui::EmSize(25.f));
        ImGui::PopID();

        float real_zoom = m_zoom * pixel_ratio();
        int   numer     = (real_zoom < 1.0f) ? 1 : (int)round(real_zoom);
        int   denom     = (real_zoom < 1.0f) ? (int)round(1.0f / real_zoom) : 1;
        x               = ImGui::GetIO().DisplaySize.x - HelloImGui::EmSize(11.f) -
            (m_show_FPS ? HelloImGui::EmSize(14.f) : HelloImGui::EmSize(0.f));
        sized_text(10.f, fmt::format("{:7.2f}% ({:d}:{:d})", real_zoom * 100, numer, denom));
    }

    if (m_show_FPS)
    {
        ImGui::SameLine(ImGui::GetIO().DisplaySize.x - 14.f * ImGui::GetFontSize());
        ImGui::Checkbox("Enable idling", &m_params.fpsIdling.enableIdling);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("FPS: %.1f%s", HelloImGui::FrameRate(), m_params.fpsIdling.isIdling ? " (Idling)" : "");
    }

    ImGui::PopStyleVar();
}

void HDRViewApp::draw_menus()
{
    if (ImGui::BeginMenu("File"))
    {
        MenuItem(action("Open image..."));
#if !defined(__EMSCRIPTEN__)
        MenuItem(action("Open folder..."));
#endif

#if defined(__EMSCRIPTEN__)
        MenuItem(action("Open URL..."));
#else

        ImGui::BeginDisabled(m_image_loader.recent_files().empty());
        if (ImGui::BeginMenuEx("Open recent", ICON_MY_OPEN_IMAGE))
        {
            auto   recents = m_image_loader.recent_files_short(47, 50);
            size_t i       = 0;
            for (auto f = recents.begin(); f != recents.end(); ++f, ++i)
            {
                if (ImGui::MenuItem(fmt::format("{}##File{}", *f, i).c_str()))
                {
                    m_image_loader.load_recent_file(i);
                    break;
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Clear recently opened"))
                m_image_loader.clear_recent_files();
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        MenuItem(action("Reload image"));
        MenuItem(action("Reload all images"));
        MenuItem(action("Watch folders for changes"));
#endif

        ImGui::Separator();

        MenuItem(action("Save as..."));
        MenuItem(action("Export image as..."));

        ImGui::Separator();

        MenuItem(action("Close"));
        MenuItem(action("Close all"));

        ImGui::Separator();

        MenuItem(action("Quit"));

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        MenuItem(action("Zoom in"));
        MenuItem(action("Zoom out"));
        MenuItem(action("Center"));
        MenuItem(action("100%"));
        MenuItem(action("Fit display window"));
        MenuItem(action("Auto fit display window"));
        MenuItem(action("Fit data window"));
        MenuItem(action("Auto fit data window"));
        MenuItem(action("Fit selection"));
        MenuItem(action("Auto fit selection"));
        MenuItem(action("Flip horizontally"));
        MenuItem(action("Flip vertically"));

        ImGui::Separator();

        MenuItem(action("Draw pixel grid"));
        MenuItem(action("Draw pixel values"));
        MenuItem(action("Draw data window"));
        MenuItem(action("Draw display window"));

        ImGui::Separator();

        MenuItem(action("Increase exposure"));
        MenuItem(action("Decrease exposure"));
        MenuItem(action("Normalize exposure"));

        ImGui::Separator();

        MenuItem(action("Increase gamma/Next colormap"));
        MenuItem(action("Decrease gamma/Previous colormap"));

        ImGui::Separator();

        MenuItem(action("Reset tonemapping"));
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            MenuItem(action("Clamp to LDR"));
        MenuItem(action("Dither"));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
        ImGui::TextUnformatted(ICON_MY_ZEBRA_STRIPES);
        ImGui::SameLine();
        ImGui::TextUnformatted("Clip warnings");
        ImGui::SameLine();
        ImGui::Checkbox("##Draw clip warnings", &m_draw_clip_warnings);
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::BeginDisabled(!m_draw_clip_warnings);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::DragFloatRange2("##Clip warnings", &m_clip_range.x, &m_clip_range.y, 0.01f, 0.f, 0.f, "min: %.01f",
                               "max: %.01f");
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        MenuItem(action("Pan and zoom"));
        MenuItem(action("Rectangular select"));
        MenuItem(action("Pixel/color inspector"));

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows"))
    {
        MenuItem(action("Command palette..."));

        ImGui::Separator();

        MenuItem(action("Restore default layout"));

        ImGui::Separator();

        MenuItem(action("Show entire GUI"));
        MenuItem(action("Hide entire GUI"));

        MenuItem(action("Show all windows"));
        MenuItem(action("Hide all windows"));

        ImGui::Separator();

        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
        {
            if (!dockableWindow.includeInViewMenu)
                continue;

            MenuItem(action(fmt::format("Show {} window", dockableWindow.label)));
        }

        ImGui::Separator();

        MenuItem(action("Show top toolbar"));
        MenuItem(action("Show status bar"));
        MenuItem(action("Show FPS in status bar"));

        if (ImGui::BeginMenuEx("Theme", ICON_MY_THEME))
        {
            if (ImGui::MenuItemEx("Theme tweak window", ICON_MY_TWEAK_THEME, nullptr, g_show_tweak_window))
                g_show_tweak_window = !g_show_tweak_window;

            ImGui::Separator();

            int start = g_theme == CUSTOM_THEME ? CUSTOM_THEME : -2;
            for (int t = start; t < ImGuiTheme::ImGuiTheme_Count; ++t)
                if (ImGui::MenuItem(theme_name(t).c_str(), nullptr, t == g_theme))
                    apply_theme(t);

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    if (g_show_developer_menu && ImGui::BeginMenu("Developer"))
    {
        ImGui::MenuItem(action("Show Dear ImGui demo window"));
        ImGui::MenuItem(action("Show debug window"));
        ImGui::MenuItem(action("Show developer menu"));

        ImGui::EndMenu();
    }

    auto posX = (ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (2.f * (HelloImGui::EmSize(1.9f))));
    if (posX > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(posX);

    auto a = action("Show Log window");
    ImGui::MenuItem(a.icon, ImGui::GetKeyChordNameTranslated(a.chord), a.p_selected);
    a = action("Help");
    if (ImGui::MenuItem(a.icon, ImGui::GetKeyChordNameTranslated(a.chord), &g_help_is_open))
        g_show_help = true;
}

void HDRViewApp::save_as(const string &filename) const
{
    try
    {
#if !defined(__EMSCRIPTEN__)
        std::ofstream os{filename, std::ios_base::binary};
        current_image()->save(os, filename, powf(2.0f, m_exposure_live), true, m_dither);
#else
        std::ostringstream os;
        current_image()->save(os, filename, powf(2.0f, m_exposure_live), true, m_dither);
        string buffer = os.str();
        emscripten_browser_file::download(
            filename,                                    // the default filename for the browser to save.
            "application/octet-stream",                  // the MIME type of the data, treated as if it were a webserver
                                                         // serving a file
            string_view(buffer.c_str(), buffer.length()) // a buffer describing the data to download
        );
#endif
    }
    catch (const std::exception &e)
    {
        spdlog::error("An error occurred while saving to '{}':\n\t{}.", filename, e.what());
    }
    catch (...)
    {
        spdlog::error("An unknown error occurred while saving to '{}'.", filename);
    }
}

void HDRViewApp::export_as(const string &filename) const
{
    try
    {
        Image img(current_image()->size(), 4);
        img.finalize();
        auto bounds     = current_image()->data_window;
        int  block_size = std::max(1, 1024 * 1024 / img.size().x);
        parallel_for(blocked_range<int>(0, img.size().y, block_size),
                     [this, &img, bounds](int begin_y, int end_y, int, int)
                     {
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < img.size().x; ++x)
                             {
                                 float4 v = pixel_value(int2{x, y} + bounds.min, false, 2);

                                 img.channels[0](x, y) = v[0];
                                 img.channels[1](x, y) = v[1];
                                 img.channels[2](x, y) = v[2];
                                 img.channels[3](x, y) = v[3];
                             }
                     });

#if !defined(__EMSCRIPTEN__)
        std::ofstream os{filename, std::ios_base::binary};
        img.save(os, filename, 1.f, true, m_dither);
#else
        std::ostringstream os;
        img.save(os, filename, 1.f, true, m_dither);
        string buffer = os.str();
        emscripten_browser_file::download(
            filename,                                    // the default filename for the browser to save.
            "application/octet-stream",                  // the MIME type of the data, treated as if it were a webserver
                                                         // serving a file
            string_view(buffer.c_str(), buffer.length()) // a buffer describing the data to download
        );
#endif
    }
    catch (const std::exception &e)
    {
        spdlog::error("An error occurred while exporting to '{}':\n\t{}.", filename, e.what());
    }
    catch (...)
    {
        spdlog::error("An unknown error occurred while exporting to '{}'.", filename);
    }
}

void HDRViewApp::load_images(const vector<string> &filenames)
{
    string channel_selector = "";
    for (size_t i = 0; i < filenames.size(); ++i)
    {
        if (filenames[i][0] == ':')
        {
            channel_selector = filenames[i].substr(1);
            spdlog::debug("Channel selector set to: {}", channel_selector);
            continue;
        }

        load_image(filenames[i], {}, i == 0, channel_selector);
    }
}

void HDRViewApp::open_image()
{
#if defined(__EMSCRIPTEN__)

    // due to this bug, we just allow all file types on safari:
    // https://stackoverflow.com/questions/72013027/safari-cannot-upload-file-w-unknown-mime-type-shows-tempimage,
    string extensions =
        host_is_safari() ? "*" : fmt::format(".{}", fmt::join(Image::loadable_formats(), ",.")) + ",image/*";

    // open the browser's file selector, and pass the file to the upload handler
    spdlog::debug("Requesting file from user...");
    emscripten_browser_file::upload(
        extensions,
        [](const string &filename, const string &mime_type, string_view buffer, void *my_data = nullptr)
        {
            if (buffer.empty())
                spdlog::debug("User canceled upload.");
            else
            {
                auto [size, unit] = human_readable_size(buffer.size());
                spdlog::debug("User uploaded a {:.0f} {} file with filename '{}' of mime-type '{}'", size, unit,
                              filename, mime_type);
                hdrview()->load_image(filename, buffer, true);
            }
        });
#else
    string extensions = fmt::format("*.{}", fmt::join(Image::loadable_formats(), " *."));

    load_images(pfd::open_file("Open image(s)", "", {"Image files", extensions}, pfd::opt::multiselect).result());
#endif
}

void HDRViewApp::open_folder()
{
#if !defined(__EMSCRIPTEN__)
    load_images({pfd::select_folder("Open images in folder", "").result()});
#endif
}

// Note: the filename is passed by value in case its an element of m_recent_files, which we modify
void HDRViewApp::load_image(const string filename, const string_view buffer, bool should_select,
                            const string channel_selector)
{
    m_image_loader.background_load(filename, buffer, should_select, nullptr, channel_selector);
}

void HDRViewApp::load_url(const string_view url)
{
    if (url.empty())
        return;

#if !defined(__EMSCRIPTEN__)
    spdlog::error("load_url only supported via emscripten");
#else
    spdlog::info("Entered URL: {}", url);

    struct Payload
    {
        string      url;
        HDRViewApp *hdrview;
    };
    auto data = new Payload{string(url), this};

    m_remaining_download = 100;
    emscripten_async_wget2_data(
        data->url.c_str(), "GET", nullptr, data, true,
        (em_async_wget2_data_onload_func)[](unsigned, void *data, void *buffer, unsigned buffer_size) {
            auto   payload = reinterpret_cast<Payload *>(data);
            string url     = payload->url; // copy the url
            delete payload;

            auto filename    = get_filename(url);
            auto char_buffer = reinterpret_cast<const char *>(buffer);
            spdlog::info("Downloaded file '{}' with size {} from url '{}'", filename, buffer_size, url);
            hdrview()->load_image(url, {char_buffer, (size_t)buffer_size}, true);
        },
        (em_async_wget2_data_onerror_func)[](unsigned, void *data, int err, const char *desc) {
            auto   payload                         = reinterpret_cast<Payload *>(data);
            string url                             = payload->url; // copy the url
            payload->hdrview->m_remaining_download = 0;
            delete payload;

            spdlog::error("Downloading the file '{}' failed; {}: '{}'.", url, err, desc);
        },
        (em_async_wget2_data_onprogress_func)[](unsigned, void *data, int bytes_loaded, int total_bytes) {
            auto payload = reinterpret_cast<Payload *>(data);

            payload->hdrview->m_remaining_download = (total_bytes - bytes_loaded) / total_bytes;
        });

    // emscripten_async_wget_data(
    //     data->url.c_str(), data,
    //     (em_async_wget_onload_func)[](void *data, void *buffer, int buffer_size) {
    //         auto   payload = reinterpret_cast<Payload *>(data);
    //         string url     = payload->url; // copy the url
    //         delete payload;

    //         auto filename    = get_filename(url);
    //         auto char_buffer = reinterpret_cast<const char *>(buffer);
    //         spdlog::info("Downloaded file '{}' with size {} from url '{}'", filename, buffer_size, url);
    //         hdrview()->load_image(url, {char_buffer, (size_t)buffer_size}, true);
    //     },
    //     (em_arg_callback_func)[](void *data) {
    //         auto   payload = reinterpret_cast<Payload *>(data);
    //         string url     = payload->url; // copy the url
    //         delete payload;

    //         spdlog::error("Downloading the file '{}' failed.", url);
    //     });
#endif
}

void HDRViewApp::reload_image(ImagePtr image, bool should_select)
{
    if (!image)
    {
        spdlog::warn("Tried to reload a null image");
        return;
    }

    spdlog::info("Reloading file '{}' with channel selector '{}'...", image->filename, image->channel_selector);
    m_image_loader.background_load(image->filename, {}, should_select, image, image->channel_selector);
}

void HDRViewApp::reload_modified_files()
{
    bool any_reloaded = false;
    for (int i = 0; i < num_images(); ++i)
    {
        auto &img = m_images[i];
        if (!fs::exists(img->path))
        {
            spdlog::warn("File[{}] '{}' no longer exists, skipping reload.", i, img->path.u8string());
            continue;
        }

        fs::file_time_type last_modified;
        try
        {
            last_modified = fs::last_write_time(img->path);
        }
        catch (...)
        {
            continue;
        }

        if (last_modified != img->last_modified)
        {
            // Updating the last-modified date prevents double-scheduled reloads if the load take a lot of time or
            // fails.
            img->last_modified = last_modified;
            reload_image(img);
            any_reloaded = true;
        }
    }

    if (!any_reloaded)
        spdlog::debug("No modified files found to reload.");
}

void HDRViewApp::set_image_textures()
{
    try
    {
        // bind the primary and secondary images, or a placehold black texture when we have no current or
        // reference image
        if (auto img = current_image())
            img->set_as_texture(Target_Primary);
        else
            Image::set_null_texture(Target_Primary);

        if (auto ref = reference_image())
            ref->set_as_texture(Target_Secondary);
        else
            Image::set_null_texture(Target_Secondary);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Could not upload texture to graphics backend: {}.", e.what());
    }
}

void HDRViewApp::close_image()
{
    if (!current_image())
        return;

    // select the next image down the list
    int next = next_visible_image_index(m_current, Forward);
    if (next < m_current) // there is no visible image after this one, go to previous visible
        next = next_visible_image_index(m_current, Backward);

    auto filename = m_images[m_current]->filename;
    m_images.erase(m_images.begin() + m_current);

#if !defined(__EMSCRIPTEN__)
    auto parent_path = fs::canonical(m_images[m_current]->filename).parent_path();

    if (!m_active_directories.empty())
    {
        spdlog::debug("Active directories before closing image in '{}'.", parent_path.u8string());
        for (const auto &dir : m_active_directories) spdlog::debug("Active directory: {}", dir.u8string());
    }

    // Remove the parent directory from m_active_directories if no other images are from the same directory
    bool found = false;
    for (const auto &img : m_images)
        if (fs::canonical(img->filename).parent_path() == parent_path)
        {
            found = true;
            break;
        }

    if (!found)
        m_active_directories.erase(parent_path);

    if (!m_active_directories.empty())
    {
        spdlog::debug("Active directories after closing image in '{}'.", parent_path.u8string());
        for (const auto &dir : m_active_directories) spdlog::debug("Active directory: {}", dir.u8string());
    }

    spdlog::debug("Watched directories after closing image:");
    m_image_loader.remove_watched_directories(
        [this](const fs::path &path)
        {
            spdlog::debug("{} watched directory: {}", m_active_directories.count(path) == 0 ? "Removing" : "Keeping",
                          path.u8string());
            return m_active_directories.count(path) == 0;
        });
#endif

    // adjust the indices after erasing the current image
    set_current_image_index(next < m_current ? next : next - 1);
    set_reference_image_index(m_reference < m_current ? m_reference : m_reference - 1);

    update_visibility(); // this also calls set_image_textures();
}

void HDRViewApp::close_all_images()
{
    m_images.clear();
    m_current   = -1;
    m_reference = -1;
    m_active_directories.clear();
    m_image_loader.remove_watched_directories([](const fs::path &path) { return true; });
    update_visibility(); // this also calls set_image_textures();
}

void HDRViewApp::run()
{
    ImPlot::CreateContext();
    HelloImGui::Run(m_params);
    ImPlot::DestroyContext();
}

ImFont *HDRViewApp::font(const string &name) const
{
    if (name == "sans regular")
        return m_sans_regular;
    else if (name == "sans bold")
        return m_sans_bold;
    else if (name == "mono regular")
        return m_mono_regular;
    else if (name == "mono bold")
        return m_mono_bold;
    else
        throw std::runtime_error(fmt::format("Font with name '{}' was not loaded.", name));
}

HDRViewApp::~HDRViewApp() {}

float4 HDRViewApp::pixel_value(int2 p, bool raw, int which_image) const
{
    auto img1 = current_image();
    auto img2 = reference_image();

    float4 value;

    if (which_image == 0)
        value = img1 ? (raw ? img1->raw_pixel(p, Target_Primary) : img1->rgba_pixel(p, Target_Primary)) : float4{0.f};
    else if (which_image == 1)
        value =
            img2 ? (raw ? img2->raw_pixel(p, Target_Secondary) : img2->rgba_pixel(p, Target_Secondary)) : float4{0.f};
    else if (which_image == 2)
    {
        auto rgba1 = img1 ? img1->rgba_pixel(p, Target_Primary) : float4{0.f};
        auto rgba2 = img2 ? img2->rgba_pixel(p, Target_Secondary) : float4{0.f};
        value      = blend(rgba1, rgba2, m_blend_mode);
    }

    return raw ? value
               : ::tonemap(float4{powf(2.f, m_exposure_live) * value.xyz() + m_offset_live, value.w}, m_gamma_live,
                           m_tonemap, m_colormaps[m_colormap_index], g_reverse_colormap);
}

void HDRViewApp::draw_pixel_inspector_window()
{
    if (!current_image())
        return;

    auto PixelHeader = [](const string &title, int2 &pixel, bool *p_visible = nullptr)
    {
        bool open = ImGui::CollapsingHeader(title.c_str(), p_visible, ImGuiTreeNodeFlags_DefaultOpen);

        ImGuiInputTextFlags_ flags = p_visible ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        ImGui::BeginDisabled(p_visible == nullptr);
        // slightly convoluted process to show the coordinates as drag elements within the header
        ImGui::SameLine();
        auto  fpy = ImGui::GetStyle().FramePadding.y;
        float drag_size =
            0.5f * (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x - ImGui::GetFrameHeight());
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
        auto y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel x coordinates", &pixel.x, 1.f, 0, 0, "X: %d", flags);
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetCursorPosY(y + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel y coordinates", &pixel.y, 1.f, 0, 0, "Y: %d", flags);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();

        return open;
    };

    auto &io = ImGui::GetIO();

    ImGui::SeparatorText("Selection:");
    // float sz = ImGui::GetContentRegionAvail().x * 0.65f + ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(" Min,Max ").x);
    ImGui::DragInt4("Min,Max", &m_roi_live.min.x);
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_roi = m_roi_live;
    ImGui::SetItemTooltip("W x H: (%d x %d)", m_roi_live.size().x, m_roi_live.size().y);

    ImGui::SeparatorText("Watched pixels:");

    auto hovered_pixel = int2{pixel_at_app_pos(io.MousePos)};
    if (PixelHeader(ICON_MY_CURSOR_ARROW "##hovered pixel", hovered_pixel))
    {
        static int3 color_mode = {0, 0, 0};
        ImGui::PushID("Current");
        pixel_color_widget(hovered_pixel, color_mode.x, 0);
        ImGui::SetItemTooltip("Hovered pixel values in current channel.");
        ImGui::PopID();

        ImGui::PushID("Reference");
        pixel_color_widget(hovered_pixel, color_mode.y, 1);
        ImGui::SetItemTooltip("Hovered pixel values in reference channel.");
        ImGui::PopID();

        ImGui::PushID("Composite");
        pixel_color_widget(hovered_pixel, color_mode.z, 2);
        ImGui::SetItemTooltip("Hovered pixel values in composite.");
        ImGui::PopID();

        ImGui::Spacing();
    }

    ImGui::Checkbox("Show " ICON_MY_WATCHED_PIXEL "s in viewport", &m_draw_watched_pixels);

    int delete_idx = -1;
    for (int i = 0; i < (int)g_watched_pixels.size(); ++i)
    {
        auto &wp = g_watched_pixels[i];

        ImGui::PushID(i);
        bool visible = true;
        if (PixelHeader(fmt::format("{}{}", ICON_MY_WATCHED_PIXEL, i + 1), wp.pixel, &visible))
        {
            ImGui::PushID("Current");
            pixel_color_widget(wp.pixel, wp.color_mode.x, 0, true);
            ImGui::SetItemTooltip("Pixel %s%d values in current channel.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::PushID("Reference");
            pixel_color_widget(wp.pixel, wp.color_mode.y, 1, true);
            ImGui::SetItemTooltip("Pixel %s%d values in reference channel.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::PushID("Composite");
            pixel_color_widget(wp.pixel, wp.color_mode.z, 2, true);
            ImGui::SetItemTooltip("Pixel %s%d values in composite.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::Spacing();
        }
        ImGui::PopID();

        if (!visible)
            delete_idx = i;
    }
    if (delete_idx >= 0)
        g_watched_pixels.erase(g_watched_pixels.begin() + delete_idx);
}

static void calculate_tree_visibility(LayerTreeNode &node, Image &image)
{
    node.visible_groups = 0;
    node.hidden_groups  = 0;
    if (node.leaf_layer >= 0)
    {
        auto &layer = image.layers[node.leaf_layer];
        for (size_t g = 0; g < layer.groups.size(); ++g)
            if (image.groups[layer.groups[g]].visible)
                ++node.visible_groups;
            else
                ++node.hidden_groups;
    }

    for (auto &[child_name, child_node] : node.children)
    {
        calculate_tree_visibility(child_node, image);
        node.visible_groups += child_node.visible_groups;
        node.hidden_groups += child_node.hidden_groups;
    }
}

void HDRViewApp::update_visibility()
{
    // compute image:channel visibility and update selection indices
    vector<string> visible_image_names;
    for (auto img : m_images)
    {
        const string prefix = img->partname + (img->partname.empty() ? "" : ".");

        // compute visibility of all groups
        img->any_groups_visible = false;
        for (auto &g : img->groups)
        {
            // check if any of the contained channels in the group pass the channel filter
            g.visible = false;
            for (int c = 0; c < g.num_channels && !g.visible; ++c)
                g.visible |= m_channel_filter.PassFilter((prefix + img->channels[g.channels[c]].name).c_str());
            img->any_groups_visible |= g.visible;
        }

        // an image is visible if its filename passes the file filter and it has at least one visible group
        img->visible = m_file_filter.PassFilter(img->filename.c_str()) && img->any_groups_visible;

        if (img->visible)
            visible_image_names.emplace_back(img->file_and_partname());

        calculate_tree_visibility(img->root, *img);

        // if the selected group is hidden, select the next visible group
        if (img->is_valid_group(img->selected_group) && !img->groups[img->selected_group].visible)
        {
            auto old = img->selected_group;
            if ((img->selected_group = img->next_visible_group_index(img->selected_group, Forward)) == old)
                img->selected_group = -1; // no visible groups left
        }

        // if the reference group is hidden, clear it
        // TODO: keep it, but don't display it
        if (img->is_valid_group(img->reference_group) && !img->groups[img->reference_group].visible)
            img->reference_group = -1;
    }

    // go to the next visible image if the current one is hidden
    if (!is_valid(m_current) || !m_images[m_current]->visible)
    {
        auto old = m_current;
        if ((m_current = next_visible_image_index(m_current, Forward)) == old)
            m_current = -1; // no visible images left
    }

    // if the reference is hidden, clear it
    // TODO: keep it, but don't display it
    if (is_valid(m_reference) && !m_images[m_reference]->visible)
        m_reference = -1;

    //
    // compute short (i.e. unique) names for visible images

    // determine common vs. unique parts of visible filenames
    auto [begin_short_offset, end_short_offset] = find_common_prefix_suffix(visible_image_names);
    // we'll add ellipses, so don't shorten if we don't save much space
    if (begin_short_offset <= 4)
        begin_short_offset = 0;
    if (end_short_offset <= 4)
        end_short_offset = 0;
    for (auto img : m_images)
    {
        if (!img->visible)
            continue;

        auto   long_name   = img->file_and_partname();
        size_t short_begin = begin_short_offset;
        size_t short_end   = std::max(long_name.size() - (size_t)end_short_offset, short_begin);

        // Extend beginning and ending of short region to entire word/number
        if (isalnum(long_name[short_begin]))
            while (short_begin > 0 && isalnum(long_name[short_begin - 1])) --short_begin;
        if (isalnum(long_name[short_end - 1]))
            while (short_end < long_name.size() && isalnum(long_name[short_end])) ++short_end;

        // just use the filename if all file paths are identical
        if (short_begin == short_end)
            img->short_name = get_filename(img->file_and_partname());
        else
            img->short_name = long_name.substr(short_begin, short_end - short_begin);

        // add ellipses to indicate where we shortened
        if (short_begin != 0)
            img->short_name = "..." + img->short_name;
        if (short_end != long_name.size())
            img->short_name = img->short_name + "...";
    }

    set_image_textures();
}

void HDRViewApp::draw_file_window()
{
    if (ImGui::BeginCombo("Mode", blend_mode_names()[m_blend_mode].c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (int n = 0; n < NUM_BLEND_MODES; ++n)
        {
            const bool is_selected = (m_blend_mode == n);
            if (ImGui::Selectable(blend_mode_names()[n].c_str(), is_selected))
            {
                m_blend_mode = (EBlendMode)n;
                spdlog::debug("Switching to blend mode {}.", n);
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Channel", channel_names()[m_channel].c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (int n = 0; n < NUM_CHANNELS; ++n)
        {
            const bool is_selected = (m_channel == n);
            if (ImGui::Selectable(channel_names()[n].c_str(), is_selected))
            {
                m_channel = (EChannel)n;
                spdlog::debug("Switching to channel {}.", n);
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // if (!num_images())
    //     return;

    const ImVec2 button_size = ImGui::IconButtonSize();

    bool show_button = m_file_filter.IsActive() || m_channel_filter.IsActive(); // save here to avoid flicker
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2.f * (button_size.x + ImGui::GetStyle().ItemSpacing.x));
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InputTextWithHint("##file filter", ICON_MY_FILTER " Filter 'file pattern:channel pattern'",
                                 g_filter_buffer, IM_ARRAYSIZE(g_filter_buffer)))
    {
        // copy everything before first ':' into m_file_filter.InputBuf, and everything after into
        // m_channel_filter.InputBuf
        if (auto colon = strchr(g_filter_buffer, ':'))
        {
            int file_filter_length    = int(colon - g_filter_buffer + 1);
            int channel_filter_length = IM_ARRAYSIZE(g_filter_buffer) - file_filter_length;
            ImStrncpy(m_file_filter.InputBuf, g_filter_buffer, file_filter_length);
            ImStrncpy(m_channel_filter.InputBuf, colon + 1, channel_filter_length);
        }
        else
        {
            ImStrncpy(m_file_filter.InputBuf, g_filter_buffer, IM_ARRAYSIZE(m_file_filter.InputBuf));
            m_channel_filter.InputBuf[0] = 0; // Clear channel filter if no colon is found
        }

        m_file_filter.Build();
        m_channel_filter.Build();

        update_visibility();
    }
    ImGui::WrappedTooltip(
        "Filter visible images and channel groups.\n\nOnly images with filenames matching the file pattern and "
        "channels matching the channel pattern will be shown. A pattern is a comma-separated list of strings "
        "that must be included or excluded (if prefixed with a '-').");
    if (show_button)
    {
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
        if (ImGui::IconButton(ICON_MY_DELETE))
        {
            m_file_filter.Clear();
            m_channel_filter.Clear();
            g_filter_buffer[0] = 0;
            update_visibility();
        }
    }

    ImGui::SameLine();
    if (ImGui::IconButton(g_short_names ? ICON_MY_SHORT_NAMES "##short names button"
                                        : ICON_MY_FULL_NAMES "##short names button"))
        g_short_names = !g_short_names;
    ImGui::WrappedTooltip(g_short_names ? "Click to show full filenames."
                                        : "Click to show only the unique portion of each file name.");

    static const std::string s_view_mode_icons[] = {ICON_MY_NO_CHANNEL_GROUP, ICON_MY_LIST_VIEW, ICON_MY_TREE_VIEW};

    ImGui::SameLine();
    if (ImGui::BeginComboButton("##channel list mode", s_view_mode_icons[g_file_list_mode].data()))
    {
        if (ImGui::Selectable((s_view_mode_icons[0] + " Only images (do not list channel groups)").c_str(),
                              g_file_list_mode == 0))
            g_file_list_mode = 0;
        if (ImGui::Selectable((s_view_mode_icons[1] + " Flat list of layers and channels").c_str(),
                              g_file_list_mode == 1))
            g_file_list_mode = 1;
        if (ImGui::Selectable((s_view_mode_icons[2] + " Tree view of layers and channels").c_str(),
                              g_file_list_mode == 2))
            g_file_list_mode = 2;

        ImGui::EndComboButton();
    }
    ImGui::WrappedTooltip("Choose how the images and layers are listed below");

    static constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
                                                   ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit |
                                                   ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH |
                                                   ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("ImageList", 2, table_flags,
                          ImVec2(0.f, ImGui::GetContentRegionAvail().y - HelloImGui::EmSize(1.f) -
                                          2.f * ImGui::GetStyle().FramePadding.y - ImGui::GetStyle().ItemSpacing.y)))
    {
        const float icon_width = ImGui::IconSize().x;

        ImGui::TableSetupColumn(ICON_MY_LIST_OL,
                                ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_IndentDisable,
                                1.25f * icon_width);
        // ImGui::TableSetupColumn(ICON_MY_VISIBILITY,
        //                         ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
        //                         icon_width);
        ImGui::TableSetupColumn(g_file_list_mode ? "File:part or channel group" : "File:part.layer.channel group",
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
        ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
        ImGui::TableHeadersRow();

        ImGuiSortDirection direction = ImGuiSortDirection_None;
        if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs())
            if (sort_specs->SpecsCount)
            {
                direction = sort_specs->Specs[0].SortDirection;
                if (sort_specs->SpecsDirty || g_request_sort)
                {
                    spdlog::info("Sorting {}", (int)direction);
                    auto old_current   = current_image();
                    auto old_reference = reference_image();
                    std::sort(m_images.begin(), m_images.end(),
                              [direction](const ImagePtr &a, const ImagePtr &b)
                              {
                                  return (direction == ImGuiSortDirection_Ascending)
                                             ? a->file_and_partname() < b->file_and_partname()
                                             : a->file_and_partname() > b->file_and_partname();
                              });

                    // restore selection
                    if (old_current)
                        m_current = int(std::find(m_images.begin(), m_images.end(), old_current) - m_images.begin());
                    if (old_reference)
                        m_reference =
                            int(std::find(m_images.begin(), m_images.end(), old_reference) - m_images.begin());
                }

                sort_specs->SpecsDirty = g_request_sort = false;
            }

        static ImGuiTreeNodeFlags base_node_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen |
                                                    ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                    ImGuiTreeNodeFlags_OpenOnArrow;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.5f * icon_width);
        int id                 = 0;
        int visible_img_number = 0;
        int hidden_images      = 0;
        int hidden_groups      = 0;

        for (int i = 0; i < num_images(); ++i)
        {
            auto &img          = m_images[i];
            bool  is_current   = m_current == i;
            bool  is_reference = m_reference == i;

            if (!img->visible)
            {
                ++hidden_images;
                continue;
            }

            ++visible_img_number;

            ImGuiTreeNodeFlags node_flags = base_node_flags;

            ImGui::PushFont(g_file_list_mode == 0 ? m_sans_regular : m_sans_bold, ImGui::GetStyle().FontSizeBase);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushRowColors(is_current, is_reference, ImGui::GetIO().KeyShift);
            ImGui::TextAligned(fmt::format("{}", visible_img_number), 1.0f);

            // ImGui::TableNextColumn();
            // auto tmp_pos = ImGui::GetCursorScreenPos();
            // ImGui::TextUnformatted(is_current ? ICON_MY_VISIBILITY : "");
            // ImGui::SetCursorScreenPos(tmp_pos);
            // ImGui::TextUnformatted(is_reference ? ICON_MY_REFERENCE_IMAGE : "");

            ImGui::TableNextColumn();

            if (is_current || is_reference)
                node_flags |= ImGuiTreeNodeFlags_Selected;
            if (g_file_list_mode == 0)
            {
                node_flags |= ImGuiTreeNodeFlags_Leaf;
                ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
            }

            // right-align the truncated file name
            string filename;
            string icon     = img->groups.size() > 1 ? ICON_MY_IMAGES : ICON_MY_IMAGE;
            string ellipsis = " ";
            {
                auto &selected_group =
                    img->groups[(is_reference && !is_current) ? img->reference_group : img->selected_group];
                string group_name =
                    selected_group.num_channels == 1 ? selected_group.name : "(" + selected_group.name + ")";
                auto  &channel    = img->channels[selected_group.channels[0]];
                string layer_path = Channel::head(channel.name);
                filename          = (g_short_names ? img->short_name : img->file_and_partname()) +
                           (g_file_list_mode ? "" : img->delimiter() + layer_path + group_name);

                const float avail_width = ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing();
                while (ImGui::CalcTextSize((icon + ellipsis + filename).c_str()).x > avail_width &&
                       filename.length() > 1)
                {
                    filename = filename.substr(1);
                    ellipsis = " ...";
                }
            }

            // auto item = m_images[i];
            bool open = ImGui::TreeNodeEx((void *)(intptr_t)i, node_flags, "%s", (icon + ellipsis + filename).c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                if (ImGui::GetIO().KeyShift)
                    m_reference = is_reference ? -1 : i;
                else
                    m_current = i;
                set_image_textures();
                spdlog::trace("Setting image {} to the {} image", i, is_reference ? "reference" : "current");
            }

            ImGui::PopStyleColor(3);

            if (direction == ImGuiSortDirection_None)
            {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    // Set payload to carry the index of our item
                    ImGui::SetDragDropPayload("DND_IMAGE", &i, sizeof(int));

                    // Display preview
                    ImGui::TextUnformatted("Move here");
                    if (ImGui::BeginTable("ImageList", 2, table_flags))
                    {
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.25f * icon_width);
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextAligned(fmt::format("{}", visible_img_number), 1.0f);
                        ImGui::TableNextColumn();
                        ImGui::Text(icon + ellipsis + filename);
                        ImGui::EndTable();
                    }
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_IMAGE"))
                    {
                        IM_ASSERT(payload->DataSize == sizeof(int));
                        int payload_i = *(const int *)payload->Data;

                        // move image at payload_i to i, and shift all images in between
                        if (payload_i < i)
                            for (int j = payload_i; j < i; ++j) std::swap(m_images[j], m_images[j + 1]);
                        else
                            for (int j = payload_i; j > i; --j) std::swap(m_images[j], m_images[j - 1]);

                        // maintain the current and reference images
                        if (m_current == payload_i)
                            m_current = i;
                        if (m_reference == payload_i)
                            m_reference = i;
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            if (open)
            {
                ImGui::PushFont(m_sans_regular, 0.f);
                int visible_groups = 1;
                if (g_file_list_mode == 0)
                    ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                else if (g_file_list_mode == 1)
                {
                    visible_groups = img->draw_channel_rows(i, id, is_current, is_reference);
                    MY_ASSERT(visible_groups == img->root.visible_groups,
                              "Unexpected number of visible groups; {} != {}", visible_groups,
                              img->root.visible_groups);
                }
                else
                {
                    visible_groups = img->draw_channel_tree(i, id, is_current, is_reference);
                    MY_ASSERT(visible_groups == img->root.visible_groups,
                              "Unexpected number of visible groups; {} != {}", visible_groups,
                              img->root.visible_groups);
                }

                hidden_groups += (int)img->groups.size() - visible_groups;

                ImGui::PopFont();

                if (open)
                    ImGui::TreePop();
            }

            ImGui::PopFont();
        }

        if (hidden_images || hidden_groups)
        {
            ImGui::BeginDisabled();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // ImGui::TextUnformatted(ICON_MY_VISIBILITY_OFF);
            ImGui::TableNextColumn();
            auto images_str = hidden_images > 1 ? "s" : "";
            auto groups_str = hidden_groups > 1 ? "s" : "";
            if (hidden_groups)
            {
                if (hidden_images)
                    ImGui::TextFmt("{} {} image{} and {} channel group{} hidden", ICON_MY_VISIBILITY_OFF, hidden_images,
                                   images_str, hidden_groups, groups_str);
                else
                    ImGui::TextFmt("{} {} channel group{} hidden", ICON_MY_VISIBILITY_OFF, hidden_groups, groups_str);
            }
            else
                ImGui::TextFmt("{} {} image{} hidden", ICON_MY_VISIBILITY_OFF, hidden_images, images_str);
            ImGui::EndDisabled();
        }
        ImGui::PopStyleVar(2);

        ImGui::EndTable();
    }

    {
        IconButton(action("Play backward"));

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

        IconButton(action("Stop playback"));

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

        IconButton(action("Play forward"));

        ImGui::SameLine();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderFloat("##Playback speed", &g_playback_speed, 0.1f, 60.f, "%.1f fps",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            g_playback_speed = std::clamp(g_playback_speed, 1.f / 20.f, 60.f);
    }
    // ImGui::EndDisabled();
}

void HDRViewApp::fit_display_window()
{
    if (auto img = current_image())
    {
        m_zoom = minelem(viewport_size() / img->display_window.size());
        center();
    }
}

void HDRViewApp::fit_data_window()
{
    if (auto img = current_image())
    {
        m_zoom = minelem(viewport_size() / img->data_window.size());

        auto center_pos   = float2(viewport_size() / 2.f);
        auto center_pixel = Box2f(img->data_window).center();
        reposition_pixel_to_vp_pos(center_pos, center_pixel);
    }
}

void HDRViewApp::fit_selection()
{
    if (current_image() && m_roi.has_volume())
    {
        m_zoom = minelem(viewport_size() / m_roi.size());

        auto center_pos   = float2(viewport_size() / 2.f);
        auto center_pixel = Box2f(m_roi).center();
        reposition_pixel_to_vp_pos(center_pos, center_pixel);
    }
}

float HDRViewApp::zoom_level() const { return log(m_zoom * pixel_ratio()) / log(m_zoom_sensitivity); }

void HDRViewApp::set_zoom_level(float level)
{
    m_zoom = std::clamp(std::pow(m_zoom_sensitivity, level) / pixel_ratio(), MIN_ZOOM, MAX_ZOOM);
}

void HDRViewApp::zoom_at_vp_pos(float amount, float2 focus_vp_pos)
{
    if (amount == 0.f)
        return;

    auto  focused_pixel = pixel_at_vp_pos(focus_vp_pos); // save focused pixel coord before modifying zoom
    float scale_factor  = std::pow(m_zoom_sensitivity, amount);
    m_zoom              = std::clamp(scale_factor * m_zoom, MIN_ZOOM, MAX_ZOOM);
    // reposition so focused_pixel is still under focus_app_pos
    reposition_pixel_to_vp_pos(focus_vp_pos, focused_pixel);
}

void HDRViewApp::zoom_in()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // determine next higher power of 2 zoom level
    float level_for_sensitivity = std::ceil(log(m_zoom) / log(2.f) + 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = std::clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::zoom_out()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // determine next lower power of 2 zoom level
    float level_for_sensitivity = std::floor(log(m_zoom) / log(2.f) - 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = std::clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::reposition_pixel_to_vp_pos(float2 position, float2 pixel)
{
    if (auto img = current_image())
        pixel = select(m_flip, img->display_window.max - pixel - 1, pixel);

    // Calculate where the new offset must be in order to satisfy the image position equation.
    m_translate = position - (pixel * m_zoom) - center_offset();
}

Box2f HDRViewApp::scaled_display_window(ConstImagePtr img) const
{
    Box2f dw = img ? Box2f{img->display_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

Box2f HDRViewApp::scaled_data_window(ConstImagePtr img) const
{
    Box2f dw = img ? Box2f{img->data_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

float HDRViewApp::pixel_ratio() const { return ImGui::GetIO().DisplayFramebufferScale.x; }

float2 HDRViewApp::center_offset() const
{
    auto   dw     = scaled_display_window(current_image());
    float2 offset = (viewport_size() - dw.size()) / 2.f - dw.min;

    // Adjust for flipping: if flipped, offset from the opposite side
    // if (current_image())
    {
        if (m_flip.x)
            offset.x += dw.min.x;
        if (m_flip.y)
            offset.y += dw.min.y;
    }
    return offset;
}

float2 HDRViewApp::image_position(ConstImagePtr img) const
{
    auto   dw  = scaled_data_window(img);
    auto   dsw = scaled_display_window(img);
    float2 pos = m_translate + center_offset() + select(m_flip, dsw.max - dw.min, dw.min);

    // Adjust for flipping: move the image to the opposite side if flipped
    // if (img)
    // {
    //     if (m_flip.x)
    //         pos.x += m_offset.x + center_offset().x + (dsw.max.x - dw.min.x);
    //     if (m_flip.y)
    //         pos.y += m_offset.y + center_offset().y + (dsw.max.y - dw.min.y);
    // }
    return pos / viewport_size();
}

float2 HDRViewApp::image_scale(ConstImagePtr img) const
{
    auto   dw    = scaled_data_window(img);
    float2 scale = dw.size() / viewport_size();

    // Negate scale for flipped axes
    // if (img)
    {
        if (m_flip.x)
            scale.x = -scale.x;
        if (m_flip.y)
            scale.y = -scale.y;
    }
    return scale;
}

void HDRViewApp::draw_pixel_grid() const
{
    if (!current_image())
        return;

    static const int s_grid_threshold = 10;

    if (!m_draw_grid || (s_grid_threshold == -1) || (m_zoom <= s_grid_threshold))
        return;

    float factor = std::clamp((m_zoom - s_grid_threshold) / (2 * s_grid_threshold), 0.f, 1.f);
    float alpha  = lerp(0.0f, 1.0f, smoothstep(0.0f, 1.0f, factor));

    if (alpha <= 0.0f)
        return;

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImColor col_fg(1.0f, 1.0f, 1.0f, alpha);
    ImColor col_bg(0.2f, 0.2f, 0.2f, alpha);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    // draw vertical lines
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_bg, 4.f);

    // draw horizontal lines
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_bg, 4.f);

    // and now again with the foreground color
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_fg, 2.f);
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_fg, 2.f);
}

void HDRViewApp::draw_pixel_info() const
{
    auto img = current_image();
    if (!img || !m_draw_pixel_info)
        return;

    auto ref = reference_image();

    static constexpr float2 align = {0.5f, 0.5f};

    auto  &group = img->groups[img->selected_group];
    string names[4];
    string longest_name;
    for (int c = 0; c < group.num_channels; ++c)
    {
        auto &channel = img->channels[group.channels[c]];
        names[c]      = Channel::tail(channel.name);
        if (names[c].length() > longest_name.length())
            longest_name = names[c];
    }

    ImGui::PushFont(m_mono_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    static float line_height = ImGui::CalcTextSize("").y;
    const float2 channel_threshold2 =
        float2{ImGui::CalcTextSize((longest_name + ": 31.00000").c_str()).x, group.num_channels * line_height};
    const float2 coord_threshold2  = channel_threshold2 + float2{0.f, 2.f * line_height};
    const float  channel_threshold = maxelem(channel_threshold2);
    const float  coord_threshold   = maxelem(coord_threshold2);
    ImGui::PopFont();

    if (m_zoom <= channel_threshold)
        return;

    // fade value for the channel values shown at sufficient zoom
    float factor = std::clamp((m_zoom - channel_threshold) / (1.25f * channel_threshold), 0.f, 1.f);
    float alpha  = smoothstep(0.0f, 1.0f, factor);

    if (alpha <= 0.0f)
        return;

    // fade value for the (x,y) coordinates shown at further zoom
    float factor2 = std::clamp((m_zoom - coord_threshold) / (1.25f * coord_threshold), 0.f, 1.f);
    float alpha2  = smoothstep(0.0f, 1.0f, factor2);

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImGui::PushFont(m_mono_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    for (int y = bounds.min.y; y < bounds.max.y; ++y)
    {
        for (int x = bounds.min.x; x < bounds.max.x; ++x)
        {
            auto   pos        = app_pos_at_pixel(float2(x + 0.5f, y + 0.5f));
            float4 r_pixel    = pixel_value({x, y}, true, 2);
            float4 t_pixel    = linear_to_sRGB(pixel_value({x, y}, false, 2));
            float4 pixel      = g_status_color_mode == 0 ? r_pixel : t_pixel;
            float3 text_color = contrasting_color(t_pixel.xyz());
            float3 shadow     = contrasting_color(text_color);
            if (alpha2 > 0.f)
            {
                float2 c_pos = pos + float2{0.f, (-1 - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("({},{})", x, y);
                ImGui::AddTextAligned(draw_list, c_pos + 1.f, ImColor(float4{shadow, alpha2}), text, align);
                ImGui::AddTextAligned(draw_list, c_pos, ImColor(float4{text_color, alpha2}), text, align);
            }

            for (int c = 0; c < group.num_channels; ++c)
            {
                float2 c_pos = pos + float2{0.f, (c - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("{:>2s}:{: > 9.5f}", names[c], pixel[c]);
                ImGui::AddTextAligned(draw_list, c_pos + 1.f, ImColor(float4{shadow, alpha2}), text, align);
                ImGui::AddTextAligned(draw_list, c_pos, ImColor{float4{text_color, alpha2}}, text, align);
            }
        }
    }
    ImGui::PopFont();
}

void HDRViewApp::draw_image_border() const
{
    auto draw_list = ImGui::GetBackgroundDrawList();

    auto cimg = current_image();
    auto rimg = reference_image();

    if (!cimg && !rimg)
        return;

    if (cimg && cimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{cimg->data_window.min}), app_pos_at_pixel(float2{cimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{cimg->display_window.min}),
                                    app_pos_at_pixel(float2{cimg->display_window.max})}
                                  .make_valid();
        bool non_trivial = cimg->data_window != cimg->display_window || cimg->data_window.min != int2{0, 0};
        ImGui::PushRowColors(true, false);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive), "Data window",
                                   {0.f, 0.f}, non_trivial);
        if (m_draw_display_window && non_trivial)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header), "Display window",
                                   {1.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (rimg && rimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{rimg->data_window.min}), app_pos_at_pixel(float2{rimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{rimg->display_window.min}),
                                    app_pos_at_pixel(float2{rimg->display_window.max})}
                                  .make_valid();
        ImGui::PushRowColors(false, true, true);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive),
                                   "Reference data window", {1.f, 0.f}, true);
        if (m_draw_display_window)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header),
                                   "Reference display window", {0.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (m_roi_live.has_volume())
    {
        Box2f crop_window{app_pos_at_pixel(float2{m_roi_live.min}), app_pos_at_pixel(float2{m_roi_live.max})};
        ImGui::DrawLabeledRect(draw_list, crop_window, ImGui::ColorConvertFloat4ToU32(float4{float3{0.5f}, 1.f}),
                               "Selection", {0.5f, 1.f}, true);
    }
}

void HDRViewApp::draw_watched_pixels() const
{
    if (!current_image() || !m_draw_watched_pixels)
        return;

    auto draw_list = ImGui::GetBackgroundDrawList();

    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase);
    for (int i = 0; i < (int)g_watched_pixels.size(); ++i)
        ImGui::DrawCrosshairs(draw_list, app_pos_at_pixel(g_watched_pixels[i].pixel + 0.5f), fmt::format(" {}", i + 1));
    ImGui::PopFont();
}

void HDRViewApp::draw_image() const
{
    auto set_color = [this](Target target, ConstImagePtr img)
    {
        auto t = target_name(target);
        if (img)
        {
            int                 group_idx = target == Target_Primary ? img->selected_group : img->reference_group;
            const ChannelGroup &group     = img->groups[group_idx];

            // FIXME: tried to pass this as a 3x3 matrix, but the data was somehow not being passed properly to MSL.
            // resulted in rapid flickering. So, for now, just pad the 3x3 matrix into a 4x4 one.
            m_shader->set_uniform(fmt::format("{}_M_to_sRGB", t), float4x4{{img->M_to_sRGB[0], 0.f},
                                                                           {img->M_to_sRGB[1], 0.f},
                                                                           {img->M_to_sRGB[2], 0.f},
                                                                           {0.f, 0.f, 0.f, 1.f}});
            m_shader->set_uniform(fmt::format("{}_channels_type", t), (int)group.type);
            m_shader->set_uniform(fmt::format("{}_yw", t), img->luminance_weights);
        }
        else
        {
            m_shader->set_uniform(fmt::format("{}_M_to_sRGB", t), float4x4{la::identity});
            m_shader->set_uniform(fmt::format("{}_channels_type", t), (int)ChannelGroup::Single_Channel);
            m_shader->set_uniform(fmt::format("{}_yw", t), sRGB_Yw());
        }
    };

    set_color(Target_Primary, current_image());
    set_color(Target_Secondary, reference_image());

    if (current_image() && !current_image()->data_window.is_empty())
    {
        float2 randomness(std::generate_canonical<float, 10>(g_rand) * 255,
                          std::generate_canonical<float, 10>(g_rand) * 255);

        m_shader->set_uniform("time", (float)ImGui::GetTime());
        m_shader->set_uniform("draw_clip_warnings", m_draw_clip_warnings);
        m_shader->set_uniform("clip_range", m_clip_range);
        m_shader->set_uniform("randomness", randomness);
        m_shader->set_uniform("gain", powf(2.0f, m_exposure_live));
        m_shader->set_uniform("offset", m_offset_live);
        m_shader->set_uniform("gamma", m_gamma_live);
        m_shader->set_uniform("tonemap_mode", (int)m_tonemap);
        m_shader->set_uniform("clamp_to_LDR", m_clamp_to_LDR);
        m_shader->set_uniform("do_dither", m_dither);

        m_shader->set_uniform("primary_pos", image_position(current_image()));
        m_shader->set_uniform("primary_scale", image_scale(current_image()));

        m_shader->set_uniform("blend_mode", (int)m_blend_mode);
        m_shader->set_uniform("channel", (int)m_channel);
        m_shader->set_uniform("bg_mode", (int)m_bg_mode);
        m_shader->set_uniform("bg_color", m_bg_color);

        m_shader->set_texture("colormap", Colormap::texture(m_colormaps[m_colormap_index]));
        m_shader->set_uniform("reverse_colormap", g_reverse_colormap);

        if (reference_image())
        {
            m_shader->set_uniform("has_reference", true);
            m_shader->set_uniform("secondary_pos", image_position(reference_image()));
            m_shader->set_uniform("secondary_scale", image_scale(reference_image()));
        }
        else
        {
            m_shader->set_uniform("has_reference", false);
            m_shader->set_uniform("secondary_pos", float2{0.f});
            m_shader->set_uniform("secondary_scale", float2{1.f});
        }

        m_shader->begin();
        m_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
        m_shader->end();
    }

    // ImGui::Begin("Texture window");
    // ImGui::Image((ImTextureID)(intptr_t)Colormap::texture(m_colormap)->texture_handle(),
    //              ImGui::GetContentRegionAvail());
    // ImGui::End();
}

void HDRViewApp::draw_top_toolbar()
{
    auto img = current_image();

    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    ImGui::TextUnformatted(ICON_MY_EXPOSURE);
    ImGui::PopFont();
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(HelloImGui::EmSize(8));
    ImGui::SliderFloat("##ExposureSlider", &m_exposure_live, -9.f, 9.f, "Exposure: %+5.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_exposure = m_exposure_live;
    ImGui::EndGroup();
    ImGui::WrappedTooltip("Increasing (Shift+E) or decreasing (e) the exposure. The displayed brightness of "
                          "the image is multiplied by 2^exposure.");

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    ImGui::TextUnformatted(ICON_MY_OFFSET);
    ImGui::PopFont();
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(HelloImGui::EmSize(8));
    ImGui::SliderFloat("##OffsetSlider", &m_offset_live, -1.f, 1.f, "Offset: %+1.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_offset = m_offset_live;
    ImGui::EndGroup();
    ImGui::WrappedTooltip(
        "Increase/decrease the blackpoint offset. The offset is added to the pixel value after exposure is applied.");

    ImGui::SameLine();

    IconButton(action("Normalize exposure"));

    ImGui::SameLine();

    IconButton(action("Reset tonemapping"));

    ImGui::SameLine();

    ImGui::SetNextItemWidth(HelloImGui::EmSize(7.5));
    ImGui::Combo("##Tonemapping", (int *)&m_tonemap, "Gamma\0Colormap (+)\0Colormap (±)\0");
    ImGui::WrappedTooltip(
        "Set the tonemapping mode, which is applied to the pixel values after exposure and blackpoint offset.\n\n"
        "Gamma: Raise the pixel values to this exponent before display.\n"
        "Colormap (+): Falsecolor with colormap range set to [0,1].\n"
        "Colormap (±): Falsecolor with colormap range set to [-1,+1] (choosing a diverging "
        "colormap like IceFire can help visualize positive/negative values).");

    switch (m_tonemap)
    {
    default: [[fallthrough]];
    case Tonemap_Gamma:
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(HelloImGui::EmSize(8));
        ImGui::SliderFloat("##GammaSlider", &m_gamma_live, 0.02f, 9.f, "Gamma: %5.3f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            m_gamma = m_gamma_live;
        ImGui::SetItemTooltip("Set the exponent for gamma correction.");
    }
    break;
    case Tonemap_FalseColor: [[fallthrough]];
    case Tonemap_PositiveNegative:
    {
        ImGui::SameLine();
        auto p = ImGui::GetCursorScreenPos();

        if (ImPlot::ColormapButton(
                Colormap::name(m_colormaps[m_colormap_index]),
                ImVec2(HelloImGui::EmSize(8) - ImGui::IconButtonSize().x - ImGui::GetStyle().ItemInnerSpacing.x,
                       ImGui::GetFrameHeight()),
                m_colormaps[m_colormap_index]))
            ImGui::OpenPopup("colormap_dropdown");
        ImGui::SetItemTooltip("Click to choose a colormap.");

        ImGui::SetNextWindowPos(ImVec2{p.x, p.y + ImGui::GetFrameHeight()});
        ImGui::PushStyleVarX(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().FramePadding.x);
        auto tmp = ImGui::BeginPopup("colormap_dropdown");
        ImGui::PopStyleVar();
        if (tmp)
        {
            for (int n = 0; n < (int)m_colormaps.size(); n++)
            {
                const bool is_selected = (m_colormap_index == n);
                if (ImGui::Selectable((string("##") + Colormap::name(m_colormaps[n])).c_str(), is_selected, 0,
                                      ImVec2(0, ImGui::GetFrameHeight())))
                    m_colormap_index = n;
                ImGui::SameLine(0.f, 0.f);

                ImPlot::ColormapButton(
                    Colormap::name(m_colormaps[n]),
                    ImVec2(HelloImGui::EmSize(8) - ImGui::GetStyle().WindowPadding.x, ImGui::GetFrameHeight()),
                    m_colormaps[n]);

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        IconButton(action("Reverse colormap"));
    }
    break;
    }
    ImGui::SameLine();

    if (m_params.rendererBackendOptions.requestFloatBuffer)
    {
        IconButton(action("Clamp to LDR"));
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    }

    IconButton(action("Draw pixel grid"));
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    IconButton(action("Draw pixel values"));
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
}

void HDRViewApp::draw_background()
{
    using namespace std::literals;
    spdlog::mdc::put(" f", to_string(ImGui::GetFrameCount()));

    static auto prev_frame = std::chrono::steady_clock::now();
    auto        this_frame = std::chrono::steady_clock::now();

    if ((g_play_forward || g_play_backward) &&
        this_frame - prev_frame >= std::chrono::milliseconds(int(1000 / g_playback_speed)))
    {
        set_current_image_index(next_visible_image_index(m_current, g_play_forward ? Forward : Backward));
        set_image_textures();
        prev_frame = this_frame;
    }

    process_shortcuts();

    // If watching files for changes, do so every 250ms
    if (m_watch_files_for_changes)
    {
        if (this_frame - m_last_file_changes_check_time >= 250ms)
        {
            reload_modified_files();
            m_image_loader.load_new_files();
            m_last_file_changes_check_time = this_frame;
        }
    }

    try
    {
        auto &io = ImGui::GetIO();

        //
        // calculate the viewport sizes
        // fbsize is the size of the window in physical pixels while accounting for dpi factor on retina
        // screens. For retina displays, io.DisplaySize is the size of the window in logical pixels so we it by
        // io.DisplayFramebufferScale to get the physical pixel size for the framebuffer.
        float2 fbscale = io.DisplayFramebufferScale;
        int2   fbsize  = int2{float2{io.DisplaySize} * fbscale};
        spdlog::trace("DisplayFramebufferScale: {}, DpiWindowSizeFactor: {}, DpiFontLoadingFactor: {}",
                      float2{io.DisplayFramebufferScale}, HelloImGui::DpiWindowSizeFactor(),
                      HelloImGui::DpiFontLoadingFactor());
        m_viewport_min  = {0.f, 0.f};
        m_viewport_size = io.DisplaySize;
        if (auto id = m_params.dockingParams.dockSpaceIdFromName("MainDockSpace"))
            if (auto central_node = ImGui::DockBuilderGetCentralNode(*id))
            {
                m_viewport_size = central_node->Size;
                m_viewport_min  = central_node->Pos;
            }

        if (!io.WantCaptureMouse && current_image())
        {
            auto vp_mouse_pos   = vp_pos_at_app_pos(io.MousePos);
            bool cancel_autofit = false;
            auto scroll         = float2{io.MouseWheelH, io.MouseWheel} * g_scroll_multiplier;

            if (length2(scroll) > 0.f)
            {
                cancel_autofit = true;
                if (ImGui::IsKeyDown(ImGuiMod_Shift))
                    // panning
                    reposition_pixel_to_vp_pos(vp_mouse_pos + scroll * 4.f, pixel_at_vp_pos(vp_mouse_pos));
                else
                    zoom_at_vp_pos(scroll.y / 4.f, vp_mouse_pos);
            }

            if (g_mouse_mode == MouseMode_RectangularSelection)
            {
                // set m_roi based on dragged region
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    m_roi_live = Box2i{int2{0}};
                else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    m_roi_live.make_empty();
                    m_roi_live.enclose(int2{pixel_at_app_pos(io.MouseClickedPos[0])});
                    m_roi_live.enclose(int2{pixel_at_app_pos(io.MousePos)});
                }
                else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    m_roi = m_roi_live;
            }
            else if (g_mouse_mode == MouseMode_ColorInspector)
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    // add watched pixel
                    g_watched_pixels.emplace_back(WatchedPixel{int2{pixel_at_app_pos(io.MousePos)}});
                else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    if (g_watched_pixels.size())
                        g_watched_pixels.back().pixel = int2{pixel_at_app_pos(io.MousePos)};
                }
            }
            else
            {
                float2 drag_delta{ImGui::GetMouseDragDelta(ImGuiMouseButton_Left)};
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    cancel_autofit = true;
                    reposition_pixel_to_vp_pos(vp_mouse_pos + drag_delta, pixel_at_vp_pos(vp_mouse_pos));
                    ImGui::ResetMouseDragDelta();
                }
            }

            if (cancel_autofit)
                this->cancel_autofit();
        }

        //
        // clear the framebuffer and set up the viewport
        //

        // RenderPass expects things in framebuffer coordinates
        m_render_pass->resize(fbsize);
        m_render_pass->set_viewport(int2(m_viewport_min * fbscale), int2(m_viewport_size * fbscale));

        if (m_auto_fit_display)
            fit_display_window();
        if (m_auto_fit_data)
            fit_data_window();
        if (m_auto_fit_selection)
            fit_selection();

        m_render_pass->begin();
        draw_image();
        m_render_pass->end();

        draw_pixel_info();
        draw_pixel_grid();
        draw_image_border();
        draw_watched_pixels();

        if (current_image())
        {
            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 18.f / 14.f);
            auto   draw_list = ImGui::GetBackgroundDrawList();
            float2 pos       = ImGui::GetIO().MousePos;
            if (g_mouse_mode == MouseMode_RectangularSelection)
            {
                // draw selection indicator
                ImGui::AddTextAligned(draw_list, pos + int2{18} + int2{1, 1}, IM_COL32_BLACK, ICON_MY_SELECT,
                                      {0.5f, 0.5f});
                ImGui::AddTextAligned(draw_list, pos + int2{18}, IM_COL32_WHITE, ICON_MY_SELECT, {0.5f, 0.5f});
            }
            else if (g_mouse_mode == MouseMode_ColorInspector)
            {
                // draw pixel watcher indicator
                ImGui::DrawCrosshairs(draw_list, pos + int2{18}, " +");
            }
            // else if (g_mouse_mode == MouseMode_PanZoom)
            // {
            //     // draw pixel watcher indicator
            //     ImGui::AddTextAligned(draw_list, pos + int2{18} + int2{1, 1}, IM_COL32_BLACK, ICON_MY_PAN_ZOOM_TOOL,
            //                           {0.5f, 0.5f});
            //     ImGui::AddTextAligned(draw_list, pos + int2{18}, IM_COL32_WHITE, ICON_MY_PAN_ZOOM_TOOL, {0.5f,
            //     0.5f});
            // }
            ImGui::PopFont();
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("Drawing failed:\n\t{}.", e.what());
    }
}

int HDRViewApp::next_visible_image_index(int index, EDirection direction) const
{
    return next_matching_index(m_images, index, [](size_t, const ImagePtr &img) { return img->visible; }, direction);
}

int HDRViewApp::nth_visible_image_index(int n) const
{
    return (int)nth_matching_index(m_images, (size_t)n, [](size_t, const ImagePtr &img) { return img->visible; });
}

int HDRViewApp::image_index(ConstImagePtr img) const
{
    for (int i = 0; i < num_images(); ++i)
        if (m_images[i] == img)
            return i;
    return -1; // not found
}

bool HDRViewApp::process_event(void *e)
{
#ifdef HELLOIMGUI_USE_SDL2
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return false;

    SDL_Event *event = static_cast<SDL_Event *>(e);
    switch (event->type)
    {
    case SDL_QUIT: spdlog::trace("Got an SDL_QUIT event"); break;
    case SDL_WINDOWEVENT: spdlog::trace("Got an SDL_WINDOWEVENT event"); break;
    case SDL_MOUSEWHEEL: spdlog::trace("Got an SDL_MOUSEWHEEL event"); break;
    case SDL_MOUSEMOTION: spdlog::trace("Got an SDL_MOUSEMOTION event"); break;
    case SDL_MOUSEBUTTONDOWN: spdlog::trace("Got an SDL_MOUSEBUTTONDOWN event"); break;
    case SDL_MOUSEBUTTONUP: spdlog::trace("Got an SDL_MOUSEBUTTONUP event"); break;
    case SDL_FINGERMOTION: spdlog::trace("Got an SDL_FINGERMOTION event"); break;
    case SDL_FINGERDOWN: spdlog::trace("Got an SDL_FINGERDOWN event"); break;
    case SDL_MULTIGESTURE:
    {
        spdlog::trace("Got an SDL_MULTIGESTURE event; numFingers: {}; dDist: {}; x: {}, y: {}; io.MousePos: {}, {}; "
                      "io.MousePosFrac: {}, {}",
                      event->mgesture.numFingers, event->mgesture.dDist, event->mgesture.x, event->mgesture.y,
                      io.MousePos.x, io.MousePos.y, io.MousePos.x / io.DisplaySize.x, io.MousePos.y / io.DisplaySize.y);
        constexpr float cPinchZoomThreshold(0.0001f);
        constexpr float cPinchScale(80.0f);
        if (event->mgesture.numFingers == 2 && fabs(event->mgesture.dDist) >= cPinchZoomThreshold)
        {
            // Zoom in/out by positive/negative mPinch distance
            zoom_at_vp_pos(event->mgesture.dDist * cPinchScale, vp_pos_at_app_pos(io.MousePos));
            return true;
        }
    }
    break;
    case SDL_FINGERUP: spdlog::trace("Got an SDL_FINGERUP event"); break;
    }
#endif
    (void)e; // prevent unreferenced formal parameter warning
    return false;
}

void HDRViewApp::draw_command_palette()
{
    if (g_show_command_palette)
        ImGui::OpenPopup("Command palette...");

    g_show_command_palette = false;

    auto &io = ImGui::GetIO();

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowSize(ImVec2{400, 0}, ImGuiCond_Always);

    float remaining_height            = ImGui::GetMainViewport()->Size.y - ImGui::GetCursorScreenPos().y;
    float search_result_window_height = remaining_height - 2.f * HelloImGui::EmSize();

    // Set constraints to allow horizontal resizing based on content
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0, 0), ImVec2(io.DisplaySize.x - 2.f * HelloImGui::EmSize(), search_result_window_height));

    if (ImGui::BeginPopup("Command palette...", ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::IsWindowAppearing())
        {
            spdlog::trace("Creating ImCmd context");
            if (ImCmd::GetCurrentContext())
            {
                ImCmd::RemoveAllCaches();
                ImCmd::DestroyContext();
            }
            ImCmd::CreateContext();
            ImCmd::SetStyleFont(ImCmdTextType_Regular, m_sans_regular);
            ImCmd::SetStyleFont(ImCmdTextType_Highlight, m_sans_bold);
            ImCmd::SetStyleFlag(ImCmdTextType_Highlight, ImCmdTextFlag_Underline, true);
            // ImVec4 highlight_font_color(1.0f, 1.0f, 1.0f, 1.0f);
            auto highlight_font_color = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            ImCmd::SetStyleColor(ImCmdTextType_Highlight, ImGui::ColorConvertFloat4ToU32(highlight_font_color));

            for (auto &a : m_actions)
            {
                if (a.enabled())
                    ImCmd::AddCommand({a.name, a.p_selected ? [&a](){
                *a.p_selected = !*a.p_selected;a.callback();} : a.callback, nullptr, nullptr, a.icon, ImGui::GetKeyChordNameTranslated(a.chord), a.p_selected});
            }

#if !defined(__EMSCRIPTEN__)
            // add a two-step command to list and open recent files
            if (!m_image_loader.recent_files().empty())
                ImCmd::AddCommand({"Open recent",
                                   [this]()
                                   {
                                       ImCmd::Prompt(m_image_loader.recent_files_short());
                                       ImCmd::SetNextCommandPaletteSearchBoxFocused();
                                   },
                                   [this](int selected_option) { m_image_loader.load_recent_file(selected_option); },
                                   nullptr, ICON_MY_OPEN_IMAGE});

#endif

            // set logging verbosity. This is a two-step command
            ImCmd::AddCommand({"Set logging verbosity",
                               []()
                               {
                                   ImCmd::Prompt(std::vector<std::string>{"0: trace", "1: debug", "2: info", "3: warn",
                                                                          "4: err", "5: critical", "6: off"});
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [](int selected_option)
                               {
                                   ImGui::GlobalSpdLogWindow().sink()->set_level(
                                       spdlog::level::level_enum(selected_option));
                                   spdlog::info("Setting verbosity threshold to level {:d}.", selected_option);
                               },
                               nullptr, ICON_MY_LOG_LEVEL});

            // set background color. This is a two-step command
            ImCmd::AddCommand({"Set background color",
                               []()
                               {
                                   ImCmd::Prompt(std::vector<std::string>{"0: black", "1: white", "2: dark checker",
                                                                          "3: light checker", "4: custom..."});
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [this](int selected_option)
                               {
                                   m_bg_mode =
                                       (EBGMode)std::clamp(selected_option, (int)BG_BLACK, (int)NUM_BG_MODES - 1);
                                   if (m_bg_mode == BG_CUSTOM_COLOR)
                                       g_show_bg_color_picker = true;
                               },
                               nullptr, g_blank_icon});

            // add two-step theme selection command
            ImCmd::AddCommand({"Set theme",
                               []()
                               {
                                   vector<string> theme_names;
                                   theme_names.push_back("HDRView dark");
                                   for (int i = 0; i < ImGuiTheme::ImGuiTheme_Count; ++i)
                                       theme_names.push_back(ImGuiTheme::ImGuiTheme_Name((ImGuiTheme::ImGuiTheme_)(i)));

                                   ImCmd::Prompt(theme_names);
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [](int selected_option) { apply_theme(selected_option - 1); }, nullptr, ICON_MY_THEME});

            ImCmd::SetNextCommandPaletteSearchBoxFocused();
            ImCmd::SetNextCommandPaletteSearch("");
        }

        // ImCmd::SetNextCommandPaletteSearchBoxFocused(); // always focus the search box
        ImCmd::CommandPalette("Command palette", "Filter commands...");

        if (ImCmd::IsAnyItemSelected() || ImGui::GlobalShortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteOverActive) ||
            ImGui::GlobalShortcut(ImGuiMod_Ctrl | ImGuiKey_Period, ImGuiInputFlags_RouteOverActive))
        {
            // Close window when user selects an item, hits escape, or unfocuses the command palette window
            // (clicking elsewhere)
            ImGui::CloseCurrentPopup();
            g_show_command_palette = false;
        }

        if (ImGui::BeginTable("PaletteHelp", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ContextMenuInBody))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::ScopedFont sf{m_sans_bold, 10};

            string txt;
            txt = "Navigate (" ICON_MY_ARROW_UP ICON_MY_ARROW_DOWN ")";
            ImGui::TableNextColumn();
            ImGui::TextAligned(txt, 0.f);

            ImGui::TableNextColumn();
            txt = "Use (" ICON_MY_KEY_RETURN ")";
            ImGui::TextAligned(txt, 0.5f);

            ImGui::TableNextColumn();
            txt = "Dismiss (" ICON_MY_KEY_ESC ")";
            ImGui::TextAligned(txt, 1.f);

            ImGui::PopStyleColor();

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::process_shortcuts()
{
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        spdlog::trace("Not processing shortcuts because ImGui wants to capture the keyboard");
        return;
    }

    // spdlog::trace("Processing shortcuts (frame: {})", ImGui::GetFrameCount());

    for (auto &a : m_actions)
        if (a.chord)
            if (a.enabled() && ImGui::GlobalShortcut(a.chord, a.flags))
            {
                spdlog::trace("Processing shortcut for action '{}' (frame: {})", a.name, ImGui::GetFrameCount());
                if (a.p_selected)
                    *a.p_selected = !*a.p_selected;
                a.callback();
#ifdef __EMSCRIPTEN__
                ImGui::GetIO().ClearInputKeys(); // FIXME: somehow needed in emscripten, otherwise the key (without
                                                 // modifiers) needs to be pressed before this chord is detected again
#endif
                break;
            }

    set_image_textures();
}

void HDRViewApp::draw_about_dialog()
{
    if (g_show_help)
        ImGui::OpenPopup("About");

    // work around HelloImGui rendering a couple frames to figure out sizes
    if (ImGui::GetFrameCount() > 1)
        g_show_help = false;

    auto &io = ImGui::GetIO();

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowFocus();
    constexpr float icon_size = 128.f;
    float2          col_width = {icon_size + HelloImGui::EmSize(), 32 * HelloImGui::EmSize()};
    col_width[1]              = std::clamp(col_width[1], 5 * HelloImGui::EmSize(),
                                           io.DisplaySize.x - ImGui::GetStyle().WindowPadding.x -
                                               2 * ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().ScrollbarSize - col_width[0]);

    ImGui::SetNextWindowContentSize(float2{col_width[0] + col_width[1] + ImGui::GetStyle().ItemSpacing.x, 0});

    if (ImGui::BeginPopup("About", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_AlwaysAutoResize))
    {
        g_help_is_open = true;
        ImGui::Spacing();

        auto platform_backend = [](HelloImGui::PlatformBackendType type)
        {
            using T = HelloImGui::PlatformBackendType;
            switch (type)
            {
            case T::FirstAvailable: return "FirstAvailable";
            case T::Glfw: return "GLFW 3";
            case T::Sdl: return "SDL 2";
            case T::Null: return "Null";
            default: return "Unknown";
            }
        }(m_params.platformBackendType);

        auto renderer_backend = [](HelloImGui::RendererBackendType type)
        {
            using T = HelloImGui::RendererBackendType;
            switch (type)
            {
            case T::FirstAvailable: return "FirstAvailable";
            case T::OpenGL3: return "OpenGL3";
            case T::Metal: return "Metal";
            case T::Vulkan: return "Vulkan";
            case T::DirectX11: return "DirectX11";
            case T::DirectX12: return "DirectX12";
            case T::Null: return "Null";
            default: return "Unknown";
            }
        }(m_params.rendererBackendType);

        if (ImGui::BeginTable("about_table1", 2))
        {
            ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
            ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // right align the image
            ImGui::AlignCursor(icon_size + 0.5f * HelloImGui::EmSize(), 1.f);
            HelloImGui::ImageFromAsset("app_settings/icon-256.png", {icon_size, icon_size}); // show the app icon

            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1]);

            {
                ImGui::ScopedFont sf{m_sans_bold, 30};
                ImGui::HyperlinkText("HDRView", "https://github.com/wkjarosz/hdrview");
            }

            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 18.f / 14.f);
            ImGui::TextUnformatted(version());
            ImGui::PopFont();
            ImGui::PushFont(m_sans_regular, ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
            ImGui::TextFmt("Built on {} using the {} backend with {}.", build_timestamp(), platform_backend,
                           renderer_backend);
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
            ImGui::TextUnformatted(
                "HDRView is a simple research-oriented tool for examining, comparing, manipulating, and "
                "converting high-dynamic range images.");
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::TextUnformatted(
                "It is developed by Wojciech Jarosz, and is available under a 3-clause BSD license.");

            ImGui::PopTextWrapPos();
            ImGui::EndTable();
        }

        auto item_and_description = [this, col_width](const char *name, const char *desc, const char *url = nullptr)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::AlignCursor(name, 1.f);
            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase);
            ImGui::HyperlinkText(name, url);
            ImGui::PopFont();
            ImGui::TableNextColumn();

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1] - HelloImGui::EmSize());
            ImGui::PushFont(m_sans_regular, ImGui::GetStyle().FontSizeBase);
            ImGui::TextUnformatted(desc);
            ImGui::PopFont();
        };

        if (ImGui::BeginTabBar("AboutTabBar"))
        {
            if (ImGui::BeginTabItem("Keybindings", nullptr))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);

                ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase);
                ImGui::TextAligned("The main keyboard shortcut to remember is:", 0.5f);
                ImGui::PopFont();

                ImGui::PushFont(font("mono regular"), ImGui::GetStyle().FontSizeBase * 30.f / 14.f);
                ImGui::TextAligned(ImGui::GetKeyChordNameTranslated(action("Command palette...").chord), 0.5f);
                ImGui::PopFont();

                ImGui::TextUnformatted("This opens the command palette, which lists every available HDRView command "
                                       "along with its keyboard shortcuts (if any).");
                ImGui::Spacing();

                ImGui::TextUnformatted("Many commands and their keyboard shortcuts are also listed in the menu bar.");

                ImGui::TextUnformatted(
                    "Additonally, left-click+drag will pan the image view, and scrolling the mouse/pinching "
                    "will zoom in and out.");

                ImGui::Spacing();
                ImGui::PopTextWrapPos();

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Credits"))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);
                ImGui::TextUnformatted(
                    "HDRView additionally makes use of the following external libraries and techniques (in "
                    "alphabetical order):\n\n");
                ImGui::PopTextWrapPos();

                if (ImGui::BeginTable("about_table2", 2))
                {
                    ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
                    ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

                    item_and_description("Dear ImGui", "Omar Cornut's immediate-mode graphical user interface for C++.",
                                         "https://github.com/ocornut/imgui");
#ifdef HDRVIEW_ENABLE_UHDR
                    item_and_description("libuhdr", "For loading Ultra HDR JPEG files.",
                                         "https://github.com/google/libultrahdr");
#endif
#ifdef HDRVIEW_ENABLE_JPEGXL
                    item_and_description("libjxl", "For loading JPEG-XL files.", "https://github.com/libjxl/libjxl");
#endif
#ifdef HDRVIEW_ENABLE_HEIF
                    item_and_description("libheif", "For loading HEIF, HEIC, AVIF, and AVIFS files.",
                                         "https://github.com/strukturag/libheif");
#endif
#ifdef HDRVIEW_ENABLE_LIBPNG
                    item_and_description("libpng", "For loading PNG files.", "https://github.com/pnggroup/libpng");
#endif
#ifdef HDRVIEW_ENABLE_LCMS2
                    item_and_description("lcms2", "LittleCMS color management engine.",
                                         "https://github.com/mm2/Little-CMS");
#endif
#ifdef __EMSCRIPTEN__
                    item_and_description("emscripten", "An MIT-licensed LLVM-to-WebAssembly compiler.",
                                         "https://github.com/emscripten-core/emscripten");
                    item_and_description("emscripten-browser-file",
                                         "Armchair Software's MIT-licensed header-only C++ library "
                                         "to open and save files in the browser.",
                                         "https://github.com/Armchair-Software/emscripten-browser-file");
#endif
                    item_and_description("{fmt}", "A modern formatting library.", "https://github.com/fmtlib/fmt");
                    item_and_description("Hello ImGui", "Pascal Thomet's cross-platform starter-kit for Dear ImGui.",
                                         "https://github.com/pthom/hello_imgui");
                    item_and_description(
                        "linalg", "Sterling Orsten's public domain, single header short vector math library for C++.",
                        "https://github.com/sgorsten/linalg");
                    item_and_description("NanoGUI", "Bits of code from Wenzel Jakob's BSD-licensed NanoGUI library.",
                                         "https://github.com/mitsuba-renderer/nanogui");
                    item_and_description("OpenEXR", "High Dynamic-Range (HDR) image file format.",
                                         "https://github.com/AcademySoftwareFoundation/openexr");
#ifndef __EMSCRIPTEN__
                    item_and_description("portable-file-dialogs",
                                         "Sam Hocevar's WTFPL portable GUI dialogs library, C++11, single-header.",
                                         "https://github.com/samhocevar/portable-file-dialogs");
#endif
                    item_and_description("smalldds", "Single-header library for loading DDS images.",
                                         "https://github.com/wkjarosz/smalldds");
                    item_and_description("stb_image/write", "Single-header libraries for loading/writing images.",
                                         "https://github.com/nothings/stb");
                    item_and_description("tev", "Some code is adapted from Thomas Müller's tev.",
                                         "https://github.com/Tom94/tev");
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Build info"))
            {
                ImGui::PushFont(m_mono_regular, 0.f);
                ImVec2 child_size = ImVec2(0, HelloImGui::EmSize(12.f));
                ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_FrameStyle);

                ImGui::Text("ImGui version: %s", ImGui::GetVersion());

                ImGui::Text("EDR support: %s", HelloImGui::hasEdrSupport() ? "yes" : "no");
#ifdef __EMSCRIPTEN__
                ImGui::Text("define: __EMSCRIPTEN__");
#endif
#ifdef ASSETS_LOCATION
                ImGui::Text("ASSETS_LOCATION:"##ASSETS_LOCATION);
#endif
#ifdef HDRVIEW_ICONSET_FA6
                ImGui::Text("HDRVIEW_ICONSET: Font Awesome 6");
#elif defined(HDRVIEW_ICONSET_LC)
                ImGui::Text("HDRVIEW_ICONSET: Lucide Icons");
#elif defined(HDRVIEW_ICONSET_MS)
                ImGui::Text("HDRVIEW_ICONSET: Material Symbols");
#elif defined(HDRVIEW_ICONSET_MD)
                ImGui::Text("HDRVIEW_ICONSET: Material Design");
#elif defined(HDRVIEW_ICONSET_MDI)
                ImGui::Text("HDRVIEW_ICONSET: Material Design Icons");
#endif

                ImGui::Text("Image IO libraries:");
#ifdef HDRVIEW_ENABLE_UHDR
                ImGui::Text("\tlibuhdr: yes");
#else
                ImGui::Text("\tlibuhdr: no");
#endif
#ifdef HDRVIEW_ENABLE_JPEGXL
                ImGui::Text("\tlibjxl:  yes");
#else
                ImGui::Text("\tlibjxl:  no");
#endif
#ifdef HDRVIEW_ENABLE_HEIF
                ImGui::Text("\tlibheif: yes");
#else
                ImGui::Text("\tlibheif: no");
#endif
#ifdef HDRVIEW_ENABLE_LIBPNG
                ImGui::Text("\tlibpng:  yes");
#else
                ImGui::Text("\tlibpng:  no");
#endif
                ImGui::EndChild();
                ImGui::PopFont();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (ImGui::Button("Dismiss", HelloImGui::EmToVec2(8.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape) ||
            ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_Space) ||
            ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Slash))
        {
            ImGui::CloseCurrentPopup();
            g_show_help = false;
        }

        ImGui::ScrollWhenDraggingOnVoid(ImVec2(0.0f, -ImGui::GetIO().MouseDelta.y), ImGuiMouseButton_Left);
        ImGui::EndPopup();
    }
    else
    {
        g_help_is_open = false;
    }
}
