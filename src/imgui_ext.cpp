#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_ext.h"
#include "IconsFontAwesome6.h"
#include "box.h"
#include "colorspace.h"
#include "immapp/browse_to_url.h"
#include "spdlog/pattern_formatter.h"

#include "imgui_internal.h"
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

static const std::string s_level_icons[] = {ICON_FA_VOLUME_HIGH,          ICON_FA_BUG_SLASH,    ICON_FA_CIRCLE_INFO,
                                            ICON_FA_TRIANGLE_EXCLAMATION, ICON_FA_CIRCLE_XMARK, ICON_FA_BOMB,
                                            ICON_FA_VOLUME_XMARK};

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
            ICON_FA_FILTER
            "Filter (format: [include|-exclude][,...]; e.g. \"include_this,-but_not_this,also_include_this\")",
            m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf)))
        m_filter.Build();
    if (filter_active)
    {
        ImGui::SameLine(0.f, 0.f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
        if (ImGui::IconButton(ICON_FA_DELETE_LEFT))
            m_filter.Clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(button_size.x);
    ImGui::PushStyleColor(ImGuiCol_Text, m_level_colors.at(current_level));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(0.5f * ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y));
    if (ImGui::BeginCombo("##Log level", s_level_icons[int(current_level)].data(), ImGuiComboFlags_NoArrowButton))
    {
        for (int i = 0; i < spdlog::level::n_levels; ++i)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, i < int(current_level) ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                                                        : m_level_colors.at(i));
            if (ImGui::Selectable((ICON_FA_GREATER_THAN_EQUAL + std::to_string(i) + ": " + s_level_icons[i] + " " +
                                   level_names[i].data())
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
    if (ImGui::IconButton(ICON_FA_TRASH_CAN))
        m_sink->clear_messages();
    ImGui::WrappedTooltip("Clear all messages.");
    ImGui::SameLine();
    if (ImGui::IconButton(m_auto_scroll ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
        m_auto_scroll = !m_auto_scroll;
    ImGui::WrappedTooltip(m_auto_scroll ? "Turn auto scrolling off." : "Turn auto scrolling on.");
    ImGui::SameLine();
    if (ImGui::IconButton(m_wrap_text ? ICON_FA_BARS_STAGGERED : ICON_FA_ALIGN_LEFT))
        m_wrap_text = !m_wrap_text;
    ImGui::WrappedTooltip(m_wrap_text ? "Turn line wrapping off." : "Turn line wrapping on.");

    auto window_flags = m_wrap_text
                            ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                            : ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar;

    ImGui::BeginChild("##spdlog window", ImVec2(0.f, 0.f), ImGuiChildFlags_None, window_flags);
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

ImVec2 IconSize() { return CalcTextSize(ICON_FA_VOLUME_HIGH); }

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

// // taken from https://github.com/ocornut/imgui/issues/632#issuecomment-1847070050
// static void PlotMultiEx(ImGuiPlotType plot_type, const char *label, int num_datas, const char **names, const ImColor
// *colors,
//                  float (*getter)(const void *data, int idx, int tableIndex), const void *datas, int values_count,
//                  float scale_min, float scale_max, ImVec2 graph_size)
// {
//     auto InvertColorU32 = [](ImU32 in)
//     {
//         ImVec4 in4 = ImGui::ColorConvertU32ToFloat4(in);
//         in4.x      = 1.f - in4.x;
//         in4.y      = 1.f - in4.y;
//         in4.z      = 1.f - in4.z;
//         return ImGui::GetColorU32(in4);
//     };

//     const int values_offset = 0;

//     ImGuiWindow *window = ImGui::GetCurrentWindow();
//     if (window->SkipItems)
//         return;

//     ImGuiContext     &g     = *GImGui;
//     const ImGuiStyle &style = g.Style;

//     const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
//     if (graph_size.x == 0.0f)
//         graph_size.x = ImGui::CalcItemWidth();
//     if (graph_size.y == 0.0f)
//         graph_size.y = label_size.y + (style.FramePadding.y * 2);

//     const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(graph_size.x, graph_size.y));
//     const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
//     const ImRect total_bb(
//         frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f,
//         0));
//     ImGui::ItemSize(total_bb, style.FramePadding.y);
//     if (!ImGui::ItemAdd(total_bb, 0, &frame_bb))
//         return;

//     // Determine scale from values if not specified
//     if (scale_min == FLT_MAX || scale_max == FLT_MAX)
//     {
//         float v_min = FLT_MAX;
//         float v_max = -FLT_MAX;
//         for (int data_idx = 0; data_idx < num_datas; ++data_idx)
//         {
//             for (int i = 0; i < values_count; i++)
//             {
//                 const float v = getter(datas, i, data_idx);
//                 if (v != v) // Ignore NaN values
//                     continue;
//                 v_min = ImMin(v_min, v);
//                 v_max = ImMax(v_max, v);
//             }
//         }
//         if (scale_min == FLT_MAX)
//             scale_min = v_min;
//         if (scale_max == FLT_MAX)
//             scale_max = v_max;
//     }

//     ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

//     int res_w      = ImMin((int)graph_size.x, values_count) + ((plot_type == ImGuiPlotType_Lines) ? -1 : 0);
//     int item_count = values_count + ((plot_type == ImGuiPlotType_Lines) ? -1 : 0);

//     // Tooltip on hover
//     int v_hovered = -1;
//     if (ImGui::IsItemHovered())
//     {
//         const float t = ImClamp((g.IO.MousePos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x), 0.0f,
//         0.9999f); const int   v_idx = (int)(t * item_count); IM_ASSERT(v_idx >= 0 && v_idx < values_count);

//         // std::string toolTip;
//         ImGui::BeginTooltip();
//         const int idx0 = (v_idx + values_offset) % values_count;
//         if (plot_type == ImGuiPlotType_Lines)
//         {
//             const int idx1 = (v_idx + 1 + values_offset) % values_count;
//             ImGui::Text("%8d %8d | Name", v_idx, v_idx + 1);
//             for (int dataIdx = 0; dataIdx < num_datas; ++dataIdx)
//             {
//                 const float v0 = getter(datas, idx0, dataIdx);
//                 const float v1 = getter(datas, idx1, dataIdx);
//                 ImGui::TextColored(colors[dataIdx], "%08.4g %08.4g | %s", v0, v1, names[dataIdx]);
//             }
//         }
//         else if (plot_type == ImGuiPlotType_Histogram)
//         {
//             for (int dataIdx = 0; dataIdx < num_datas; ++dataIdx)
//             {
//                 const float v0 = getter(datas, idx0, dataIdx);
//                 ImGui::TextColored(colors[dataIdx], "%d: %08.4g | %s", v_idx, v0, names[dataIdx]);
//             }
//         }
//         ImGui::EndTooltip();
//         v_hovered = v_idx;
//     }

//     for (int data_idx = 0; data_idx < num_datas; ++data_idx)
//     {
//         const float t_step = 1.0f / (float)res_w;

//         float  v0  = getter(datas, (0 + values_offset) % values_count, data_idx);
//         float  t0  = 0.0f;
//         ImVec2 tp0 = ImVec2(
//             t0, 1.0f - ImSaturate((v0 - scale_min) /
//                                   (scale_max - scale_min))); // Point in the normalized space of our target rectangle

//         const ImU32 col_base    = colors[data_idx];
//         const ImU32 col_hovered = InvertColorU32(colors[data_idx]);

//         for (int n = 0; n < res_w; n++)
//         {
//             const float t1     = t0 + t_step;
//             const int   v1_idx = (int)(t0 * item_count + 0.5f);
//             IM_ASSERT(v1_idx >= 0 && v1_idx < values_count);
//             const float  v1  = getter(datas, (v1_idx + values_offset + 1) % values_count, data_idx);
//             const ImVec2 tp1 = ImVec2(t1, 1.0f - ImSaturate((v1 - scale_min) / (scale_max - scale_min)));

//             // NB: Draw calls are merged together by the DrawList system. Still, we should render our batch are lower
//             // level to save a bit of CPU.
//             ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, tp0);
//             ImVec2 pos1 =
//                 ImLerp(inner_bb.Min, inner_bb.Max, (plot_type == ImGuiPlotType_Lines) ? tp1 : ImVec2(tp1.x, 1.0f));
//             if (plot_type == ImGuiPlotType_Lines)
//             {
//                 window->DrawList->AddLine(pos0, pos1, v_hovered == v1_idx ? col_hovered : col_base);
//             }
//             else if (plot_type == ImGuiPlotType_Histogram)
//             {
//                 if (pos1.x >= pos0.x + 2.0f)
//                     pos1.x -= 1.0f;
//                 window->DrawList->AddRectFilled(pos0, pos1, v_hovered == v1_idx ? col_hovered : col_base);
//             }

//             t0  = t1;
//             tp0 = tp1;
//         }
//     }

//     if (label_size.x > 0.0f)
//         ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);
// }

// void PlotMultiLines(const char *label, int num_datas, const char **names, const ImColor *colors,
//                     float (*getter)(const void *data, int idx, int tableIndex), const void *datas, int values_count,
//                     float scale_min, float scale_max, ImVec2 graph_size)
// {
//     PlotMultiEx(ImGuiPlotType_Lines, label, num_datas, names, colors, getter, datas, values_count, scale_min,
//     scale_max,
//                 graph_size);
// }

// void PlotMultiHistograms(const char *label, int num_hists, const char **names, const ImColor *colors,
//                          float (*getter)(const void *data, int idx, int tableIndex), const void *datas,
//                          int values_count, float scale_min, float scale_max, ImVec2 graph_size)
// {
//     PlotMultiEx(ImGuiPlotType_Histogram, label, num_hists, names, colors, getter, datas, values_count, scale_min,
//                 scale_max, graph_size);
// }

void PushRowColors(bool is_current, bool is_reference)
{
    float4 active  = GetStyleColorVec4(ImGuiCol_HeaderActive);
    float4 header  = GetStyleColorVec4(ImGuiCol_Header);
    float4 hovered = GetStyleColorVec4(ImGuiCol_HeaderHovered);

    // choose the complement color if we are the reference
    float4 hovered_c = convert_colorspace(
        convert_colorspace(hovered, HSV_CS, LinearSRGB_CS) + float4{0.67f, 0.f, -0.4f, 0.f}, LinearSRGB_CS, HSV_CS);
    float4 active_c = convert_colorspace(
        convert_colorspace(active, HSV_CS, LinearSRGB_CS) + float4{0.67f, 0.f, -0.4f, 0.f}, LinearSRGB_CS, HSV_CS);

    if (is_reference && is_current)
    {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0.5f * (hovered_c + hovered));
        ImGui::PushStyleColor(ImGuiCol_Header, 0.5f * (hovered_c + hovered));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0.5f * (active_c + active));
    }
    else if (is_current)
    {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hovered);
        ImGui::PushStyleColor(ImGuiCol_Header, hovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, active);
    }
    else if (is_reference)
    {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hovered_c);
        ImGui::PushStyleColor(ImGuiCol_Header, hovered_c);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, active_c);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header);
        ImGui::PushStyleColor(ImGuiCol_Header, hovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, active);
    }
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
            SetTooltip("%s %s", ICON_FA_LINK, href);
            PopFont();
            UnderLine(LinkColor());
        }
        if (IsItemClicked())
            ImmApp::BrowseToUrl(href);
    }
}

} // namespace ImGui