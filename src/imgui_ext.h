#pragma once

#include "fwd.h"

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "ringbuffer_color_sink.h"

#include <string>

namespace ImGui
{

class ScopedFont
{
public:
    explicit ScopedFont(ImFont *font) { ImGui::PushFont(font); }
    ~ScopedFont() { ImGui::PopFont(); }

    ScopedFont(ScopedFont &&)                 = delete;
    ScopedFont &operator=(ScopedFont &&)      = delete;
    ScopedFont(const ScopedFont &)            = delete;
    ScopedFont &operator=(const ScopedFont &) = delete;
};

class SpdLogWindow
{
public:
    SpdLogWindow(int max_items = 1024);

    void draw(ImFont *console_font = nullptr);

    std::shared_ptr<spdlog::sinks::ringbuffer_color_sink_mt> &sink() { return m_sink; }

    /// set the pattern of the underlying spdlog sink.
    /// also adds support for the custom flag %* to show the log level icon.
    void set_pattern(const std::string &pattern);

    void clear();

    ImU32 get_default_color();
    void  set_default_color(ImU32 color);

    void  set_level_color(spdlog::level::level_enum level, ImU32 color);
    ImU32 get_level_color(spdlog::level::level_enum level);

protected:
    std::shared_ptr<spdlog::sinks::ringbuffer_color_sink_mt> m_sink;
    ImU32                                                    m_default_color;
    std::array<ImU32, spdlog::level::n_levels>               m_level_colors;
    ImGuiTextFilter                                          m_filter;
    bool                                                     m_auto_scroll = true;
    bool                                                     m_wrap_text   = false;
};

// reference to a global SpdLogWindow instance
SpdLogWindow &GlobalSpdLogWindow();

ImVec2 IconSize();
ImVec2 IconButtonSize();
bool   IconButton(const char *icon);

inline bool BeginComboButton(const char *id, const char *preview_icon, ImGuiComboFlags flags = ImGuiComboFlags_None)
{
    // Calculate the padding needed to center an icon in a ComboBox
    // Solve for NewPadding.x:
    // NewPadding.x + IconWidth + NewPadding.x = button_size.x
    // NewPadding.x + FontSize + NewPadding.x = FontSize + style.FramePadding.y * 2
    // 2 * NewPadding.x = style.FramePadding.y * 2
    // NewPadding.x = style.FramePadding.y
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetStyle().FramePadding.y, ImGui::GetStyle().FramePadding.y));
    ImGui::SetNextItemWidth(IconButtonSize().x);
    bool ret =
        ImGui::BeginCombo(id, preview_icon, flags | ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLargest);
    ImGui::PopStyleVar();
    return ret;
}

inline void EndComboButton() { ImGui::EndCombo(); }

inline bool ToggleButton(const char *label, bool *active, const ImVec2 &size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, *active ? GetColorU32(ImGuiCol_ButtonActive) : GetColorU32(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetColorU32(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetColorU32(ImGuiCol_FrameBgActive));

    bool ret;
    if ((ret = ImGui::Button(label, size)))
        *active = !*active;
    ImGui::PopStyleColor(3);
    return ret;
}

inline void Text(const std::string &text) { return Text("%s", text.c_str()); }

inline void TextUnformatted(const std::string &text) { return TextUnformatted(text.c_str()); }

template <typename... T>
inline void TextFmt(fmt::format_string<T...> fmt, T &&...args)
{
    // TODO the below produces an obscure compiler message that we could decipher and resolve properly
    //      for now, we just bypass the compile-time validation
    // std::string str = fmt::format(fmt, fmt::make_format_args(args...));
    std::string str = fmt::format(fmt::runtime(fmt), args...);
    ImGui::TextUnformatted(str.c_str());
}

// return true when activated.
inline bool MenuItem(const std::string &label, const std::string &shortcut = "", bool selected = false,
                     bool enabled = true)
{
    return MenuItem(label.c_str(), shortcut.c_str(), selected, enabled);
}

// return true when activated + toggle (*p_selected) if p_selected != NULL
inline bool MenuItem(const std::string &label, const std::string &shortcut, bool *p_selected, bool enabled = true)
{
    return MenuItem(label.c_str(), shortcut.c_str(), p_selected, enabled);
}

bool MenuItemEx(const std::string &label, const std::string &icon, const std::string &shortcut, bool *p_selected,
                bool enabled = true);

void AddTextAligned(ImDrawList *draw_list, float2 pos, ImU32 color, const std::string &text,
                    float2 align = float2{0.f});

void ScrollWhenDraggingOnVoid(const ImVec2 &delta, ImGuiMouseButton mouse_button);

void PlotMultiLines(const char *label, int num_datas, const char **names, const ImColor *colors,
                    float (*getter)(const void *data, int idx, int tableIndex), const void *datas, int values_count,
                    float scale_min = FLT_MAX, float scale_max = FLT_MAX, ImVec2 graph_size = ImVec2(0, 0));

void PlotMultiHistograms(const char *label, int num_hists, const char **names, const ImColor *colors,
                         float (*getter)(const void *data, int idx, int tableIndex), const void *datas,
                         int values_count, float scale_min = FLT_MAX, float scale_max = FLT_MAX,
                         ImVec2 graph_size = ImVec2(0, 0));

inline void AlignCursor(float width, float align)
{
    if (auto shift = align * (GetContentRegionAvail().x - width))
        SetCursorPosX(GetCursorPosX() + shift);
}

inline void AlignCursor(const std::string &text, float align) { AlignCursor(CalcTextSize(text.c_str()).x, align); }

inline void TextAligned(const std::string text, float align)
{
    AlignCursor(text, align);
    TextUnformatted(text.c_str());
}

void PushRowColors(bool is_current, bool is_reference, bool reference_mod = false);

inline void WrappedTooltip(const char *text, float wrap_width = 400.f)
{
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(wrap_width);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// draw a horizontal line under the last item, raised by a factor of the current font size
// (e.g. raise=0.5 would strikethrough the previous text)
void UnderLine(ImColor c, float raise = 0.05f);

// Replacement for ImGui::TextLinkOpenURL which uses default font for tooltip
void HyperlinkText(const char *label, const char *url = NULL);

// Like ImGui::GetKeyChordName, but returns the translated name of the key chord.
const char *GetKeyChordNameTranslated(ImGuiKeyChord key_chord);
// Used for global key chords, e.g. for menu shortcuts.
bool GlobalShortcut(const ImGuiKeyChord &chord, ImGuiInputFlags flags = 0);

// linalg::float3 wrapper for ImGui function
// Convert rgb floats ([0-1],[0-1],[0-1]) to hsv floats ([0-1],[0-1],[0-1])
inline float3 ColorConvertRGBtoHSV(const float3 &rgb)
{
    float3 hsv;
    ColorConvertRGBtoHSV(rgb.x, rgb.y, rgb.z, hsv.x, hsv.y, hsv.z);
    return hsv;
}

// linalg::float3 wrapper for ImGui function
// Convert hsv floats ([0-1],[0-1],[0-1]) to rgb floats ([0-1],[0-1],[0-1])
inline float3 ColorConvertHSVtoRGB(const float3 &hsv)
{
    float3 rgb;
    ColorConvertHSVtoRGB(hsv.x, hsv.y, hsv.z, rgb.x, rgb.y, rgb.z);
    return rgb;
}

} // namespace ImGui