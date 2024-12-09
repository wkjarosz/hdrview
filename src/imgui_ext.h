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

void PushRowColors(bool is_current, bool is_reference);

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

void BusyBar(float fraction, const ImVec2 &size_arg = ImVec2(-FLT_MIN, 0), const char *overlay = NULL);

// draw a horizontal line under the last item, raised by a factor of the current font size
// (e.g. raise=0.5 would strikethrough the previous text)
void   UnderLine(ImColor c, float raise = 0.05f);
ImVec4 LinkColor();

void HyperlinkText(const char *href, const char *fmt, ...);
void TextWithHoverColor(ImVec4 col, const char *fmt, ...);

// Like ImGui::GetKeyChordName, but returns the translated name of the key chord.
const char *GetKeyChordNameTranslated(ImGuiKeyChord key_chord);
// Used for global key chords, e.g. for menu shortcuts.
bool GlobalChordPressed(const ImGuiKeyChord &chord, ImGuiInputFlags flags = 0);

} // namespace ImGui