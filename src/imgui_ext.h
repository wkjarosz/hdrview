#pragma once

#include "common.h"
#include "fwd.h"

#include "box.h"

#include <spdlog/sinks/dup_filter_sink.h>
#include <spdlog/spdlog.h>

#include "imgui.h"
#include "ringbuffer_color_sink.h"

#include <optional>
#include <string>

namespace ImGui
{

struct ScopedFont : ScopeGuard<std::function<void()>>
{
public:
    explicit ScopedFont(ImFont *font, float font_size_base_unscaled) : ScopeGuard([]() { ImGui::PopFont(); })
    {
        ImGui::PushFont(font, font_size_base_unscaled);
    }

    ScopedFont(ScopedFont &&)                 = delete;
    ScopedFont &operator=(ScopedFont &&)      = delete;
    ScopedFont(const ScopedFont &)            = delete;
    ScopedFont &operator=(const ScopedFont &) = delete;
};

class SpdLogWindow
{
public:
    using BadgeState = spdlog::sinks::ringbuffer_color_sink_mt::BadgeState;

    SpdLogWindow(int max_items = 1024);

    void draw(ImFont *console_font = nullptr, float size = 0.f);

    std::shared_ptr<spdlog::sinks::dup_filter_sink_mt> &sink() { return m_filter_sink; }

    /// set the pattern of the underlying spdlog sink.
    /// also adds support for the custom flag %* to show the log level icon.
    void set_pattern(const std::string &pattern);

    void clear();

    void  set_level_color(spdlog::level::level_enum level, ImU32 color);
    ImU32 get_level_color(spdlog::level::level_enum level);

    /// snapshot of the highest-severity activity logged since the last mark_log_seen()
    BadgeState badge_state() { return m_ringbuffer_sink->badge_state(); }
    /// clears the badge state; called automatically once this window regains focus
    void mark_log_seen() { m_ringbuffer_sink->mark_badge_seen(); }

    /// scrolls the log view to the item with the given seq (see ringbuffer_color_sink::LogItem::seq)
    /// the next time draw() is called
    void scroll_to(uint64_t seq) { m_scroll_to_seq = seq; }

protected:
    std::shared_ptr<spdlog::sinks::dup_filter_sink_mt>       m_filter_sink;
    std::shared_ptr<spdlog::sinks::ringbuffer_color_sink_mt> m_ringbuffer_sink;
    std::array<ImU32, spdlog::level::n_levels>               m_level_colors;
    ImGuiTextFilter                                          m_filter;
    bool                                                     m_auto_scroll = true;
    bool                                                     m_wrap_text   = false;
    std::optional<uint64_t>                                  m_scroll_to_seq;
};

// reference to a global SpdLogWindow instance
SpdLogWindow &GlobalSpdLogWindow();

// icon representing a given spdlog level, from the app's active icon set
const char *LogLevelIcon(spdlog::level::level_enum level);

ImVec2 IconSize();
ImVec2 IconButtonSize();
bool   IconButton(const char *icon, bool *v = nullptr, const ImVec2 &size = ImVec2(-1, -1));

// A button with no background/border in its resting state (just a hover/press highlight), sized to fit
// its label rather than forced into a fixed square like IconButton. Useful for compact status/toolbar
// controls where the label itself (icon, text, or both) should be the only visible element. When
// active is true, the resting state is filled with ImGuiCol_FrameBg instead of being transparent, for
// a persistent toggled-on look (matching IconButton's toggle-pointer variant), without affecting the
// return value based on the click that happened this frame.
bool FlatButton(const char *label, bool active = false, const ImVec2 &size = ImVec2(0, 0));

//! A simple abstraction for a GUI action, which can be shown as a menu item, button, Checkbox, etc.
struct Action
{
    std::vector<std::string> names; // first element is primary name, rest are aliases
    std::string              icon       = "";
    ImGuiKeyChord            chord      = ImGuiKey_None;
    ImGuiInputFlags          flags      = ImGuiInputFlags_None;
    std::function<void()>    callback   = []() { return; };
    std::function<bool()>    enabled    = []() { return true; };
    bool                     needs_menu = false;
    bool                    *p_selected = nullptr;
    std::string              tooltip    = "";
    int                      last_used  = 0; // incremented whenever the action is used
};

void MenuItem(const Action &a, bool inlude_name = true);
void IconButton(const Action &a, bool include_name = false);
void Checkbox(const Action &a);

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

//! How a ChannelValuesRow's numeric boxes currently render their values. Raw/ExposureAdjusted read from the
//! row's `raw` array (Raw as-is, ExposureAdjusted scaled by `exposure_gain`); Displayed32/8/Hex read from
//! the row's `displayed` array (already run through the app's exposure/tonemap/gamma/sRGB pipeline).
enum ChannelDisplayMode_ : int
{
    ChannelDisplayMode_Raw = 0,
    ChannelDisplayMode_ExposureAdjusted,
    ChannelDisplayMode_Displayed32,
    ChannelDisplayMode_Displayed8,
    ChannelDisplayMode_DisplayedHex,
    ChannelDisplayMode_COUNT
};
using ChannelDisplayModeMask                                    = unsigned int;
constexpr ChannelDisplayModeMask ChannelDisplayMode_RawOnlyMask = 1u << ChannelDisplayMode_Raw;
constexpr ChannelDisplayModeMask ChannelDisplayMode_NoDisplayMask =
    (1u << ChannelDisplayMode_Raw) | (1u << ChannelDisplayMode_ExposureAdjusted);
constexpr ChannelDisplayModeMask ChannelDisplayMode_AllMask = (1u << ChannelDisplayMode_COUNT) - 1u;

//! Draws `num_components` read-only numeric boxes side by side, plus an optional trailing color swatch
//! and/or text label -- all one click target opening a popup with "Copy to clipboard" (if `allow_copy`)
//! and a "Display as:" list of ChannelDisplayMode_ entries (disabled, not omitted, per `enabled_modes`).
//! The click target stays active under `content_disabled`; only the boxes/swatch/label gray out (e.g. for
//! a meaningless/out-of-bounds sample). `*mode` is the caller-owned current selection. `raw` is required;
//! `displayed` may be null unless a Displayed* mode is enabled. `id` scopes the row's widget IDs.
//! `total_width`, if nonzero, overrides the row's natural (CalcItemWidth-based) width. `show_color_markers`
//! tints each box's left edge per component (only meaningful with `show_swatch`). Row height follows the
//! ambient FramePadding -- push ImGuiStyleVar_FramePadding for a more compact row; there is no compact
//! default.
void ChannelValuesRow(const char *id, const float *raw, const float *displayed, int num_components,
                      ImGuiDataType data_type, const char *format, float exposure_gain, int *mode,
                      ChannelDisplayModeMask enabled_modes = ChannelDisplayMode_AllMask, bool allow_copy = true,
                      bool show_swatch = false, const ImVec4 &swatch_color = ImVec4(0, 0, 0, 1),
                      const std::string &label = {}, float total_width = 0.f, bool content_disabled = false,
                      bool show_color_markers = false);

//! Draws a row of `num_components` text labels (e.g. channel names), each left-aligned over the column
//! ChannelValuesRow(..., num_components, ...) would draw for the same `total_width` -- for a
//! column-header row placed above one or more ChannelValuesRow calls sharing that same num_components. Pass
//! `reserve_swatch_gap = true` if any of those rows reserve a swatch column (see ChannelValuesRow) out of
//! the same `total_width`, so the header's columns still line up with the (narrower) box columns below.
void ChannelValuesRowHeader(const std::string *names, int num_components, float total_width = 0.f,
                            bool reserve_swatch_gap = false);

inline void AlignCursor(float width, float align)
{
    if (auto shift = align * (GetContentRegionAvail().x - width))
        SetCursorPosX(GetCursorPosX() + shift);
}

inline void AlignCursor(const std::string &text, float align) { AlignCursor(CalcTextSize(text.c_str()).x, align); }

// right-align the truncated file name
std::string TruncatedText(const std::string &filename, const std::string &icon);

void PushRowColors(bool is_current, bool is_reference, bool reference_mod = false);

//! Draws one row of a table-as-tree/outliner view -- the shared skeleton behind the image list's three
//! row kinds (top-level image rows, flat/tree channel-group rows, tree-mode layer-path rows): TableNextRow(),
//! an optional leading-column callback (drawn in column 0; pass nullptr to leave it empty), then
//! TreeNodeEx(id, flags, "%s", label) in column 1. `before_node` runs immediately before the TreeNodeEx
//! call (after the row has moved into the label column) and must push exactly 3 style colors -- e.g. via
//! PushRowColors() above, or a plain dim/hover-neutralizing 3x PushStyleColor() for a non-selectable row --
//! which TreeRow() pops again before returning; it's also the right place for any one-off Indent()/
//! Unindent() that needs to apply specifically to this row's label column. `label` may be "" for callers
//! that render their own content after TreeRow() returns instead (e.g. a row needing custom text
//! truncation) -- SpanAllColumns keeps that row the click target either way. Returns TreeNodeEx's own
//! open/closed result.
bool TreeRow(const void *id, ImGuiTreeNodeFlags flags, const char *label, const std::function<void()> &leading_column,
             const std::function<void()> &before_node);

void TextAlignedV2(float align_x, float size_x, const char *fmt, va_list args);
void TextAligned2(float align_x, float size_x, const char *fmt, ...);

void Tooltip(const char *description, bool questionMark = false, float wrap_width = -1.f);

// draw a horizontal line under the last item, raised by a factor of the current font size
// (e.g. raise=0.5 would strikethrough the previous text)
void UnderLine(ImColor c, float raise = 0.05f);

// Replacement for ImGui::TextLinkOpenURL which uses default font for tooltip
void HyperlinkText(const char *label, const char *url = nullptr);

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

// draws a rectangle into draw_list with a tab-like label positioned according to align if draw_label is true
// rect is in ImGui absolute coordinates
void DrawLabeledRect(ImDrawList *draw_list, const Box2f &rect, ImU32 col, const std::string &text, const float2 &align,
                     bool draw_label);

// draw a crosshair icon with an optional subscript at the ImGui absolute coordinates pos
void DrawCrosshairs(ImDrawList *draw_list, const float2 &pos, const std::string &subscript = "");

// bool DragInt4(const char *label, int *values, const char *formats[], ImGuiSliderFlags flags = 0);

// bool InputFloat4(const char *label, float *values, const char *formats[], ImGuiSliderFlags flags = 0);

namespace PropertyEditor
{
bool Begin(const char     *label = "PE::Table",
           ImGuiTableFlags flag  = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable);
void End();
bool Entry(const std::string &property_name, const std::function<bool()> &content_fct, const std::string &tooltip = {});
//! An unlabeled row whose content spans both columns. content_fct receives the available width, since a cell's
//! content region only reports its own column.
bool FullWidthEntry(const char *id, const std::function<bool(float)> &content_fct);
//! Width in pixels of a column of the innermost table, or 0 outside one. Valid between Begin and End.
float       ColumnWidth(int column_n);
inline void Entry(const std::string &property_name, const std::string &value)
{
    Entry(property_name,
          [&]
          {
              ImGui::Text("%s", value.c_str());
              return false;
          });
}
bool TreeNode(const char *name, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth);
void TreePop();

/**
    Displays a property with wrapped text in a property editor.

    This function renders a property name and its value, wrapping the value text to fit within a specified width.
    Optionally, a custom font can be used. If the value is clicked, it is copied to the clipboard.
    When hovered, the mouse cursor changes to a hand icon. A tooltip can be displayed for additional information.

    @param property_name The name of the property to display.
    @param value The value of the property to display, shown as wrapped text.
    @param tooltip Tooltip text to show when hovering over the property.
    @param font Optional font to use for rendering the property value. If nullptr, the default font is used.
    @param wrap_em The width (in em units) to wrap the text at. If 0 or less, wraps to the available content region
                   width.
*/
void WrappedText(const std::string &property_name, const std::string &value, const std::string &tooltip,
                 ImFont *font = nullptr, float wrap_em = 0.f);
void Hyperlink(const char *name, const char *desc, const char *url = nullptr);

// ===== PropertyEditor: explicit named widget forwarders =====
// These provide PropertyEditor::XXX(property_name, widget_args..., tooltip)
// and forward into the central Entry(property_name, content_fct, tooltip).

inline bool SliderFloat(const std::string &property_name, float *v, float v_min, float v_max,
                        const char *format = "%.3f", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::SliderFloat("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}
inline bool SliderFloat2(const std::string &property_name, float v[2], float v_min, float v_max,
                         const char *format = "%.3f", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::SliderFloat2("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}
inline bool SliderFloat3(const std::string &property_name, float v[3], float v_min, float v_max,
                         const char *format = "%.3f", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::SliderFloat3("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}
inline bool SliderFloat4(const std::string &property_name, float v[4], float v_min, float v_max,
                         const char *format = "%.3f", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::SliderFloat4("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}

inline bool SliderInt(const std::string &property_name, int *v, int v_min, int v_max, const char *format = "%d",
                      ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::SliderInt("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}
inline bool SliderInt2(const std::string &property_name, int v[2], int v_min, int v_max, const char *format = "%d",
                       ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::SliderInt2("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}
inline bool SliderInt3(const std::string &property_name, int v[3], int v_min, int v_max, const char *format = "%d",
                       ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::SliderInt3("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}

inline bool SliderInt4(const std::string &property_name, int v[4], int v_min, int v_max, const char *format = "%d",
                       ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::SliderInt4("##hidden", v, v_min, v_max, format, flags); }, tooltip);
}

inline bool VSliderFloat(const std::string &property_name, const ImVec2 &size, float *v, float v_min, float v_max,
                         const char *format = "%.3f", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::VSliderFloat("##hidden", size, v, v_min, v_max, format, flags); }, tooltip);
}
inline bool VSliderInt(const std::string &property_name, const ImVec2 &size, int *v, int v_min, int v_max,
                       const char *format = "%d", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::VSliderInt("##hidden", size, v, v_min, v_max, format, flags); }, tooltip);
}

inline bool DragFloat(const std::string &property_name, float *v, float v_speed = 1.0f, float v_min = 0.0f,
                      float v_max = 0.0f, const char *format = "%.3f", ImGuiSliderFlags flags = 0,
                      const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragFloat("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}
inline bool DragFloat2(const std::string &property_name, float v[2], float v_speed = 1.0f, float v_min = 0.0f,
                       float v_max = 0.0f, const char *format = "%.3f", ImGuiSliderFlags flags = 0,
                       const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragFloat2("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}
inline bool DragFloat3(const std::string &property_name, float v[3], float v_speed = 1.0f, float v_min = 0.0f,
                       float v_max = 0.0f, const char *format = "%.3f", ImGuiSliderFlags flags = 0,
                       const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragFloat3("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}
inline bool DragFloat4(const std::string &property_name, float v[4], float v_speed = 1.0f, float v_min = 0.0f,
                       float v_max = 0.0f, const char *format = "%.3f", ImGuiSliderFlags flags = 0,
                       const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragFloat4("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}

inline bool DragInt(const std::string &property_name, int *v, float v_speed = 1.0f, int v_min = 0, int v_max = 0,
                    const char *format = "%d", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragInt("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}
inline bool DragInt2(const std::string &property_name, int v[2], float v_speed = 1.0f, int v_min = 0, int v_max = 0,
                     const char *format = "%d", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragInt2("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}
inline bool DragInt3(const std::string &property_name, int v[3], float v_speed = 1.0f, int v_min = 0, int v_max = 0,
                     const char *format = "%d", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragInt3("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}
inline bool DragInt4(const std::string &property_name, int v[4], float v_speed = 1.0f, int v_min = 0, int v_max = 0,
                     const char *format = "%d", ImGuiSliderFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::DragInt4("##hidden", v, v_speed, v_min, v_max, format, flags); }, tooltip);
}

inline bool InputFloat(const std::string &property_name, float *v, float step = 0.0f, float step_fast = 0.0f,
                       const char *format = "%.3f", ImGuiInputTextFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&] { return ImGui::InputFloat("##hidden", v, step, step_fast, format, flags); }, tooltip);
}
inline bool InputFloat2(const std::string &property_name, float v[2], const char *format = "%.3f",
                        ImGuiInputTextFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::InputFloat2("##hidden", v, format, flags); }, tooltip);
}
inline bool InputFloat3(const std::string &property_name, float v[3], const char *format = "%.3f",
                        ImGuiInputTextFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::InputFloat3("##hidden", v, format, flags); }, tooltip);
}
inline bool InputFloat4(const std::string &property_name, float v[4], const char *format = "%.3f",
                        ImGuiInputTextFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::InputFloat4("##hidden", v, format, flags); }, tooltip);
}

inline bool InputInt(const std::string &property_name, int *v, int step = 1, int step_fast = 100,
                     ImGuiInputTextFlags flags = 0, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::InputInt("##hidden", v, step, step_fast, flags); }, tooltip);
}
inline bool InputInt2(const std::string &property_name, int v[2], ImGuiInputTextFlags flags = 0,
                      const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::InputInt2("##hidden", v, flags); }, tooltip);
}
inline bool InputInt3(const std::string &property_name, int v[3], ImGuiInputTextFlags flags = 0,
                      const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::InputInt3("##hidden", v, flags); }, tooltip);
}
inline bool InputInt4(const std::string &property_name, int v[4], ImGuiInputTextFlags flags = 0,
                      const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::InputInt4("##hidden", v, flags); }, tooltip);
}

inline bool Checkbox(const std::string &property_name, bool *v, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::Checkbox("##hidden", v); }, tooltip);
}
inline bool CheckboxFlags(const std::string &property_name, int *flags, int flags_value,
                          const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::CheckboxFlags("##hidden", flags, flags_value); }, tooltip);
}
inline bool CheckboxFlags(const std::string &property_name, unsigned int *flags, unsigned int flags_value,
                          const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::CheckboxFlags("##hidden", flags, flags_value); }, tooltip);
}

inline bool RadioButton(const std::string &property_name, bool active, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::RadioButton("##hidden", active); }, tooltip);
}
inline bool RadioButton(const std::string &property_name, int *v, int v_button, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::RadioButton("##hidden", v, v_button); }, tooltip);
}

inline bool Button(const std::string &property_name, const ImVec2 &size = ImVec2(0, 0), const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::Button("##hidden", size); }, tooltip);
}
inline bool SmallButton(const std::string &property_name, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::SmallButton("##hidden"); }, tooltip);
}

inline bool Combo(const std::string &property_name, int *current_item, const char *const items[], int items_count,
                  int popup_max_height_in_items = -1, const std::string &tooltip = {})
{
    return Entry(
        property_name,
        [&] { return ImGui::Combo("##hidden", current_item, items, items_count, popup_max_height_in_items); }, tooltip);
}
inline bool Combo(const std::string &property_name, int *current_item, const char *items_separated_by_zeros,
                  int popup_max_height_in_items = -1, const std::string &tooltip = {})
{
    return Entry(
        property_name,
        [&] { return ImGui::Combo("##hidden", current_item, items_separated_by_zeros, popup_max_height_in_items); },
        tooltip);
}
inline bool Combo(const std::string &property_name, int *current_item, const char *(*getter)(void *user_data, int idx),
                  void *user_data, int items_count, int popup_max_height_in_items = -1, const std::string &tooltip = {})
{
    return Entry(
        property_name, [&]
        { return ImGui::Combo("##hidden", current_item, getter, user_data, items_count, popup_max_height_in_items); },
        tooltip);
}

inline bool ColorEdit3(const std::string &property_name, float col[3], ImGuiColorEditFlags flags = 0,
                       const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::ColorEdit3("##hidden", col, flags); }, tooltip);
}
inline bool ColorEdit4(const std::string &property_name, float col[4], ImGuiColorEditFlags flags = 0,
                       const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::ColorEdit4("##hidden", col, flags); }, tooltip);
}
inline bool ColorPicker3(const std::string &property_name, float col[3], ImGuiColorEditFlags flags = 0,
                         const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::ColorPicker3("##hidden", col, flags); }, tooltip);
}
inline bool ColorPicker4(const std::string &property_name, float col[4], ImGuiColorEditFlags flags = 0,
                         const float *ref_col = NULL, const std::string &tooltip = {})
{
    return Entry(property_name, [&] { return ImGui::ColorPicker4("##hidden", col, flags, ref_col); }, tooltip);
}
inline bool ColorButton(const char *label, const ImVec4 &col, ImGuiColorEditFlags flags = 0,
                        const ImVec2 &size = ImVec2(0, 0), const std::string &tooltip = {})
{
    return Entry(label, [&] { return ImGui::ColorButton("##hidden", col, flags, size); }, tooltip);
}

} // namespace PropertyEditor

namespace PE = PropertyEditor; // short alias

} // namespace ImGui