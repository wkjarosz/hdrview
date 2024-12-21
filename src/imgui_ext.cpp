#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_ext.h"
#include "box.h"
#include "colorspace.h"
#include "hello_imgui/icons_font_awesome_6.h"
#include "spdlog/pattern_formatter.h"

#include "fonts.h"

#include "imgui_internal.h"

#include <array>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(_WIN32)
#include <windows.h>

#include <shellapi.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <cstdio>
#include <cstdlib>

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
    m_sink(make_shared<spdlog::sinks::ringbuffer_color_sink_mt>(max_items)), m_default_color(white),
    m_level_colors({white, cyan, green, yellow, red, magenta, gray})
{
}

void SpdLogWindow::set_pattern(const string &pattern)
{
    // add support for custom level icon flag to formatter
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<level_icon_formatter_flag>('*').set_pattern(pattern);
    m_sink->set_formatter(std::move(formatter));
}

void SpdLogWindow::draw(ImFont *console_font)
{
    static const spdlog::string_view_t level_names[] = SPDLOG_LEVEL_NAMES;

    auto         current_level = m_sink->level();
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
    ImGui::SetNextItemWidth(button_size.x);
    ImGui::PushStyleColor(ImGuiCol_Text, m_level_colors.at(current_level));
    // Calculate the padding needed to center the icon in a ComboBox
    // Solve for NewPadding.x:
    // NewPadding.x + IconWidth + NewPadding.x = button_size.x
    // NewPadding.x + FontSize + NewPadding.x = FontSize + style.FramePadding.y * 2
    // 2 * NewPadding.x = style.FramePadding.y * 2
    // NewPadding.x = style.FramePadding.y
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetStyle().FramePadding.y, ImGui::GetStyle().FramePadding.y));
    if (ImGui::BeginCombo("##Log level", s_level_icons[int(current_level)].data(),
                          ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLargest))
    {
        for (int i = 0; i < spdlog::level::n_levels; ++i)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, i < int(current_level) ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                                                        : m_level_colors.at(i));
            if (ImGui::Selectable(
                    (ICON_MY_GREATER_EQUAL + std::to_string(i) + ": " + s_level_icons[i] + " " + level_names[i].data())
                        .c_str(),
                    current_level == i))
                m_sink->set_level(spdlog::level::level_enum(i));
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::WrappedTooltip("Click to choose the verbosity level.");
    ImGui::SameLine();
    if (ImGui::IconButton(ICON_MY_TRASH_CAN))
        m_sink->clear_messages();
    ImGui::WrappedTooltip("Clear all messages.");
    ImGui::SameLine();
    if (ImGui::IconButton(m_auto_scroll ? ICON_MY_LOCK : ICON_MY_LOCK_OPEN))
        m_auto_scroll = !m_auto_scroll;
    ImGui::WrappedTooltip(m_auto_scroll ? "Turn auto scrolling off." : "Turn auto scrolling on.");
    ImGui::SameLine();
    if (ImGui::IconButton(m_wrap_text ? ICON_MY_TEXT_WRAP_ON : ICON_MY_TEXT_WRAP_OFF))
        m_wrap_text = !m_wrap_text;
    ImGui::WrappedTooltip(m_wrap_text ? "Turn line wrapping off." : "Turn line wrapping on.");

    auto window_flags = m_wrap_text
                            ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                            : ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar;

    ImGui::BeginChild("##spdlog window", ImVec2(0.f, 0.f), ImGuiChildFlags_FrameStyle, window_flags);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
    ImGui::PushFont(console_font);
    ImGui::PushStyleColor(ImGuiCol_Text, m_default_color);

    int  item_num = 0;
    bool did_copy = false;
    m_sink->iterate(
        [this, &item_num, &did_copy](const typename spdlog::sinks::ringbuffer_color_sink_mt::LogItem &msg) -> bool
        {
            ++item_num;
            if (!m_sink->should_log(msg.level) ||
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
                    ImGui::CalcTextSize(msg.message.c_str() + (invalid_color_range ? 0 : msg.color_range_end), nullptr,
                                        false, m_wrap_text ? (ImGui::GetContentRegionAvail().x - prefix_width) : -1.f)
                        .y;
                selectable_size.x = ImGui::GetContentRegionAvail().x + (m_wrap_text ? 0 : ImGui::GetScrollMaxX());
            }

            ImGui::PushID(item_num);
            if (ImGui::Selectable("##log item selectable", false, ImGuiSelectableFlags_AllowOverlap, selectable_size))
            {
                did_copy = true;
                ImGui::SetClipboardText(msg.message.c_str() + (invalid_color_range ? 0 : msg.color_range_end));
            }
            ImGui::PopID();
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

    ImGui::PopStyleColor();

    if (m_sink->has_new_items() && m_auto_scroll)
        ImGui::SetScrollHereY(1.f);

    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void SpdLogWindow::clear() { m_sink->clear_messages(); }

ImU32 SpdLogWindow::get_default_color() { return m_default_color; }
void  SpdLogWindow::set_default_color(ImU32 color) { m_default_color = color; }

void SpdLogWindow::set_level_color(spdlog::level::level_enum level, ImU32 color)
{
    m_level_colors.at(static_cast<size_t>(level)) = color;
}
ImU32 SpdLogWindow::get_level_color(spdlog::level::level_enum level)
{
    return m_level_colors.at(static_cast<size_t>(level));
}

ImVec2 IconSize() { return CalcTextSize(ICON_MY_WIDEST); }

ImVec2 IconButtonSize()
{
    return {ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};
    // return {IconSize().x + 2 * ImGui::GetStyle().ItemInnerSpacing.x, 0.f};
}

bool IconButton(const char *icon)
{
    // Remove frame padding and spacing
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
    // ImGui::PushID(icon);
    bool ret = ImGui::Button(icon, IconButtonSize());
    ImGui::PopStyleVar(2);
    // ImGui::PopID();
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

void PushRowColors(bool is_current, bool is_reference)
{
    float4 active   = GetStyleColorVec4(ImGuiCol_HeaderActive);
    float4 header   = GetStyleColorVec4(ImGuiCol_Header);
    float4 hovered  = GetStyleColorVec4(ImGuiCol_HeaderHovered);
    bool   mod_held = ImGui::GetIO().KeyCtrl;

    // "complementary" color (for reference image/channel group) is shifted by 2/3 in hue
    constexpr float3 hsv_adjust = float3{0.67f, 0.f, -0.2f};
    float4           hovered_c{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(hovered.xyz()) + hsv_adjust), hovered.w};
    float4           header_c{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(header.xyz()) + hsv_adjust), header.w};
    float4           active_c{ColorConvertHSVtoRGB(ColorConvertRGBtoHSV(active.xyz()) + hsv_adjust), active.w};

    // the average between the two is used when a row is both current and reference
    float4 hovered_avg = 0.5f * (hovered_c + hovered);
    float4 header_avg  = 0.5f * (header_c + header);
    float4 active_avg  = 0.5f * (active_c + active);

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, mod_held ? (is_current ? hovered_avg : hovered_c) : hovered);
    ImGui::PushStyleColor(ImGuiCol_Header, is_reference ? (is_current ? header_avg : header_c) : header);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, mod_held ? (is_current ? active_avg : active_c) : active);
}

static float triangle_wave(float t, float period = 1.f)
{
    float a = period / 2.f;
    return fabs(2 * (t / a - floor(t / a + 0.5f)));
}

void BusyBar(float fraction, const ImVec2 &size_arg, const char *overlay)
{
    ImGuiContext &g      = *GImGui;
    ImGuiWindow  *window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    const auto &style        = g.Style;
    const float w            = size_arg.x == 0.f ? CalcItemWidth() : size_arg.x;
    const float grab_padding = 2.0f;

    // ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), g.FontSize + style.FramePadding.y * 2.0f);
    ImVec2 size = ImVec2(w, g.FontSize + style.FramePadding.y * 2.0f);
    ImVec2 pos  = window->DC.CursorPos;
    ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, 0))
        return;

    bool indeterminate = fraction != fraction || fraction < 0.f;

    RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
    bb.Expand(ImVec2(-grab_padding, -grab_padding));

    float left, right;
    if (indeterminate)
    {
        const float time  = (float)g.Time;
        const float speed = 0.25f;
        const float anim1 = smoothstep(0.0f, 1.0f, smoothstep(0.0f, 1.0f, triangle_wave(time * speed)));
        const float anim2 = smoothstep(0.0f, 1.0f, triangle_wave(time * speed * 2.f));

        float bar_size = lerp(0.05f, 0.25f, anim2);
        float t0       = lerp(0.f, 1.f - bar_size, anim1);
        float t1       = t0 + bar_size;

        left  = lerp(bb.Min.x, bb.Max.x - 2.f * style.GrabRounding, t0);
        right = lerp(bb.Min.x + 2.f * style.GrabRounding, bb.Max.x, t1);
        // RenderRectFilledRangeH(window->DrawList, bb, GetColorU32(ImGuiCol_SliderGrab), t0, t1, style.GrabRounding);

        fraction = 0.0f; // make the overlay text show up
    }
    else
    {
        fraction = ImSaturate(fraction);
        left     = bb.Min.x;
        right    = lerp(bb.Min.x + 2.f * style.GrabRounding, bb.Max.x, fraction);
        // RenderRectFilledRangeH(window->DrawList, bb, GetColorU32(ImGuiCol_SliderGrab), 0.0f, fraction,
        //                        style.GrabRounding);
    }
    window->DrawList->AddRectFilled(ImVec2(left, bb.Min.y), ImVec2(right, bb.Max.y), GetColorU32(ImGuiCol_SliderGrab),
                                    style.GrabRounding);

    // Default displaying the fraction as percentage string, but user can override it
    char overlay_buf[32];
    if (!overlay && !indeterminate)
    {
        ImFormatString(overlay_buf, IM_ARRAYSIZE(overlay_buf), "%.0f%%", fraction * 100 + 0.01f);
        overlay = overlay_buf;
    }

    const ImVec2 fill_br = ImVec2(ImLerp(bb.Min.x, bb.Max.x, fraction), bb.Max.y);

    ImVec2 overlay_size = CalcTextSize(overlay, NULL);
    if (overlay_size.x > 0.0f)
        RenderTextClipped(ImVec2(ImClamp(fill_br.x + style.ItemSpacing.x, bb.Min.x,
                                         bb.Max.x - overlay_size.x - style.ItemInnerSpacing.x),
                                 bb.Min.y),
                          bb.Max, overlay, NULL, &overlay_size, ImVec2(0.5f, 0.5f), &bb);
}

ImVec4 LinkColor()
{
    auto col_text = ImGui::GetStyle().Colors[ImGuiCol_Text];

    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(col_text.x, col_text.y, col_text.z, h, s, v);
    h = 0.57f;
    if (v >= 0.8f)
        v = 0.8f;
    if (v <= 0.5f)
        v = 0.5f;
    if (s <= 0.5f)
        s = 0.5f;

    ImGui::ColorConvertHSVtoRGB(h, s, v, col_text.x, col_text.y, col_text.z);
    return col_text;
}

// from https://github.com/ocornut/imgui/issues/5280#issuecomment-1117155573
void TextWithHoverColor(ImVec4 col, const char *fmt, ...)
{
    ImGuiContext &g      = *GImGui;
    ImGuiWindow  *window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    // Format text
    va_list args;
    va_start(args, fmt);
    auto text_begin = g.TempBuffer.Data;
    auto text_end   = g.TempBuffer.Data + ImFormatStringV(g.TempBuffer.Data, g.TempBuffer.Size, fmt, args);
    va_end(args);

    // Layout
    const ImVec2 text_pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImVec2 text_size = CalcTextSize(text_begin, text_end);
    ImRect       bb(text_pos.x, text_pos.y, text_pos.x + text_size.x, text_pos.y + text_size.y);
    ItemSize(text_size, 0.0f);
    if (!ItemAdd(bb, 0))
        return;

    // Render
    bool hovered = IsItemHovered();
    if (hovered)
        PushStyleColor(ImGuiCol_Text, col);
    RenderText(bb.Min, text_begin, text_end, false);
    if (hovered)
        PopStyleColor();
}

void UnderLine(ImColor c, float raise)
{
    ImVec2 mi = ImGui::GetItemRectMin();
    ImVec2 ma = ImGui::GetItemRectMax();

    mi.y = ma.y = ma.y - raise * ImGui::GetFontSize();

    float lineThickness = ImGui::GetFontSize() / 14.5f;
    ImGui::GetWindowDrawList()->AddLine(mi, ma, c, lineThickness);
}

// copied from ImGUI Bundle:
// https://github.com/pthom/imgui_bundle/blob/a57b127c198fb7c6ab0ba0157a9a968a5ed96ffb/external/immapp/immapp/browse_to_url.cpp#L19
//
// The MIT License (MIT)
// Copyright (c) 2021-2024 Pascal Thomet
//
// A platform specific utility to open an url in a browser
// (especially useful with emscripten version)
// Specific per platform includes for BrowseToUrl
void BrowseToUrl(const char *url)
{
#if defined(__EMSCRIPTEN__)
    char js_command[1024];
    snprintf(js_command, 1024, "window.open(\"%s\");", url);
    emscripten_run_script(js_command);
#elif defined(_WIN32)
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif TARGET_OS_IPHONE
    // Nothing on iOS
#elif TARGET_OS_OSX
    char cmd[1024];
    snprintf(cmd, 1024, "open %s", url);
    system(cmd);
#elif defined(__linux__)
    char cmd[1024];
    snprintf(cmd, 1024, "xdg-open %s", url);
    int r = system(cmd);
    (void)r;
#endif
}

// adapted from https://github.com/ocornut/imgui/issues/5280#issuecomment-1117155573
void HyperlinkText(const char *href, const char *fmt, ...)
{
    ImGuiContext &g      = *GImGui;
    ImGuiWindow  *window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    // Format text
    va_list args;
    va_start(args, fmt);
    auto text_begin = g.TempBuffer.Data;
    auto text_end   = g.TempBuffer.Data + ImFormatStringV(g.TempBuffer.Data, g.TempBuffer.Size, fmt, args);
    va_end(args);

    // Layout
    const ImVec2 text_pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImVec2 text_size = CalcTextSize(text_begin, text_end);
    ImRect       bb(text_pos.x, text_pos.y, text_pos.x + text_size.x, text_pos.y + text_size.y);
    ItemSize(text_size, 0.0f);
    if (!ItemAdd(bb, 0))
        return;

    // Render
    if (href)
        PushStyleColor(ImGuiCol_Text, LinkColor());
    RenderText(bb.Min, text_begin, text_end, false);
    if (href)
        PopStyleColor();

    if (href)
    {
        if (IsItemHovered())
        {
            PushFont(nullptr);
            SetMouseCursor(ImGuiMouseCursor_Hand);
            SetTooltip("%s %s", ICON_MY_LINK, href);
            PopFont();
            UnderLine(LinkColor());
        }
        if (IsItemClicked())
            BrowseToUrl(href);
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

} // namespace ImGui