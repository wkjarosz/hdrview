/** \file app.cpp
    \author Wojciech Jarosz
*/

#include "theme.h"

#include "hello_imgui/hello_imgui.h"

#include <spdlog/spdlog.h>

using namespace std;
using namespace HelloImGui;

static void apply_hdrview_dark_theme()
{
    // Apply default style
    ImGuiStyle &style = ImGui::GetStyle();
    style             = ImGuiStyle(); // resets all fields to default values

    style.FontSizeBase = 14.0f; // base font size
    // style._NextFrameFontSizeBase = style.FontSizeBase; // FIXME: Temporary hack until we finish remaining work.
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
    colors[ImGuiCol_Text]                      = ImVec4(1.00f, 1.00f, 1.00f, 0.71f);
    colors[ImGuiCol_TextDisabled]              = ImVec4(0.50f, 0.50f, 0.50f, 0.71f);
    colors[ImGuiCol_WindowBg]                  = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ChildBg]                   = ImVec4(0.04f, 0.04f, 0.04f, 0.20f);
    colors[ImGuiCol_PopupBg]                   = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_Border]                    = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_BorderShadow]              = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
    colors[ImGuiCol_FrameBg]                   = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]            = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
    colors[ImGuiCol_FrameBgActive]             = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
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
    colors[ImGuiCol_Separator]                 = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
    colors[ImGuiCol_SeparatorHovered]          = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_SeparatorActive]           = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_ResizeGrip]                = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]         = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]          = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
    colors[ImGuiCol_InputTextCursor]           = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
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
    colors[ImGuiCol_TreeLines]                 = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
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

    style.FontSizeBase = 14.0f; // base font size
    // style._NextFrameFontSizeBase = style.FontSizeBase; // FIXME: Temporary hack until we finish remaining work.
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

const char *Theme::name(int t)
{
    if (t >= 0 && t < ImGuiTheme::ImGuiTheme_Count)
        return ImGuiTheme::ImGuiTheme_Name((ImGuiTheme::ImGuiTheme_)t);
    else if (t == DARK_THEME)
        return "HDRView dark";
    else if (t == LIGHT_THEME)
        return "HDRView light";
    else
        return "Custom";
}

static void apply(int theme)
{
    if (theme >= 0)
    {
        ImGuiTheme::ImGuiTheme_ t                               = (ImGuiTheme::ImGuiTheme_)theme;
        GetRunnerParams()->imGuiWindowParams.tweakedTheme.Theme = t;
        ImGuiTheme::ApplyTheme(t);
    }
    else if (theme == Theme::DARK_THEME)
        apply_hdrview_dark_theme();
    else if (theme == Theme::LIGHT_THEME)
        apply_hdrview_light_theme();

    // otherwise, its a custom theme, and we keep the parameters that were read from the config file
}

void Theme::set(int t)
{
    spdlog::info("Applying theme: '{}'", Theme::name(t));
    if (t >= ImGuiTheme::ImGuiTheme_Count)
    {
        spdlog::error("Invalid theme index: {}. Using default theme.", t);
        t = DARK_THEME;
    }

    theme = t;

    apply(theme);
}

void Theme::load(json j)
{
    if (!j.contains("theme"))
    {
        theme = DARK_THEME; // default to dark theme
        apply(theme);
        return;
    }

    auto name = j["theme"].get<string>();
    spdlog::info("Restoring theme: '{}'", name);
    if (name == "HDRView dark")
        theme = DARK_THEME;
    else if (name == "HDRView light")
        theme = LIGHT_THEME;
    else if (name == "Custom")
    {
        theme = CUSTOM_THEME;
        if (j.contains("style"))
        {
            spdlog::debug("Restoring custom ImGui style values from settings:\n{}", j["style"].dump(2));
            spdlog::debug("[DIAGNOSTIC] Before loading custom style: FontSizeBase={}, FontScaleMain={}, "
                          "FontScaleDpi={}, WindowPadding=({},{})",
                          ImGui::GetStyle().FontSizeBase, ImGui::GetStyle().FontScaleMain,
                          ImGui::GetStyle().FontScaleDpi, ImGui::GetStyle().WindowPadding.x,
                          ImGui::GetStyle().WindowPadding.y);
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
            auto read_float1 = [&](const char *key, float &val)
            {
                if (j_style.contains(key))
                    val = j_style[key].get<float>();
            };
            auto read_float2 = [&](const char *key, ImVec2 &val)
            {
                if (j_style.contains(key) && j_style[key].is_array() && j_style[key].size() == 2)
                    val = ImVec2(j_style[key][0].get<float>(), j_style[key][1].get<float>());
            };

            read_float1("Alpha", style.Alpha);
            read_float1("DisabledAlpha", style.DisabledAlpha);
            read_float2("WindowPadding", style.WindowPadding);
            read_float1("WindowRounding", style.WindowRounding);
            read_float1("WindowBorderSize", style.WindowBorderSize);
            read_float2("WindowMinSize", style.WindowMinSize);
            read_float2("WindowTitleAlign", style.WindowTitleAlign);
            read_float1("ChildRounding", style.ChildRounding);
            read_float1("ChildBorderSize", style.ChildBorderSize);
            read_float1("PopupRounding", style.PopupRounding);
            read_float1("PopupBorderSize", style.PopupBorderSize);
            read_float2("FramePadding", style.FramePadding);
            read_float1("FrameRounding", style.FrameRounding);
            read_float1("FrameBorderSize", style.FrameBorderSize);
            read_float2("ItemSpacing", style.ItemSpacing);
            read_float2("ItemInnerSpacing", style.ItemInnerSpacing);
            read_float1("IndentSpacing", style.IndentSpacing);
            read_float2("CellPadding", style.CellPadding);
            read_float1("ScrollbarSize", style.ScrollbarSize);
            read_float1("ScrollbarRounding", style.ScrollbarRounding);
            read_float1("GrabMinSize", style.GrabMinSize);
            read_float1("GrabRounding", style.GrabRounding);
            read_float1("ImageBorderSize", style.ImageBorderSize);
            read_float1("TabRounding", style.TabRounding);
            read_float1("TabBorderSize", style.TabBorderSize);
            read_float1("TabBarBorderSize", style.TabBarBorderSize);
            read_float1("TabBarOverlineSize", style.TabBarOverlineSize);
            read_float1("TableAngledHeadersAngle", style.TableAngledHeadersAngle);
            read_float2("TableAngledHeadersTextAlign", style.TableAngledHeadersTextAlign);
            read_float1("TreeLinesSize", style.TreeLinesSize);
            read_float1("TreeLinesRounding", style.TreeLinesRounding);
            read_float2("ButtonTextAlign", style.ButtonTextAlign);
            read_float2("SelectableTextAlign", style.SelectableTextAlign);
            read_float1("SeparatorTextBorderSize", style.SeparatorTextBorderSize);
            read_float2("SeparatorTextAlign", style.SeparatorTextAlign);
            read_float2("SeparatorTextPadding", style.SeparatorTextPadding);
            read_float1("DockingSeparatorSize", style.DockingSeparatorSize);
            read_float1("FontSizeBase", style.FontSizeBase);
            read_float1("FontScaleMain", style.FontScaleMain);
            read_float1("FontScaleDpi", style.FontScaleDpi);
            read_float1("CircleTessellationMaxError", style.CircleTessellationMaxError);
            if (j_style.contains("WindowMenuButtonPosition"))
                style.WindowMenuButtonPosition = (ImGuiDir)j_style["WindowMenuButtonPosition"].get<int>();

            spdlog::debug(
                "[DIAGNOSTIC] After loading custom style: FontSizeBase={}, FontScaleMain={}, FontScaleDpi={}, "
                "WindowPadding=({},{})",
                style.FontSizeBase, style.FontScaleMain, style.FontScaleDpi, style.WindowPadding.x,
                style.WindowPadding.y);
        }
    }
    else
        theme = GetRunnerParams()->imGuiWindowParams.tweakedTheme.Theme = ImGuiTheme::ImGuiTheme_FromName(name.c_str());

    apply(theme);
}

void Theme::save(json &j) const
{
    // Save ImGui theme tweaks
    j["theme"] = Theme::name(theme);

    if (theme != CUSTOM_THEME)
        return;

    const auto &style = ImGui::GetStyle();
    spdlog::debug(
        "[DIAGNOSTIC] Saving custom style: FontSizeBase={}, FontScaleMain={}, FontScaleDpi={}, WindowPadding=({},{})",
        style.FontSizeBase, style.FontScaleMain, style.FontScaleDpi, style.WindowPadding.x, style.WindowPadding.y);

    auto         &j_style = j["style"];
    const ImVec4 *colors  = style.Colors;
    // Save all ImGuiCol colors
    for (int col = 0; col < ImGuiCol_COUNT; ++col)
    {
        const ImVec4 &c        = colors[col];
        const char   *col_name = ImGui::GetStyleColorName(col);
        if (col_name)
            j_style[col_name] = {c.x, c.y, c.z, c.w};
    }
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
