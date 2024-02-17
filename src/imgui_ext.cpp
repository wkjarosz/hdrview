#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_ext.h"
#include "IconsFontAwesome6.h"
#include "box.h"
#include "colorspace.h"

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

void SpdLogWindow::draw(ImFont *console_font)
{
    static const std::string level_names[] = {"trace", "debug", "info", "warning", "error", "critical", "off"};
    static const std::string level_icons[] = {ICON_FA_VOLUME_HIGH,          ICON_FA_BUG,          ICON_FA_CIRCLE_INFO,
                                              ICON_FA_TRIANGLE_EXCLAMATION, ICON_FA_CIRCLE_XMARK, ICON_FA_BOMB,
                                              ICON_FA_VOLUME_XMARK};

    auto         current_level = m_sink->level();
    const ImVec2 button_size   = {ImGui::CalcTextSize(ICON_FA_VOLUME_HIGH).x + 2 * ImGui::GetStyle().ItemInnerSpacing.x,
                                  0.f};
    bool         filter_active = m_filter.IsActive(); // save here to avoid flicker

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4 * (button_size.x + ImGui::GetStyle().ItemSpacing.x) -
                            (filter_active ? button_size.x : 0.f));
    if (ImGui::InputTextWithHint(
            "##log filter",
            "Filter (in format: [include|-exclude][,...]; e.g. \"includeThis,-butNotThis,alsoIncludeThis\")",
            m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf)))
        m_filter.Build();
    if (filter_active)
    {
        ImGui::SameLine(0.f, 0.f);
        if (ImGui::Button(ICON_FA_DELETE_LEFT, button_size))
            m_filter.Clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(button_size.x);
    ImGui::PushStyleColor(ImGuiCol_Text, m_level_colors.at(current_level));
    if (ImGui::BeginCombo("##Log level", level_icons[int(current_level)].data(), ImGuiComboFlags_NoArrowButton))
    {
        for (int i = 0; i < spdlog::level::n_levels; ++i)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, i < int(current_level) ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                                                        : m_level_colors.at(i));
            if (ImGui::Selectable((ICON_FA_GREATER_THAN_EQUAL + std::to_string(i) + ": " + level_icons[i] + " " +
                                   level_names[i].data())
                                      .c_str(),
                                  current_level == i))
                m_sink->set_level(spdlog::level::level_enum(i));
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor();
    ImGui::WrappedTooltip("Click to choose the verbosity level.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH_CAN, button_size))
        m_sink->clear_messages();
    ImGui::WrappedTooltip("Clear all messages.");
    ImGui::SameLine();
    ImGui::ToggleButton(m_auto_scroll ? ICON_FA_LOCK_OPEN : ICON_FA_LOCK, &m_auto_scroll, button_size);
    ImGui::WrappedTooltip(m_auto_scroll ? "Turn auto scrolling off." : "Turn auto scrolling on.");
    ImGui::SameLine();
    ImGui::ToggleButton(m_wrap_text ? ICON_FA_TURN_DOWN : ICON_FA_ALIGN_LEFT, &m_wrap_text, button_size);
    ImGui::WrappedTooltip(m_wrap_text ? "Turn line wrapping off." : "Turn line wrapping on.");

    auto window_flags = m_wrap_text
                            ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                            : ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar;

    ImGui::BeginChild("##spdlog window", ImVec2(0.f, 0.f), ImGuiChildFlags_None, window_flags);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
    ImGui::PushFont(console_font);
    ImGui::PushStyleColor(ImGuiCol_Text, m_default_color);

    m_sink->iterate(
        [this](const typename spdlog::sinks::ringbuffer_color_sink_mt::LogItem &msg) -> bool
        {
            if (!m_sink->should_log(msg.level) ||
                !m_filter.PassFilter(msg.message.c_str(), msg.message.c_str() + msg.message.size()))
                return true;

            // if color range not specified or not not valid, just draw all the text with default color
            if (msg.color_range_end <= msg.color_range_start ||
                std::min(msg.color_range_start, msg.color_range_end) >= msg.message.length())
                ImGui::TextUnformatted(msg.message.c_str());
            else
            {
                // insert the text before the color range
                ImGui::TextUnformatted(msg.message.c_str(), msg.message.c_str() + msg.color_range_start);
                ImGui::SameLine(0.f, 0.f);

                // insert the colorized text
                ImGui::PushStyleColor(ImGuiCol_Text, m_level_colors.at(msg.level));
                ImGui::PushFont(nullptr);
                ImGui::TextUnformatted(" " + level_icons[msg.level]);
                ImGui::PopFont();
                ImGui::SameLine(0.f, 0.f);
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

    ImGui::PopStyleColor();

    if (m_sink->has_new_items() && m_auto_scroll)
        ImGui::SetScrollHereY(1.f);

    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void SpdLogWindow::clear() { m_sink->clear_messages(); }

void SpdLogWindow::scroll_to_bottom() {}

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

} // namespace ImGui