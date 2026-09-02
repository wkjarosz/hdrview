#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_ext.h"
#include "box.h"
#include "common.h"
#include "fonts.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "spdlog/pattern_formatter.h"
#include <hello_imgui/dpi_aware.h>

#include "app.h"

#include <array>

using namespace std;

namespace
{
enum : ImU32
{
    white       = IM_COL32(0xff, 0xff, 0xff, 0xff),
    black       = IM_COL32(0x00, 0x00, 0x00, 0xff),
    red         = IM_COL32(0xff, 0x00, 0x00, 0xff),
    darkRed     = IM_COL32(0x80, 0x00, 0x00, 0xff),
    green       = IM_COL32(0x00, 0xff, 0x00, 0xff),
    darkGreen   = IM_COL32(0x00, 0x80, 0x00, 0xff),
    blue        = IM_COL32(0x00, 0x00, 0xff, 0xff),
    darkBlue    = IM_COL32(0x00, 0x00, 0x80, 0xff),
    cyan        = IM_COL32(0x00, 0xff, 0xff, 0xff),
    darkCyan    = IM_COL32(0x00, 0x80, 0x80, 0xff),
    magenta     = IM_COL32(0xff, 0x00, 0xff, 0xff),
    darkMagenta = IM_COL32(0x80, 0x00, 0x80, 0xff),
    yellow      = IM_COL32(0xff, 0xff, 0x00, 0xff),
    darkYellow  = IM_COL32(0x80, 0x80, 0x00, 0xff),
    gray        = IM_COL32(0xa0, 0xa0, 0xa4, 0xff),
    darkGray    = IM_COL32(0x80, 0x80, 0x80, 0xff),
    lightGray   = IM_COL32(0xc0, 0xc0, 0xc0, 0xff),
};

static const std::string s_level_icons[] = {
    ICON_MY_LOG_LEVEL_TRACE, ICON_MY_LOG_LEVEL_DEBUG,    ICON_MY_LOG_LEVEL_INFO, ICON_MY_LOG_LEVEL_WARN,
    ICON_MY_LOG_LEVEL_ERROR, ICON_MY_LOG_LEVEL_CRITICAL, ICON_MY_LOG_LEVEL_OFF};

class level_icon_formatter_flag : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) override
    {
        std::string some_txt = s_level_icons[msg.level];
        dest.append(some_txt.data(), some_txt.data() + some_txt.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<level_icon_formatter_flag>();
    }
};

} // namespace

namespace ImGui
{

SpdLogWindow &GlobalSpdLogWindow()
{
    static ImGui::SpdLogWindow s_log{1024};
    return s_log;
}

const char *LogLevelIcon(spdlog::level::level_enum level) { return s_level_icons[int(level)].c_str(); }

SpdLogWindow::SpdLogWindow(int max_items) :
    m_filter_sink(make_shared<spdlog::sinks::dup_filter_sink_mt>(std::chrono::seconds(5))),
    m_ringbuffer_sink(make_shared<spdlog::sinks::ringbuffer_color_sink_mt>(max_items)),
    m_level_colors({white, cyan, green, yellow, red, magenta, gray})
{
    m_filter_sink->add_sink(m_ringbuffer_sink);
}

void SpdLogWindow::set_pattern(const string &pattern)
{
    // add support for custom level icon flag to formatter
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<level_icon_formatter_flag>('*').set_pattern(pattern);
    m_filter_sink->set_formatter(std::move(formatter));
}

void SpdLogWindow::draw(ImFont *console_font, float size)
{
    // Being focused (as opposed to merely visible, e.g. an inactive docked tab) is what counts as
    // the user having seen the log, so this is what clears the status-bar badge.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        mark_log_seen();

    static const spdlog::string_view_t level_names[] = SPDLOG_LEVEL_NAMES;

    auto         current_level = m_ringbuffer_sink->level();
    const ImVec2 button_size   = IconButtonSize();
    bool         filter_active = m_filter.IsActive(); // save here to avoid flicker

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4 * (button_size.x + ImGui::GetStyle().ItemSpacing.x));
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InputTextWithHint(
            "##log filter",
            ICON_MY_FILTER
            "Filter (format: [include|-exclude][,...]; e.g. \"include_this,-but_not_this,also_include_this\")",
            m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf)))
        m_filter.Build();
    if (filter_active)
    {
        ImGui::SameLine(0.f, 0.f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
        if (ImGui::IconButton(ICON_MY_DELETE))
            m_filter.Clear();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, m_level_colors.at(current_level));
    if (ImGui::BeginComboButton("##Log level", s_level_icons[int(current_level)].data()))
    {
        for (int i = 0; i < spdlog::level::n_levels; ++i)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, i < int(current_level) ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                                                        : m_level_colors.at(i));
            if (ImGui::Selectable(
                    (ICON_MY_GREATER_EQUAL + std::to_string(i) + ": " + s_level_icons[i] + " " + level_names[i].data())
                        .c_str(),
                    current_level == i))
            {
                m_filter_sink->set_level(spdlog::level::level_enum(i));
                m_ringbuffer_sink->set_level(spdlog::level::level_enum(i));
                spdlog::set_level(spdlog::level::level_enum(i));
                spdlog::info("Setting verbosity threshold to level {:d}.", i);
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor();
    ImGui::Tooltip("Click to choose the verbosity level.");
    ImGui::SameLine();
    if (ImGui::IconButton(ICON_MY_TRASH_CAN))
        m_ringbuffer_sink->clear_messages();
    ImGui::Tooltip("Clear all messages.");
    ImGui::SameLine();
    ImGui::IconButton(m_auto_scroll ? ICON_MY_LOCK : ICON_MY_LOCK_OPEN, &m_auto_scroll);
    ImGui::Tooltip(m_auto_scroll ? "Turn auto scrolling off." : "Turn auto scrolling on.");
    ImGui::SameLine();
    ImGui::IconButton(m_wrap_text ? ICON_MY_TEXT_WRAP_ON : ICON_MY_TEXT_WRAP_OFF, &m_wrap_text);
    ImGui::Tooltip(m_wrap_text ? "Turn line wrapping off." : "Turn line wrapping on.");

    auto window_flags = m_wrap_text
                            ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                            : ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar;

    ImGui::BeginChild("##spdlog window", ImVec2(0.f, 0.f), ImGuiChildFlags_FrameStyle, window_flags);
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
        auto default_font = ImGui::GetFont();
        ImGui::PushFont(console_font, size);

        int  item_num           = 0;
        bool did_copy           = false;
        bool scrolled_to_target = false;
        m_ringbuffer_sink->iterate(
            [this, &item_num, &did_copy, &scrolled_to_target,
             default_font](const typename spdlog::sinks::ringbuffer_color_sink_mt::LogItem &msg) -> bool
            {
                ++item_num;
                bool is_scroll_target = m_scroll_to_seq && msg.seq == *m_scroll_to_seq;
                if (!m_ringbuffer_sink->should_log(msg.level) ||
                    !m_filter.PassFilter(msg.message.c_str(), msg.message.c_str() + msg.message.size()))
                {
                    // the requested item exists but is filtered out of view; drop the request rather
                    // than waiting forever for a match that will never render
                    if (is_scroll_target)
                        m_scroll_to_seq.reset();
                    return true;
                }

                bool invalid_color_range = msg.color_range_end <= msg.color_range_start ||
                                           std::min(msg.color_range_start, msg.color_range_end) >= msg.message.length();

                // compute the size of the selectable, and draw it
                ImVec2 selectable_size{0.f, 0.f};
                {
                    float prefix_width =
                        ImGui::CalcTextSize(msg.message.c_str(), msg.message.c_str() + msg.color_range_end).x;
                    selectable_size.y =
                        ImGui::CalcTextSize(msg.message.c_str() + (invalid_color_range ? 0 : msg.color_range_end),
                                            nullptr, false,
                                            m_wrap_text ? (ImGui::GetContentRegionAvail().x - prefix_width) : -1.f)
                            .y;
                    selectable_size.x = ImGui::GetContentRegionAvail().x + (m_wrap_text ? 0 : ImGui::GetScrollMaxX());
                }

                ImGui::PushID(item_num);
                if (ImGui::Selectable("##log item selectable", false, ImGuiSelectableFlags_AllowOverlap,
                                      selectable_size))
                {
                    did_copy = true;
                    ImGui::SetClipboardText(msg.message.c_str() + (invalid_color_range ? 0 : msg.color_range_end));
                }
                ImGui::PopID();
                ImGui::PushFont(default_font, 0.f);
                ImGui::SetItemTooltip("Click to copy to clipboard");
                ImGui::PopFont();
                ImGui::SameLine(ImGui::GetStyle().ItemInnerSpacing.x);

                // if color range not specified or not valid, just draw all the text with default color
                if (invalid_color_range)
                {
                    if (m_wrap_text)
                        ImGui::TextWrapped("%s", msg.message.c_str());
                    else
                        ImGui::TextUnformatted(msg.message.c_str());
                }
                else
                {
                    // insert the text before the color range
                    ImGui::TextUnformatted(msg.message.c_str(), msg.message.c_str() + msg.color_range_start);
                    ImGui::SameLine(0.f, 0.f);

                    // insert the colorized text
                    ImGui::PushStyleColor(ImGuiCol_Text, m_level_colors.at(msg.level));
                    ImGui::TextUnformatted(msg.message.c_str() + msg.color_range_start,
                                           msg.message.c_str() + msg.color_range_end);
                    ImGui::SameLine(0.f, 0.f);
                    ImGui::PopStyleColor();

                    // insert the text after the color range with default format
                    if (m_wrap_text)
                        ImGui::TextWrapped("%s", msg.message.substr(msg.color_range_end).c_str());
                    else
                        ImGui::TextUnformatted(msg.message.c_str() + msg.color_range_end);
                }

                if (is_scroll_target)
                {
                    ImGui::SetScrollHereY(0.5f);
                    m_scroll_to_seq.reset();
                    scrolled_to_target = true;
                }

                return true;
            });

        if (did_copy)
            spdlog::trace("Copied a log item to clipboard"); // the log sink is locked during the iterate loop above, so
                                                             // this needs to happen outside

        // ImGui::PopStyleColor();

        // still consume has_new_items() every frame so it doesn't report a stale batch once autoscroll
        // resumes; just don't act on it the same frame we scrolled to an explicit target above
        bool has_new = m_ringbuffer_sink->has_new_items();
        if (!scrolled_to_target && has_new && m_auto_scroll)
            ImGui::SetScrollHereY(1.f);

        ImGui::PopFont();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

void SpdLogWindow::clear() { m_ringbuffer_sink->clear_messages(); }

void SpdLogWindow::set_level_color(spdlog::level::level_enum level, ImU32 color)
{
    m_level_colors.at(static_cast<size_t>(level)) = color;
}
ImU32 SpdLogWindow::get_level_color(spdlog::level::level_enum level)
{
    return m_level_colors.at(static_cast<size_t>(level));
}

string TruncatedText(const string &filename, const string &icon)
{
    const float avail_width = GetContentRegionAvail().x;

    // Common case: the whole thing already fits, nothing to truncate.
    if (CalcTextSize((icon + filename).c_str()).x <= avail_width)
        return filename;

    // Filenames here are `file:part.layer.channel` paths whose meaningful part is the end, so this keeps the
    // tail and elides the front, the opposite of ImGui's RenderTextEllipsis(). Same technique as that
    // function: accumulate per-glyph advances in a single pass, here walking backwards from the end.
    static const string ellipsis = " ...";
    const float         budget   = avail_width - CalcTextSize((icon + ellipsis).c_str()).x;

    ImFont      *font  = GetFont();
    ImFontBaked *baked = font->GetFontBaked(GetFontSize());

    const char *begin = filename.c_str();
    const char *end   = begin + filename.size();
    const char *cut   = end;
    float       width = 0.f;
    while (cut > begin)
    {
        // Step back to the start (lead byte) of the codepoint immediately before `cut`.
        const char *prev = cut - 1;
        while (prev > begin && (static_cast<unsigned char>(*prev) & 0xC0) == 0x80) --prev;

        unsigned int codepoint = 0;
        ImTextCharFromUtf8(&codepoint, prev, cut);
        float advance = baked->GetCharAdvance((ImWchar)codepoint);

        // Always keep at least the final character, even if it alone exceeds the budget, so the
        // result is never a bare ellipsis.
        if (cut != end && width + advance > budget)
            break;

        width += advance;
        cut = prev;
    }

    return cut == begin ? filename : ellipsis + string(cut, end);
};

ImVec2 IconSize() { return CalcTextSize(ICON_MY_WIDEST); }

ImVec2 IconButtonSize()
{
    return {ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};
    // return {IconSize().x + 2 * ImGui::GetStyle().ItemInnerSpacing.x, 0.f};
}

bool IconButton(const char *icon, bool *v, const ImVec2 &size)
{
    auto   asz = IconButtonSize();
    ImVec2 sz  = size;
    sz.x       = sz.x < 0.f ? asz.x : sz.x;
    sz.y       = sz.y < 0.f ? asz.y : sz.y;

    bool toggle = v != nullptr;
    if (toggle)
    {
        auto bh = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
        auto ba = ImGui::GetColorU32(ImGuiCol_FrameBg);
        auto fb = ImGui::GetColorU32(ImGuiCol_ButtonActive);
        auto b  = ImGui::GetColorU32(ImGuiCol_Button);

        ImGui::PushStyleColor(ImGuiCol_ButtonActive, fb);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, *v ? ba : bh);
        ImGui::PushStyleColor(ImGuiCol_Button, *v ? ba : b);
    }

    bool ret = ImGui::Button(icon, sz);

    if (v && ret)
        *v = !*v;

    if (toggle)
        ImGui::PopStyleColor(3);

    return ret;
}

bool FlatButton(const char *label, bool active, const ImVec2 &size)
{
    // clearing ImGuiCol_Button alone isn't enough: the theme sets a nonzero FrameBorderSize, which
    // Button() draws unconditionally in ImGuiCol_Border regardless of the fill color
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImGui::GetColorU32(ImGuiCol_FrameBg) : IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ButtonHovered, active ? 0.75f : 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.75f));
    bool ret = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return ret;
}

void AddTextAligned(ImDrawList *draw_list, float2 pos, ImU32 color, const string &text, float2 align)
{
    draw_list->AddText(pos - align * float2{ImGui::CalcTextSize(text.c_str())}, color, text.c_str());
}

bool MenuItemEx(const std::string &label, const std::string &icon, const std::string &shortcut, bool *p_selected,
                bool enabled)
{
    if (MenuItemEx(label.c_str(), icon.c_str(), shortcut.c_str(), p_selected ? *p_selected : false, enabled))
    {
        if (p_selected)
            *p_selected = !*p_selected;
        return true;
    }
    return false;
}

// from https://github.com/ocornut/imgui/issues/3379#issuecomment-1678718752
void ScrollWhenDraggingOnVoid(const ImVec2 &delta, ImGuiMouseButton mouse_button)
{
    ImGuiContext &g       = *ImGui::GetCurrentContext();
    ImGuiWindow  *window  = g.CurrentWindow;
    bool          hovered = false;
    bool          held    = false;
    ImGuiID       id      = window->GetID("##scrolldraggingoverlay");
    ImGui::KeepAliveID(id);
    ImGuiButtonFlags button_flags = (mouse_button == 0)   ? ImGuiButtonFlags_MouseButtonLeft
                                    : (mouse_button == 1) ? ImGuiButtonFlags_MouseButtonRight
                                                          : ImGuiButtonFlags_MouseButtonMiddle;
    if (g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
        ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, button_flags);
    if (held && delta.x != 0.0f)
        ImGui::SetScrollX(window, window->Scroll.x + delta.x);
    if (held && delta.y != 0.0f)
        ImGui::SetScrollY(window, window->Scroll.y + delta.y);
}

void PushRowColors(bool is_current, bool is_reference, bool reference_mod, bool is_selected)
{
    float4 active  = GetStyleColorVec4(ImGuiCol_HeaderActive);
    float4 header  = GetStyleColorVec4(ImGuiCol_Header);
    float4 hovered = GetStyleColorVec4(ImGuiCol_HeaderHovered);

    // Called once per visible row per frame, so the derived colors below are cached and rederived only when
    // the theme changes: comparing three colors is much cheaper than six HSV<->RGB conversions.
    static float4 cached_hovered{-1.f}, cached_header{-1.f}, cached_active{-1.f};
    static float4 hovered_c, header_c, active_c, hovered_avg, header_avg, active_avg, header_dim;
    if (hovered != cached_hovered || header != cached_header || active != cached_active)
    {
        cached_hovered = hovered;
        cached_header  = header;
        cached_active  = active;

        // "complementary" color (for reference image/channel group) is shifted by 2/3 in hue
        constexpr float3 hsv_adjust = float3{0.67f, 0.f, -0.2f};
        hovered_c = float4{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(hovered.xyz()) + hsv_adjust), hovered.w};
        header_c  = float4{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(header.xyz()) + hsv_adjust), header.w};
        active_c  = float4{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(active.xyz()) + hsv_adjust), active.w};

        // the average between the two is used when a row is both current and reference
        hovered_avg = 0.5f * (hovered_c + hovered);
        header_avg  = 0.5f * (header_c + header);
        active_avg  = 0.5f * (active_c + active);

        // A selected row that isn't the current one is the same color at three quarters strength, done with
        // the alpha: against the light theme's background a darker blue would read as more emphasis, not
        // less.
        header_dim = float4{header.xyz(), 0.75f * header.w};
    }

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, reference_mod ? (is_current ? hovered_avg : hovered_c)
                                                                : (is_reference ? hovered_avg : hovered));
    ImGui::PushStyleColor(ImGuiCol_Header, is_reference ? (is_current ? header_avg : header_c)
                                                        : (is_current || !is_selected ? header : header_dim));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, reference_mod ? (is_current ? active_avg : active_c) : active);
}

bool TreeRow(const void *id, ImGuiTreeNodeFlags flags, const char *label, const std::function<void()> &leading_column,
             const std::function<void()> &before_node)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (leading_column)
        leading_column();

    ImGui::TableNextColumn();
    before_node();
    bool open = ImGui::TreeNodeEx(id, flags, "%s", label);
    ImGui::PopStyleColor(3);
    return open;
}

// Splits `total_width` (already reduced by inter-item spacing) into `num_components` columns the same way
// Dear ImGui's ColorEdit4 does internally, remainder folded into the last column, so an independently
// computed header row still lines up with the value boxes below it.
static void split_channel_widths(float total_width, int num_components, float out_widths[4])
{
    float prev_split = 0.f;
    for (int c = 0; c < num_components; ++c)
    {
        float next_split = IM_TRUNC(total_width * (c + 1) / num_components);
        out_widths[c]    = ImMax(next_split - prev_split, 1.0f);
        prev_split       = next_split;
    }
}

// Same left-edge tint ColorEdit4 draws on its R/G/B/A component sliders (see GDefaultRgbaColorMarkers in
// imgui_widgets.cpp). ImGuiSliderFlags_ColorMarkers is only wired up in Drag/SliderScalar, not
// InputFloat/InputScalar, so this is drawn via the public RenderColorComponentMarker() those widgets call.
static const ImU32 rgba_marker_colors[4] = {IM_COL32(240, 20, 20, 255), IM_COL32(20, 240, 20, 255),
                                            IM_COL32(20, 20, 240, 255), IM_COL32(140, 140, 140, 255)};

static const char *channel_display_mode_names[ChannelDisplayMode_COUNT] = {
    "Raw values", "Exposure-adjusted", "Displayed (32-bit)", "Displayed (8-bit)", "Displayed (hex)"};

void ChannelValuesRow(const char *id, const float *raw, const float *displayed, int num_components,
                      ImGuiDataType data_type, const char *format, float exposure_gain, int *mode,
                      ChannelDisplayModeMask enabled_modes, bool allow_copy, bool show_swatch,
                      const ImVec4 &swatch_color, const std::string &label, float total_width, bool content_disabled,
                      bool show_color_markers, const std::function<void()> &extra_popup_items)
{
    ImGui::PushID(id);

    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float sz      = ImGui::GetFrameHeight();
    float label_w = label.empty() ? 0.f : ImGui::CalcTextSize(label.c_str()).x;

    // `total_width`, when given, is the footprint for the whole row (boxes + swatch + label) -- a PE table
    // column's width, say, which the swatch must fit inside or be clipped by the cell's clip rect. The box
    // area is what is left after reserving the swatch (always) and the label (if given). Without it, the
    // boxes take their natural CalcItemWidth() and the row grows to fit the swatch and label beyond them.
    float w_full, row_width;
    if (total_width > 0.f)
    {
        row_width = total_width;
        w_full    = ImMax(total_width - (spacing + sz) - (label.empty() ? 0.f : spacing + label_w), 1.f);
    }
    else
    {
        w_full    = ImGui::CalcItemWidth();
        row_width = w_full + spacing + sz + (label.empty() ? 0.f : spacing + label_w);
    }

    float w_items = w_full - spacing * (num_components - 1);
    float widths[4];
    split_channel_widths(w_items, num_components, widths);

    // 8-bit/hex are both simple derivations of `displayed` (already run through the app's tonemap/gamma/sRGB
    // pipeline) -- computed once up front regardless of *mode so Copy-to-clipboard can use them too.
    int ldr[4] = {0, 0, 0, 0};
    if (displayed)
        for (int c = 0; c < num_components; ++c)
        {
            // IM_ROUND casts to int internally, and neither it nor ImClamp survives a non-finite input: NaN
            // slips through both of ImClamp's comparisons, and the cast is then undefined.
            float v = displayed[c] * 255.f;
            ldr[c]  = std::isfinite(v) ? (int)ImClamp(IM_ROUND(ImClamp(v, 0.f, 255.f)), 0.f, 255.f) : 0;
        }

    // The whole row is one click target opening the Copy/Display-as popup. Real ImGui widgets (InputFloat,
    // ColorButton, ...) call ButtonBehavior() -> ItemHoverable(), which claims g.HoveredId regardless of
    // BeginDisabled(), and SetNextItemAllowOverlap() here does not stop a later inert widget taking that
    // claim back -- so the boxes, swatch and label below are drawn straight onto the draw list, with no
    // ImGui item or ID of their own. The click target is outside BeginDisabled(content_disabled), so its
    // popup stays usable when the sample itself is meaningless.
    bool clicked = ImGui::InvisibleButton("##click", ImVec2{row_width, sz});
    if (clicked)
        ImGui::OpenPopup("##dropdown");
    ImGui::SetItemTooltip(extra_popup_items ? "Click to change what is shown and how%s"
                                            : "Click to change value format%s",
                          allow_copy ? ", or copy to clipboard." : ".");

    ImVec2            row_pos   = ImGui::GetItemRectMin();
    ImDrawList       *draw_list = ImGui::GetWindowDrawList();
    const ImGuiStyle &style     = ImGui::GetStyle();

    ImGui::BeginDisabled(content_disabled);

    // Read-only look: no fill or border, the optional color-component marker being the only framing left.
    // Text is clipped to the box's bounds through ImGui's standard clipped-text renderer, so a long value
    // can't spill into the next box. Only the numeric text renders in the mono font; the label, tooltip and
    // popup stay in the ambient one.
    auto draw_box = [&](ImVec2 p_min, float w, const char *text)
    {
        ImVec2 p_max{p_min.x + w, p_min.y + sz};
        ImGui::PushFont(hdrview()->font("mono regular"), ImGui::GetStyle().FontSizeBase);
        ImVec2 text_size = ImGui::CalcTextSize(text);
        ImGui::RenderTextClipped(ImVec2{p_min.x + style.FramePadding.x, p_min.y + (sz - text_size.y) * 0.5f}, p_max,
                                 text, nullptr, &text_size);
        ImGui::PopFont();
        return ImRect(p_min, p_max);
    };

    char  text_buf[64];
    float x = row_pos.x;
    if (*mode == ChannelDisplayMode_DisplayedHex)
    {
        uint32_t hex = 0;
        for (int c = 0; c < 4; ++c) hex |= (uint32_t)(c < num_components ? ldr[c] : 0) << (8 * (3 - c));
        ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), "#%08X", hex);
        draw_box(ImVec2{x, row_pos.y}, w_full, text_buf);
        // The per-component loop below advances by (width + spacing) on every iteration, including the last,
        // so match that here or the swatch and label sit a spacing further left in hex mode.
        x += w_full + spacing;
    }
    else
    {
        for (int c = 0; c < num_components; ++c)
        {
            switch (*mode)
            {
            case ChannelDisplayMode_Raw:
                if (data_type == ImGuiDataType_Float)
                    ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), format, raw[c]);
                else
                    ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), format, (int)raw[c]);
                break;
            case ChannelDisplayMode_ExposureAdjusted:
                ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), "%g", raw[c] * exposure_gain);
                break;
            case ChannelDisplayMode_Displayed32:
                ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), "%g", displayed ? displayed[c] : 0.f);
                break;
            case ChannelDisplayMode_Displayed8:
            default: ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), "%d", ldr[c]); break;
            }

            ImRect box_rect = draw_box(ImVec2{x, row_pos.y}, widths[c], text_buf);
            if (show_color_markers)
                ImGui::RenderColorComponentMarker(box_rect, rgba_marker_colors[c % 4], style.FrameRounding);
            x += widths[c] + spacing;
        }
    }

    // Always reserve the swatch's footprint (real swatch, or an equally sized blank gap) so rows without one
    // still line up with rows that have one.
    if (show_swatch)
    {
        ImVec2 p_min{x, row_pos.y}, p_max{x + sz, row_pos.y + sz};
        if (swatch_color.w < 1.0f)
        {
            // The swatch is a plain draw-list rect, not a ColorButton (see the click-target note above), so
            // the half-solid/half-checkerboard split ImGui::ColorButton's AlphaPreviewHalf flag draws is
            // replicated here, down to the rounding clamp and the inward bb shrink it applies for its
            // border; anything approximated visibly fails to match a real ColorEdit.
            float  grid_step = ImMin(sz, sz) / 2.99f;
            float  rounding  = ImMin(style.FrameRounding, grid_step * 0.5f);
            float  off       = -0.75f;
            ImRect bb_inner{p_min, p_max};
            bb_inner.Expand(off);
            float  mid_x = IM_ROUND((bb_inner.Min.x + bb_inner.Max.x) * 0.5f);
            ImVec4 opaque{swatch_color.x, swatch_color.y, swatch_color.z, 1.f};
            ImGui::RenderColorRectWithAlphaCheckerboard(draw_list, ImVec2{bb_inner.Min.x + grid_step, bb_inner.Min.y},
                                                        bb_inner.Max, ImGui::GetColorU32(swatch_color), grid_step,
                                                        ImVec2{-grid_step + off, off}, rounding,
                                                        ImDrawFlags_RoundCornersRight);
            draw_list->AddRectFilled(bb_inner.Min, ImVec2{mid_x, bb_inner.Max.y}, ImGui::GetColorU32(opaque), rounding,
                                     ImDrawFlags_RoundCornersLeft);
        }
        else
            draw_list->AddRectFilled(p_min, p_max, ImGui::ColorConvertFloat4ToU32(swatch_color), style.FrameRounding);
    }
    x += sz;

    if (!label.empty())
    {
        x += spacing;
        ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        draw_list->AddText(ImVec2{x, row_pos.y + (sz - text_size.y) * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text),
                           label.c_str());
    }

    ImGui::EndDisabled();

    if (ImGui::BeginPopup("##dropdown"))
    {
        if (allow_copy && ImGui::Selectable("Copy to clipboard"))
        {
            string buf;
            switch (*mode)
            {
            case ChannelDisplayMode_Raw:
            case ChannelDisplayMode_ExposureAdjusted:
            {
                float gain = *mode == ChannelDisplayMode_ExposureAdjusted ? exposure_gain : 1.f;
                if (num_components == 4)
                    buf = fmt::format("({:g}, {:g}, {:g}, {:g})", raw[0] * gain, raw[1] * gain, raw[2] * gain,
                                      raw[3] * gain);
                else if (num_components == 3)
                    buf = fmt::format("({:g}, {:g}, {:g})", raw[0] * gain, raw[1] * gain, raw[2] * gain);
                else if (num_components == 2)
                    buf = fmt::format("({:g}, {:g})", raw[0] * gain, raw[1] * gain);
                else
                    buf = fmt::format("{:g}", raw[0] * gain);
                break;
            }
            case ChannelDisplayMode_Displayed32:
                if (displayed)
                {
                    if (num_components == 4)
                        buf = fmt::format("({:g}, {:g}, {:g}, {:g})", displayed[0], displayed[1], displayed[2],
                                          displayed[3]);
                    else if (num_components == 3)
                        buf = fmt::format("({:g}, {:g}, {:g})", displayed[0], displayed[1], displayed[2]);
                    else if (num_components == 2)
                        buf = fmt::format("({:g}, {:g})", displayed[0], displayed[1]);
                    else
                        buf = fmt::format("{:g}", displayed[0]);
                }
                break;
            case ChannelDisplayMode_Displayed8:
                buf = fmt::format("({:d}, {:d}, {:d}, {:d})", ldr[0], ldr[1], ldr[2], ldr[3]);
                break;
            case ChannelDisplayMode_DisplayedHex:
                buf = fmt::format("#{:02X}{:02X}{:02X}{:02X}", ldr[0], ldr[1], ldr[2], ldr[3]);
                break;
            }
            ImGui::SetClipboardText(buf.c_str());
        }
        if (extra_popup_items)
            extra_popup_items();
        ImGui::SeparatorText("Display as:");
        for (int m = 0; m < ChannelDisplayMode_COUNT; ++m)
        {
            ImGui::BeginDisabled((enabled_modes & (1u << m)) == 0);
            if (ImGui::Selectable(channel_display_mode_names[m], *mode == m))
                *mode = m;
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

void ChannelValuesRowHeader(const std::string *names, int num_components, float total_width, bool reserve_swatch_gap)
{
    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float w_full  = total_width > 0.f ? total_width : ImGui::CalcItemWidth();
    if (reserve_swatch_gap)
        w_full = ImMax(w_full - (spacing + ImGui::GetFrameHeight()), 1.f);
    float w_items = w_full - spacing * (num_components - 1);
    float widths[4];
    split_channel_widths(w_items, num_components, widths);

    // Drawn straight onto the draw list, like ChannelValuesRow's boxes and swatch: ImGui's cursor and
    // same-line bookkeeping across a loop of item calls drifts, leaving only the first label at row_y.
    ImVec2      row_pos   = ImGui::GetCursorScreenPos();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    float       line_h    = ImGui::GetTextLineHeight();
    ImU32       text_col  = ImGui::GetColorU32(ImGuiCol_Text);

    // Left-aligned, with the same FramePadding.x inset the value boxes use for their own text, so a column's
    // header lines up with its value text and not just its box. AddText() picks up the pushed bold font like
    // any ImGui-level text call, since PushFont() also updates the shared draw-list state.
    ImGui::PushFont(hdrview()->font("sans bold"), 0.f);
    float x = row_pos.x;
    for (int c = 0; c < num_components; ++c)
    {
        draw_list->AddText(ImVec2{x + ImGui::GetStyle().FramePadding.x, row_pos.y}, text_col, names[c].c_str());
        x += widths[c] + ImGui::GetStyle().ItemInnerSpacing.x;
    }
    ImGui::PopFont();

    // Reserve the row's layout space so whatever's drawn next lands below it.
    ImGui::Dummy(ImVec2{x - ImGui::GetStyle().ItemInnerSpacing.x - row_pos.x, line_h});
}

void UnderLine(ImColor c, float raise)
{
    ImVec2 mi = ImGui::GetItemRectMin();
    ImVec2 ma = ImGui::GetItemRectMax();

    mi.y = ma.y = ma.y - raise * ImGui::GetFontSize();

    float lineThickness = ImGui::GetFontSize() / 14.5f;
    ImGui::GetWindowDrawList()->AddLine(mi, ma, c, lineThickness);
}

void HyperlinkText(const char *label, const char *url)
{
    ImGuiContext &g = *GImGui;
    if (!url)
        url = label;
    if (TextLink(label))
        if (g.PlatformIO.Platform_OpenInShellFn)
            g.PlatformIO.Platform_OpenInShellFn(&g, url);
    auto prev_size = ImGui::GetStyle().FontSizeBase;
    auto font      = ImGui::GetFont();
    PopFont();
    // PushFont(GetIO().FontDefault, 12.f);
    SetItemTooltip("%s '%s'", ICON_MY_LINK, url);
    // PopFont();
    PushFont(font, prev_size);
    if (BeginPopupContextItem())
    {
        if (MenuItem(LocalizeGetMsg(ImGuiLocKey_CopyLink)))
            SetClipboardText(url);
        EndPopup();
    }
}

// copied from imgui.cpp
static ImGuiKeyChord GetModForLRModKey(ImGuiKey key)
{
    if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl)
        return ImGuiMod_Ctrl;
    if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift)
        return ImGuiMod_Shift;
    if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
        return ImGuiMod_Alt;
    if (key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper)
        return ImGuiMod_Super;
    return ImGuiMod_None;
}

// Return translated names
// Lifetime of return value: valid until next call to GetKeyChordNameTranslated or GetKeyChordName
const char *GetKeyChordNameTranslated(ImGuiKeyChord key_chord)
{
    ImGuiContext &g = *GImGui;

    const ImGuiKey key = (ImGuiKey)(key_chord & ~ImGuiMod_Mask_);
    if (IsLRModKey(key))
        key_chord &= ~GetModForLRModKey(key); // Return "Ctrl+LeftShift" instead of "Ctrl+Shift+LeftShift"
    ImFormatString(g.TempKeychordName, IM_ARRAYSIZE(g.TempKeychordName), "%s%s%s%s%s",
                   (key_chord & ImGuiMod_Ctrl) ? (g.IO.ConfigMacOSXBehaviors ? "Cmd+" : "Ctrl+") : "",    // avoid wrap
                   (key_chord & ImGuiMod_Shift) ? "Shift+" : "",                                          //
                   (key_chord & ImGuiMod_Alt) ? (g.IO.ConfigMacOSXBehaviors ? "Option+" : "Alt+") : "",   //
                   (key_chord & ImGuiMod_Super) ? (g.IO.ConfigMacOSXBehaviors ? "Ctrl+" : "Super+") : "", //
                   (key != ImGuiKey_None) ? GetKeyName(key) : "");                                        //
    size_t len;
    if (key == ImGuiKey_None)
        if ((len = strlen(g.TempKeychordName)) != 0) // Remove trailing '+'
            g.TempKeychordName[len - 1] = 0;
    return g.TempKeychordName;
}
// // Return translated names
// // Lifetime of return value: valid until next call to GetKeyChordNameTranslated or GetKeyChordName
// const char *GetKeyChordNameTranslated(ImGuiKeyChord key_chord)
// {
//     ImGuiContext &g = *GImGui;

//     const ImGuiKey key = (ImGuiKey)(key_chord & ~ImGuiMod_Mask_);
//     if (IsLRModKey(key))
//         key_chord &= ~GetModForLRModKey(key); // Return "Ctrl+LeftShift" instead of "Ctrl+Shift+LeftShift"
//     ImFormatString(
//         g.TempKeychordName, IM_ARRAYSIZE(g.TempKeychordName), "%s%s%s%s%s",
//         (key_chord & ImGuiMod_Ctrl) ? (g.IO.ConfigMacOSXBehaviors ? ICON_MY_KEY_COMMAND : ICON_MY_KEY_CONTROL)
//                                     : "",                      // avoid wrap
//         (key_chord & ImGuiMod_Shift) ? ICON_MY_KEY_SHIFT : "", //
//         (key_chord & ImGuiMod_Alt) ? (g.IO.ConfigMacOSXBehaviors ? ICON_MY_KEY_OPTION : ICON_MY_KEY_OPTION) : "", //
//         (key_chord & ImGuiMod_Super) ? (g.IO.ConfigMacOSXBehaviors ? ICON_MY_KEY_CONTROL : "Super+") : "",        //
//         (key != ImGuiKey_None) ? GetKeyName(key) : "");                                                           //
//     size_t len;
//     // if (key == ImGuiKey_None)
//     //     if ((len = strlen(g.TempKeychordName)) != 0) // Remove trailing '+'
//     //         g.TempKeychordName[len - 1] = 0;
//     return g.TempKeychordName;
// }

bool GlobalShortcut(const ImGuiKeyChord &chord, ImGuiInputFlags flags)
{
    return ImGui::Shortcut(chord, flags | ImGuiInputFlags_RouteGlobal);
}

void DrawLabeledRect(ImDrawList *draw_list, const Box2f &rect, ImU32 col, const string &text, const float2 &align,
                     bool draw_label)
{
    constexpr float  thickness = 3.f;
    constexpr float2 fudge     = float2{thickness * 0.5f - 0.5f, -(thickness * 0.5f - 0.5f)};
    const float2     pad       = float2{0.25, 0.125} * ImGui::GetFontSize();

    draw_list->AddRect(rect.min, rect.max, col, 0.f, thickness, ImDrawFlags_None);

    if (!draw_label)
        return;

    float2 shifted_align = (2.f * align - float2{1.f});
    float2 text_size     = ImGui::CalcTextSize(text.c_str());
    float2 tab_size      = text_size + pad * 2.f;
    float  fade          = 1.f - smoothstep(0.5f * rect.size().x, 1.0f * rect.size().x, tab_size.x);
    if (fade == 0.0f)
        return;

    Box2f tab_box = {float2{0.f}, tab_size};
    tab_box.move_min_to(
        // move to the correct corner while accounting for the tab size
        rect.min + align * (rect.size() - tab_size) +
        // shift the tab outside the rectangle
        shifted_align * (fudge + float2{0, tab_size.y}));
    draw_list->AddRectFilled(tab_box.min, tab_box.max, ImGui::GetColorU32(col, fade),
                             std::clamp(ImGui::GetStyle().TabRounding, 0.0f, tab_size.x * 0.5f - 1.0f),
                             shifted_align.y < 0.f ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersBottom);
    ImGui::AddTextAligned(draw_list, tab_box.min + align * tab_box.size() - shifted_align * pad,
                          ImGui::GetColorU32(ImGuiCol_Text, fade), text, align);
}

void DrawCrosshairs(ImDrawList *draw_list, const float2 &pos, const string &subscript)
{
    ImGui::AddTextAligned(draw_list, pos + int2{1, 1}, IM_COL32_BLACK, ICON_MY_WATCHED_PIXEL, {0.5f, 0.5f});
    ImGui::AddTextAligned(draw_list, pos, IM_COL32_WHITE, ICON_MY_WATCHED_PIXEL, {0.5f, 0.5f});

    if (subscript.length() == 0)
        return;

    ImGui::AddTextAligned(draw_list, pos + int2{1, 1}, IM_COL32_BLACK, subscript.c_str(), {-0.15f, -0.15f});
    ImGui::AddTextAligned(draw_list, pos, IM_COL32_WHITE, subscript.c_str(), {-0.15f, -0.15f});
}

// bool DragInt4(const char *label, int *p_data, const char *formats[], ImGuiSliderFlags flags)
// {
//     ImGuiWindow *window = GetCurrentWindow();
//     if (window->SkipItems)
//         return false;

//     ImGuiContext &g             = *GImGui;
//     bool          value_changed = false;
//     BeginGroup();
//     PushID(label);
//     PushMultiItemsWidths(4, CalcItemWidth());
//     for (int i = 0; i < 4; i++)
//     {
//         PushID(i);
//         if (i > 0)
//             SameLine(0, g.Style.ItemInnerSpacing.x);
//         value_changed |= DragInt("", p_data, 1.f, 0, 0, formats[i], flags);
//         PopID();
//         PopItemWidth();
//         ++p_data;
//     }
//     PopID();

//     const char *label_end = FindRenderedTextEnd(label);
//     if (label != label_end)
//     {
//         SameLine(0, g.Style.ItemInnerSpacing.x);
//         TextEx(label, label_end);
//     }

//     EndGroup();
//     return value_changed;
// }

// bool InputFloat4(const char *label, float *p_data, const char *formats[], ImGuiSliderFlags flags)
// {
//     ImGuiWindow *window = GetCurrentWindow();
//     if (window->SkipItems)
//         return false;

//     ImGuiContext &g             = *GImGui;
//     bool          value_changed = false;
//     BeginGroup();
//     PushID(label);
//     PushMultiItemsWidths(4, CalcItemWidth());
//     for (int i = 0; i < 4; i++)
//     {
//         PushID(i);
//         if (i > 0)
//             SameLine(0, g.Style.ItemInnerSpacing.x);
//         value_changed |= InputFloat("", p_data, 0.f, 0.f, formats[i], flags);
//         PopID();
//         PopItemWidth();
//         ++p_data;
//     }
//     PopID();

//     const char *label_end = FindRenderedTextEnd(label);
//     if (label != label_end)
//     {
//         SameLine(0, g.Style.ItemInnerSpacing.x);
//         TextEx(label, label_end);
//     }

//     EndGroup();
//     return value_changed;
// }

/// Shared by both MenuItem() overloads; `name` is what the item is labelled and tooltipped with.
static void menu_item(const Action &a, const std::string &name, bool include_name)
{
    if (a.needs_menu)
    {
        if (ImGui::BeginMenuEx(name.c_str(), a.icon.c_str(), a.enabled()))
        {
            a.callback();
            ImGui::EndMenu();
        }
    }
    else
    {
        if (ImGui::MenuItemEx(include_name ? name : a.icon, include_name ? a.icon : "",
                              ImGui::GetKeyChordNameTranslated(a.chord), a.p_selected, a.enabled()))
            a.callback();
        if (!include_name)
            ImGui::Tooltip(fmt::format("{}{}{}", name,
                                       a.chord ? fmt::format(" ({})", ImGui::GetKeyChordNameTranslated(a.chord)) : "",
                                       a.tooltip.empty() ? "" : fmt::format("\n\n{}", a.tooltip))
                               .c_str());
        else if (!a.tooltip.empty())
            ImGui::Tooltip(fmt::format("{}{}", a.tooltip.c_str(),
                                       a.chord ? fmt::format(" ({})", ImGui::GetKeyChordNameTranslated(a.chord)) : "")
                               .c_str());
    }
}

void MenuItem(const Action &a, bool include_name) { menu_item(a, a.names[0], include_name); }

void MenuItem(const Action &a, const std::string &label) { menu_item(a, label, true); }

void IconButton(const Action &a, bool include_name)
{
    const auto &name = a.names[0];
    ImGui::BeginDisabled(a.enabled() == false);

    if (include_name)
    {
        if (ImGui::IconButton(fmt::format("{} {}", a.icon, name).c_str(), a.p_selected, ImVec2(0, -1)))
            a.callback();
        if (a.chord)
            ImGui::Tooltip(fmt::format("({}){}", ImGui::GetKeyChordNameTranslated(a.chord),
                                       a.tooltip.empty() ? "" : fmt::format("\n\n{}", a.tooltip))
                               .c_str());
        else
            ImGui::Tooltip(fmt::format("{}", a.tooltip.empty() ? "" : fmt::format("{}", a.tooltip)).c_str());
    }
    else
    {
        if (ImGui::IconButton(fmt::format("{}##{}", a.icon, name).c_str(), a.p_selected))
            a.callback();
        if (a.chord)
            ImGui::Tooltip(fmt::format("{} ({}){}", name, ImGui::GetKeyChordNameTranslated(a.chord),
                                       a.tooltip.empty() ? "" : fmt::format("\n\n{}", a.tooltip))
                               .c_str());
        else
            ImGui::Tooltip(
                fmt::format("{}{}", name, a.tooltip.empty() ? "" : fmt::format("\n\n{}", a.tooltip)).c_str());
    }

    ImGui::EndDisabled();
}

void Checkbox(const Action &a)
{
    ImGui::Checkbox(a.names[0].c_str(), a.p_selected);
    if (!a.tooltip.empty() || a.chord)
    {
        string parenthesized_chord = a.chord ? fmt::format("({})", ImGui::GetKeyChordNameTranslated(a.chord)) : "";
        string tooltip             = fmt::format("{}{}", a.tooltip, parenthesized_chord);
        ImGui::Tooltip(tooltip.c_str());
    }
}

bool BeginModalDialog(const char *title, bool &open, DialogPosition position, ImGuiWindowFlags flags)
{
    // HelloImGui renders a couple of frames to figure out sizes before the first real frame; guard against
    // opening a dialog that starts out already-open (e.g. the About box on first launch) before then.
    if (open && ImGui::GetFrameCount() > 2)
    {
        ImGui::OpenPopup(title);
        open = false;
    }

    switch (position)
    {
    case DialogPosition::TopCenter:
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2, 5.f * HelloImGui::EmSize()),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
        break;
    case DialogPosition::Center:
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        break;
    case DialogPosition::None: break;
    }

    return ImGui::BeginPopupModal(title, nullptr, flags);
}

void RowSpan::take()
{
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();

    right = ImMax(right, mx.x);

    const float y = 0.5f * (mn.y + mx.y);
    if (count++ == 0)
        first = y;
    last = y;
}

bool RowBracketButton(const char *icon, const RowSpan &rows, bool bracketed, const char *tooltip)
{
    if (rows.count < 2)
        return false;

    const float gap   = ImGui::GetStyle().ItemInnerSpacing.x;
    const float reach = 0.5f * HelloImGui::EmSize();
    const float x0 = rows.right + gap, x1 = x0 + reach;

    // Placed and then put back: the rows have already been laid out, and whatever follows them should not
    // find the cursor somewhere off to the side.
    const ImVec2 restore = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x1 + gap, 0.5f * (rows.first + rows.last) - 0.5f * ImGui::GetFrameHeight()));

    const bool pressed = ImGui::Button(icon);
    if (tooltip && tooltip[0])
        Tooltip(tooltip);

    ImGui::SetCursorScreenPos(restore);

    if (bracketed)
    {
        auto       *dl  = ImGui::GetWindowDrawList();
        const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);

        dl->AddLine(ImVec2(x0, rows.first), ImVec2(x1, rows.first), col);
        dl->AddLine(ImVec2(x1, rows.first), ImVec2(x1, rows.last), col);
        dl->AddLine(ImVec2(x1, rows.last), ImVec2(x0, rows.last), col);
    }

    return pressed;
}

DialogResult DialogButtons(const char *confirm_label, const char *cancel_label, bool use_shortcuts,
                           bool confirm_enabled)
{
    DialogResult result = DialogResult::None;

    // Enter confirms and Escape cancels whatever else is going on, so a dialog reached from the command
    // palette can be finished without the mouse. Neither is conditioned on keyboard navigation being idle,
    // which arriving from the palette leaves active. Two things do yield: an item being edited keeps Enter,
    // so it commits the field and the next press applies the dialog, and so does a button reached by
    // keyboard navigation, so activating Cancel that way does not also confirm.
    const bool editing = ImGui::IsAnyItemActive();

    // Omitting the size lets Dear ImGui auto-fit each button to its own label (text size + FramePadding*2),
    // so a long label like "Reset options to defaults" is never clipped, and the pair is right-aligned by
    // measuring the two rather than by a fixed offset.
    const ImGuiStyle &style = ImGui::GetStyle();
    const float       width = ImGui::CalcTextSize(cancel_label).x + ImGui::CalcTextSize(confirm_label).x +
                        4.f * style.FramePadding.x + style.ItemSpacing.x;

    // Trailing edge of the content, which under an auto-resizing dialog is as wide as its widest row --
    // so the buttons end where the controls above them do.
    if (const float indent = ImGui::GetContentRegionAvail().x - width; indent > 0.f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

    const bool cancel_pressed = ImGui::Button(cancel_label);
    if (cancel_pressed ||
        (use_shortcuts && (ImGui::Shortcut(ImGuiKey_Escape) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Period))))
        result = DialogResult::Cancel;

    ImGui::SameLine();

    ImGui::BeginDisabled(!confirm_enabled);
    const bool confirm_pressed = ImGui::Button(confirm_label);
    if (confirm_pressed || (confirm_enabled && use_shortcuts && !editing && !cancel_pressed &&
                            (ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_KeypadEnter) ||
                             ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter))))
        result = DialogResult::Confirm;
    ImGui::EndDisabled();

    return result;
}

DialogResult ConfirmDialog(const char *title, bool &open, const char *message, const char *confirm_label,
                           DialogPosition position)
{
    DialogResult result = DialogResult::None;
    if (BeginModalDialog(title, open, position))
    {
        ImGui::TextUnformatted(message);
        ImGui::Spacing();

        result = DialogButtons(confirm_label);
        if (result != DialogResult::None)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
    return result;
}

void Tooltip(const char *description, bool questionMark /*= false*/, float wrap /*=-1.f*/)
{
    if (questionMark)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_MY_ABOUT);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | (questionMark ? 0 : ImGuiHoveredFlags_DelayNormal)))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(wrap < 0.f ? HelloImGui::EmSize(35.f) : wrap);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Beginning the Property Editor
bool PE::Begin(const char *label, ImGuiTableFlags flag)
{
    // ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    bool result = ImGui::BeginTable(label, 2, flag);
    // if (!result)
    //     ImGui::PopStyleVar();
    return result;
}

// Ending the Editor
void PE::End()
{
    ImGui::EndTable();
    // ImGui::PopStyleVar();
}

// adapted from imgui_internal: currently only needed to remove the tooltip at the end
// align_x: 0.0f = left, 0.5f = center, 1.0f = right.
// size_x : 0.0f = shortcut for GetContentRegionAvail().x
// FIXME-WIP: Works but API is likely to be reworked. This is designed for 1 item on the line. (#7024)
void TextAlignedV2(float align_x, float size_x, const char *fmt, va_list args)
{
    ImGuiWindow *window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    const char *text, *text_end;
    ImFormatStringToTempBufferV(&text, &text_end, fmt, args);
    const ImVec2 text_size = CalcTextSize(text, text_end);
    size_x                 = CalcItemSize(ImVec2(size_x, 0.0f), 0.0f, text_size.y).x;

    ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    ImVec2 pos_max(pos.x + size_x, window->ClipRect.Max.y);
    ImVec2 size(ImMin(size_x, text_size.x), text_size.y);
    window->DC.CursorMaxPos.x = ImMax(window->DC.CursorMaxPos.x, pos.x + text_size.x);
    window->DC.IdealMaxPos.x  = ImMax(window->DC.IdealMaxPos.x, pos.x + text_size.x);
    if (align_x > 0.0f && text_size.x < size_x)
        pos.x += ImTrunc((size_x - text_size.x) * align_x);
    // RenderTextClipped(pos, pos_max, text, text_end, &text_size);
    RenderTextEllipsis(window->DrawList, pos, pos_max, pos_max.x, text, text_end, &text_size);

    const ImVec2 backup_max_pos = window->DC.CursorMaxPos;
    ItemSize(size);
    ItemAdd(ImRect(pos, pos + size), 0);
    window->DC.CursorMaxPos.x =
        backup_max_pos.x; // Cancel out extending content size because right-aligned text would otherwise mess it up.
}

void TextAligned2(float align_x, float size_x, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    TextAlignedV2(align_x, size_x, fmt, args);
    va_end(args);
}

float PE::ColumnWidth(int column_n)
{
    const ImGuiTable *table = ImGui::GetCurrentTable();
    return table && column_n < table->ColumnsCount ? table->Columns[column_n].WidthGiven : 0.f;
}

bool PE::FullWidthEntry(const char *id, const std::function<bool(float)> &content_fct)
{
    ImGui::PushID(id);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // Advance into column 1 before drawing. The content below spans both columns via the cursor/clip rect
    // override, but ImGui's per-column auto-fit width tracking attributes whatever the cursor reaches to
    // whichever column is current, and column 0 is the one double-clicking the resize border auto-fits.
    ImGui::TableNextColumn();

    // WidthGiven excludes each column's cell padding and inter-column spacing, so summing per-column widths
    // falls short of the row's real span; read the bounds off the table instead. Also reset the cursor to
    // column 0's true left edge, undoing any ambient Indent() a caller applied (column 0 has
    // ImGuiTableColumnFlags_IndentEnable on by default).
    const ImGuiTable *table   = ImGui::GetCurrentTable();
    const float       left_x  = table ? table->Columns[0].WorkMinX : ImGui::GetCursorScreenPos().x;
    const float       right_x = table && table->ColumnsCount > 1 ? table->Columns[1].WorkMaxX : left_x;
    ImGui::SetCursorScreenPos(ImVec2(left_x, ImGui::GetCursorScreenPos().y));

    // Tables clip each cell to its own column, so widen the clip rect to cover both before drawing across them.
    const float  width = right_x - left_x;
    const ImVec2 p0    = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(p0, ImVec2(p0.x + width, ImGui::GetCurrentWindow()->ClipRect.Max.y), false);
    bool result = content_fct(width);
    ImGui::PopClipRect();

    ImGui::PopID();
    return result;
}

static ImVector<ImFont *> g_pe_label_fonts;

void PE::PushLabelFont(ImFont *font) { g_pe_label_fonts.push_back(font); }
void PE::PopLabelFont()
{
    IM_ASSERT(!g_pe_label_fonts.empty() && "PE::PopLabelFont() without a matching PE::PushLabelFont()");
    g_pe_label_fonts.pop_back();
}

// Pushes the current label font, if any; returns whether a matching ImGui::PopFont() is needed.
static bool PushPELabelFont()
{
    ImFont *font = g_pe_label_fonts.empty() ? nullptr : g_pe_label_fonts.back();
    if (font)
        ImGui::PushFont(font, 0.f);
    return font != nullptr;
}

// Generic entry, the lambda function should return true if the widget changed
bool PE::Entry(const std::string &property_name, const std::function<bool()> &content_fct, const std::string &tooltip)
{
    ImGui::PushID(property_name.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    bool pop_font = PushPELabelFont();
    ImGui::TextAligned2(0.f, -FLT_MIN, "%s", property_name.c_str());
    // ImGui::TextUnformatted(property_name.c_str());
    if (pop_font)
        ImGui::PopFont();
    if (!tooltip.empty())
        Tooltip(tooltip.c_str());
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool result = content_fct();
    ImGui::PopID();
    return result; // returning if the widget changed
}

bool PE::TreeNode(const char *name, ImGuiTreeNodeFlags flags)
{
    ImGui::PushID(name);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    auto ret = ImGui::TreeNodeEx("##tree node arrow",
                                 ImGuiTreeNodeFlags_SpanFullWidth | flags); // | ImGuiTreeNodeFlags_SpanAllColumns);
    ImGui::SameLine(0.f, 0.f);
    bool pop_font = PushPELabelFont();
    ImGui::TextAligned2(0.f, -FLT_MIN, "%s", name);
    if (pop_font)
        ImGui::PopFont();

    if (!ret)
        ImGui::PopID();
    return ret;
}
void PE::TreePop()
{
    ImGui::TreePop();
    ImGui::PopID();
}

void PE::Hyperlink(const char *name, const char *desc, const char *url /*= nullptr*/)
{
    ImGui::PushID(name);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();

    ImGui::AlignCursor(name, 1.f);
    ImGui::PushFont(hdrview()->font("sans bold"), ImGui::GetStyle().FontSizeBase);
    ImGui::HyperlinkText(name, url);
    ImGui::PopFont();
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::TextUnformatted(desc);
    ImGui::PopID();
}

void PE::WrappedText(const string &property_name, const string &value, const string &tooltip, ImFont *font,
                     float wrap_em)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.f));
    PE::Entry(
        property_name,
        [&]
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 1),
                                                ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 10));
            if (ImGui::BeginChild("ResizableChild", ImVec2(-FLT_MIN, 0.f), ImGuiChildFlags_AutoResizeY))
            {
                ImGui::PushTextWrapPos(wrap_em); // wrap_em==0.f: wrap to end of window/column
                ImGui::AlignTextToFramePadding();

                ImGui::PushFont(font, ImGui::GetStyle().FontSizeBase);
                ImGui::TextUnformatted(value);
                ImGui::PopFont();

                ImGui::PopTextWrapPos();

                Tooltip("Click to copy to clipboard.");
                if (ImGui::IsItemClicked())
                    ImGui::SetClipboardText(value.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            ImGui::PopStyleColor();
            ImGui::EndChild();

            return false; // no change
        },
        tooltip);
    ImGui::PopStyleVar();
}

} // namespace ImGui