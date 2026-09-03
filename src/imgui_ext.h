#pragma once

#include "common.h"
#include "fwd.h"

#include "box.h"

#include <spdlog/sinks/dup_filter_sink.h>
#include <spdlog/spdlog.h>

#include "imgui.h"
#include "ringbuffer_color_sink.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>

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
    /**
        Also adds support for the custom flag %* to show the log level icon.
    */
    void set_pattern(const std::string &pattern);

    void clear();

    void  set_level_color(spdlog::level::level_enum level, ImU32 color);
    ImU32 get_level_color(spdlog::level::level_enum level);

    /// snapshot of the highest-severity activity logged since the last mark_log_seen()
    BadgeState badge_state() { return m_ringbuffer_sink->badge_state(); }
    /// clears the badge state; called automatically once this window regains focus
    void mark_log_seen() { m_ringbuffer_sink->mark_badge_seen(); }

    /// scrolls the log view to the item with the given seq (see ringbuffer_color_sink::LogItem::seq) on the next draw()
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

// A button with no background or border in its resting state (just a hover/press highlight), sized to fit
// its label instead of a fixed square like IconButton. `active` fills the resting state with
// ImGuiCol_FrameBg for a toggled-on look, without affecting the return value.
bool FlatButton(const char *label, bool active = false, const ImVec2 &size = ImVec2(0, 0));

/// A simple abstraction for a GUI action, which can be shown as a menu item, button, Checkbox, etc.
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
/// MenuItem() with the label spelled out instead of taken from the action's name.
/**
    For an item whose text depends on something the action does not carry ("Undo Rotate 90 degrees
    clockwise"). The action's own name is the key the registry and the command palette address it by, so it
    stays put.
*/
void MenuItem(const Action &a, const std::string &label);
void IconButton(const Action &a, bool include_name = false);
void Checkbox(const Action &a);

// ===== Modal dialog helpers =====
// Shared boilerplate for HDRViewApp's "PopupDialog"-style modals: an OpenPopup-on-request shell plus a
// Cancel/Confirm footer, used together (via ConfirmDialog) for simple yes/no prompts, or separately for
// dialogs with custom bodies (see e.g. HDRViewApp::draw_save_as_dialog / draw_confirm_load_session_dialog).

enum class DialogPosition
{
    TopCenter, ///< centered horizontally, near the top of the viewport
    Center,    ///< centered both horizontally and vertically in the viewport
    None       ///< no positioning; caller may set its own via SetNextWindowPos/SetNextWindowSize
};

// Handles the open-on-request/startup-frame-safety/centering boilerplate common to every modal dialog:
// fires ImGui::OpenPopup(title) the frame `open` becomes true (once HelloImGui's startup frames have
// settled, so a dialog can start out already-open), consumes `open`, applies `position`, then forwards to
// ImGui::BeginPopupModal. Call ImGui::EndPopup() only when this returns true, as with BeginPopupModal.
bool BeginModalDialog(const char *title, bool &open, DialogPosition position = DialogPosition::TopCenter,
                      ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize);

enum class DialogResult
{
    None,
    Cancel,
    Confirm
};

// Draws a Cancel/<confirm_label> button pair (SameLine-separated). With use_shortcuts, Escape or Ctrl+.
// also trigger Cancel and Ctrl+Enter also triggers Confirm (ignored while ImGui nav is active). Returns
// which one fired this frame, if any; the caller decides what each means and whether to call
// ImGui::CloseCurrentPopup(), so the Cancel slot can hold an in-place action that never closes the popup.
DialogResult DialogButtons(const char *confirm_label = "OK", const char *cancel_label = "Cancel",
                           bool use_shortcuts = true, bool confirm_enabled = true);

// Draws `message` plus a Cancel/confirm_label footer (via DialogButtons, with shortcuts enabled) inside a
// BeginModalDialog shell. Returns Confirm/Cancel the frame a button fires (also closing the popup then),
// else None.
DialogResult ConfirmDialog(const char *title, bool &open, const char *message, const char *confirm_label = "OK",
                           DialogPosition position = DialogPosition::Center);

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
    std::string str = fmt::format(fmt, std::forward<T>(args)...);
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

/// How a ChannelValuesRow's numeric boxes render their values.
/**
    Raw/ExposureAdjusted read the row's `raw` array (ExposureAdjusted scaled by `exposure_gain`);
    Displayed32/8/Hex read its `displayed` array, already run through the app's exposure/tonemap/gamma/sRGB
    pipeline.
*/
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

/// Draws `num_components` read-only numeric boxes side by side, plus an optional swatch and/or text label.
/**
    All one click target opening a popup with "Copy to clipboard" (if `allow_copy`) and a "Display as:"
    list of ChannelDisplayMode_ entries, disabled rather than omitted per `enabled_modes`.
    `content_disabled` grays out the boxes, swatch and label but leaves the click target active. `*mode` is
    the caller-owned current selection, `raw` is required, `displayed` may be null unless a Displayed* mode
    is enabled, and `id` scopes the row's widget IDs. `extra_popup_items` is drawn at the top of the popup.

    `total_width`, if nonzero, overrides the row's natural (CalcItemWidth-based) width.
    `show_color_markers` tints each box's left edge per component, meaningful only with `show_swatch`. Row
    height follows the ambient FramePadding, so push ImGuiStyleVar_FramePadding for a more compact row.
*/
void ChannelValuesRow(const char *id, const float *raw, const float *displayed, int num_components,
                      ImGuiDataType data_type, const char *format, float exposure_gain, int *mode,
                      ChannelDisplayModeMask enabled_modes = ChannelDisplayMode_AllMask, bool allow_copy = true,
                      bool show_swatch = false, const ImVec4 &swatch_color = ImVec4(0, 0, 0, 1),
                      const std::string &label = {}, float total_width = 0.f, bool content_disabled = false,
                      bool show_color_markers = false, const std::function<void()> &extra_popup_items = {});

/// Draws a row of `num_components` text labels (e.g. channel names) as a header above ChannelValuesRows.
/**
    Each is left-aligned over the column ChannelValuesRow would draw for the same `total_width`. Pass
    `reserve_swatch_gap` when any of those rows reserve a swatch column out of that same `total_width`.
*/
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

/// Push the Header/HeaderHovered/HeaderActive colors an image-list row is drawn with.
/**
    A row can be current, selected, the reference, or both current and reference, which draws the average
    of the two. `reference_mod` previews what a shift-click would do, tinting a hovered row in the
    reference color.
*/
void PushRowColors(bool is_current, bool is_reference, bool reference_mod = false, bool is_selected = false);

/// One row of a table-as-tree view: TableNextRow(), `leading_column` in column 0, then a tree node.
/**
    `leading_column` may be null. TreeNodeEx(id, flags, "%s", label) is drawn in column 1 and its
    open/closed result returned. `before_node` runs just before the TreeNodeEx call and must push exactly 3
    style colors (e.g. PushRowColors()), which are popped here; it is also where a one-off
    Indent()/Unindent() for this row's label column belongs. `label` may be "" for a caller that draws its
    own content afterwards, since SpanAllColumns keeps the row the click target either way.
*/
bool TreeRow(const void *id, ImGuiTreeNodeFlags flags, const char *label, const std::function<void()> &leading_column,
             const std::function<void()> &before_node);

void TextAlignedV2(float align_x, float size_x, const char *fmt, va_list args);
void TextAligned2(float align_x, float size_x, const char *fmt, ...);

void Tooltip(const char *description, bool questionMark = false, float wrap_width = -1.f);

/**
    The right-hand extent of a run of rows, so that something can be hung off all of them at once.

    Fed by calling take() after each item that belongs to a row; it remembers the widest right edge and
    where the first and last rows sit vertically. See RowBracketButton(), which is what consumes it.
*/
/**
    The square plot of a tone curve that the tonal dialogs draw above their sliders: what a level goes in
    as against what it comes out as, over [0,1]. Used like ImPlot itself:

        if (ImGui::BeginToneCurvePlot("##Curve"))
        {
            ImGui::ToneCurve("gamma", ys, ImVec4(1, 1, 1, 0.85f));
            ImGui::EndToneCurvePlot();
        }
*/

/// Samples along the horizontal axis.
constexpr int ToneCurveSamples = 129;
/// The input level at sample \p i, where every curve drawn here is evaluated.
inline float ToneCurveX(int i) { return float(i) / float(ToneCurveSamples - 1); }

/// Open the plot. False when ImPlot declined it, in which case nothing else may be called.
bool BeginToneCurvePlot(const char *id);
/// One curve, \p ys being ToneCurveSamples outputs for the inputs ToneCurveX(0)...
void ToneCurve(const char *name, const float *ys, ImVec4 color, float weight = 2.f);
/// A vertical line, for marking the input level a curve pivots about.
void ToneCurveMarkerX(const char *name, float value, ImVec4 color);
/// A small marker at \p at, for a point of the curve that can be taken hold of.
void ToneCurveHandle(float2 at, ImVec4 color);
/// While the left button is dragging inside the plot, where it is, in plot coordinates.
/**
    Not the widget's coordinates, which include the frame and tick labels. A drag that began inside is
    followed after it leaves, and \p pressed_at receives where it began, which says what is being dragged.
*/
bool ToneCurveDrag(float2 &position, float2 *pressed_at = nullptr);
void EndToneCurvePlot();

struct RowSpan
{
    float right = 0.f;             ///< Right edge of the widest row so far
    float first = 0.f, last = 0.f; ///< Vertical centers of the first and most recent rows
    int   count = 0;

    /// Take in the item just drawn.
    void take();
};

/**
    A button at the open end of a bracket joining \p rows, as Photoshop joins a width to a height.

    The bracket says what the button means better than the button can -- these rows are tied, and here is
    where -- and is drawn only when \p bracketed, since an open chain has nothing to join. Positioned
    entirely from \p rows, leaving the cursor where it found it, so it can be called after the rows are
    laid out without disturbing what follows them.

    \returns Whether the button was pressed this frame.
*/
bool RowBracketButton(const char *icon, const RowSpan &rows, bool bracketed, const char *tooltip);

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
/// An unlabeled row whose content spans both columns.
/**
    content_fct receives the available width, since a cell's content region only reports its own column.
*/
bool FullWidthEntry(const char *id, const std::function<bool(float)> &content_fct);
/// Width in pixels of a column of the innermost table, or 0 outside one. Valid between Begin and End.
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

/// Font used by Entry()/TreeNode() for the property-name column only; the content column keeps the ambient one.
/**
    A whole table's labels can be bolded with one push around it. Pushed at the ambient size, so row
    heights are unaffected, and stacked, so nested sections restore the enclosing choice.
*/
void PushLabelFont(ImFont *font);
void PopLabelFont();

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