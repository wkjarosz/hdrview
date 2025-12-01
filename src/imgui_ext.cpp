#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_ext.h"
#include "box.h"
#include "common.h"
#include "fonts.h"
#include "hello_imgui/dpi_aware.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "spdlog/pattern_formatter.h"

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

        int  item_num = 0;
        bool did_copy = false;
        m_ringbuffer_sink->iterate(
            [this, &item_num, &did_copy,
             default_font](const typename spdlog::sinks::ringbuffer_color_sink_mt::LogItem &msg) -> bool
            {
                ++item_num;
                if (!m_ringbuffer_sink->should_log(msg.level) ||
                    !m_filter.PassFilter(msg.message.c_str(), msg.message.c_str() + msg.message.size()))
                    return true;

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

                return true;
            });

        if (did_copy)
            spdlog::trace("Copied a log item to clipboard"); // the log sink is locked during the iterate loop above, so
                                                             // this needs to happen outside

        // ImGui::PopStyleColor();

        if (m_ringbuffer_sink->has_new_items() && m_auto_scroll)
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
    string ellipsis = "";
    string text     = filename;

    const float avail_width = GetContentRegionAvail().x;
    while (CalcTextSize((icon + ellipsis + text).c_str()).x > avail_width && text.length() > 1)
    {
        text     = text.substr(1);
        ellipsis = " ...";
    }

    return ellipsis + text;
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

void PushRowColors(bool is_current, bool is_reference, bool reference_mod)
{
    float4 active  = GetStyleColorVec4(ImGuiCol_HeaderActive);
    float4 header  = GetStyleColorVec4(ImGuiCol_Header);
    float4 hovered = GetStyleColorVec4(ImGuiCol_HeaderHovered);

    // "complementary" color (for reference image/channel group) is shifted by 2/3 in hue
    constexpr float3 hsv_adjust = float3{0.67f, 0.f, -0.2f};
    float4           hovered_c{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(hovered.xyz()) + hsv_adjust), hovered.w};
    float4           header_c{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(header.xyz()) + hsv_adjust), header.w};
    float4           active_c{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(active.xyz()) + hsv_adjust), active.w};

    // the average between the two is used when a row is both current and reference
    float4 hovered_avg = 0.5f * (hovered_c + hovered);
    float4 header_avg  = 0.5f * (header_c + header);
    float4 active_avg  = 0.5f * (active_c + active);
    // constexpr float3 hsv_adjust2 = float3{0.33f, 0.f, -0.2f};
    // float4           hovered_avg{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(hovered.xyz()) + hsv_adjust2), hovered.w};
    // float4           header_avg{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(header.xyz()) + hsv_adjust2), header.w};
    // float4           active_avg{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(active.xyz()) + hsv_adjust2), active.w};

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, reference_mod ? (is_current ? hovered_avg : hovered_c)
                                                                : (is_reference ? hovered_avg : hovered));
    ImGui::PushStyleColor(ImGuiCol_Header, is_reference ? (is_current ? header_avg : header_c) : header);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, reference_mod ? (is_current ? active_avg : active_c) : active);
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
    PushFont(GetIO().FontDefault, ImGui::GetStyle().FontSizeBase);
    SetItemTooltip("%s '%s'", ICON_MY_LINK, url);
    PopFont();
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

    draw_list->AddRect(rect.min, rect.max, col, 0.f, ImDrawFlags_None, thickness);

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

void MenuItem(const Action &a, bool include_name)
{
    if (a.needs_menu)
    {
        if (ImGui::BeginMenuEx(a.name.c_str(), a.icon.c_str(), a.enabled()))
        {
            a.callback();
            ImGui::EndMenu();
        }
    }
    else
    {
        if (ImGui::MenuItemEx(include_name ? a.name : a.icon, include_name ? a.icon : "",
                              ImGui::GetKeyChordNameTranslated(a.chord), a.p_selected, a.enabled()))
            a.callback();
        if (!include_name)
            ImGui::Tooltip(fmt::format("{}{}{}", a.name,
                                       a.chord ? fmt::format(" ({})", ImGui::GetKeyChordNameTranslated(a.chord)) : "",
                                       a.tooltip.empty() ? "" : fmt::format("\n\n{}", a.tooltip))
                               .c_str());
        else if (!a.tooltip.empty())
            ImGui::Tooltip(fmt::format("{}{}", a.tooltip.c_str(),
                                       a.chord ? fmt::format(" ({})", ImGui::GetKeyChordNameTranslated(a.chord)) : "")
                               .c_str());
    }
}

void IconButton(const Action &a, bool include_name)
{
    ImGui::BeginDisabled(a.enabled() == false);

    if (include_name)
    {
        if (ImGui::IconButton(fmt::format("{} {}", a.icon, a.name).c_str(), a.p_selected, ImVec2(0, -1)))
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
        if (ImGui::IconButton(fmt::format("{}##{}", a.icon, a.name).c_str(), a.p_selected))
            a.callback();
        if (a.chord)
            ImGui::Tooltip(fmt::format("{} ({}){}", a.name, ImGui::GetKeyChordNameTranslated(a.chord),
                                       a.tooltip.empty() ? "" : fmt::format("\n\n{}", a.tooltip))
                               .c_str());
        else
            ImGui::Tooltip(
                fmt::format("{}{}", a.name, a.tooltip.empty() ? "" : fmt::format("\n\n{}", a.tooltip)).c_str());
    }

    ImGui::EndDisabled();
}

void Checkbox(const Action &a)
{
    ImGui::Checkbox(a.name.c_str(), a.p_selected);
    if (!a.tooltip.empty() || a.chord)
    {
        string parenthesized_chord = a.chord ? fmt::format("({})", ImGui::GetKeyChordNameTranslated(a.chord)) : "";
        string tooltip             = fmt::format("{}{}", a.tooltip, parenthesized_chord);
        ImGui::Tooltip(tooltip.c_str());
    }
}

void Tooltip(const char *description, bool questionMark /*= false*/, float timerThreshold /*= 0.5f*/,
             float wrap /*=-1.f*/)
{
    ImGuiContext *GImGui = ImGui::GetCurrentContext();

    bool passTimer = GImGui->HoveredIdTimer >= timerThreshold && GImGui->ActiveIdTimer == 0.0f;
    if (questionMark)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_MY_ABOUT);
        passTimer = true;
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && passTimer)
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
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    bool result = ImGui::BeginTable(label, 2, flag);
    if (!result)
        ImGui::PopStyleVar();
    return result;
}

// Ending the Editor
void PE::End()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
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
    RenderTextClipped(pos, pos_max, text, text_end, &text_size);

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

// Generic entry, the lambda function should return true if the widget changed
bool PE::Entry(const std::string &property_name, const std::function<bool()> &content_fct, const std::string &tooltip)
{
    ImGui::PushID(property_name.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextAligned2(1.0f, -FLT_MIN, property_name.c_str());
    if (!tooltip.empty())
        Tooltip(tooltip.c_str(), false, 0);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool result = content_fct();
    if (!tooltip.empty())
        Tooltip(tooltip.c_str());
    ImGui::PopID();
    return result; // returning if the widget changed
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
    PE::Entry(
        property_name,
        [&]
        {
            ImGui::PushFont(font, ImGui::GetStyle().FontSizeBase);

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x +
                                   std::max(HelloImGui::EmSize(8.f), wrap_em <= 0.f ? ImGui::GetContentRegionAvail().x
                                                                                    : HelloImGui::EmSize(wrap_em)));

            ImGui::TextUnformatted(value);
            if (ImGui::IsItemClicked())
                ImGui::SetClipboardText(value.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            return false; // no change
        },
        tooltip);
}

} // namespace ImGui