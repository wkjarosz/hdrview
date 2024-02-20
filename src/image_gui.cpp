//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "common.h"
#include "image.h"
#include "parallelfor.h"
#include "shader.h"
#include "texture.h"

#include "IconsFontAwesome6.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot/implot.h"
#include "implot/implot_internal.h"

#include <sstream>
#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

void Image::draw_histogram(float exposure)
{
    static ImPlotScale y_axis    = ImPlotScale_Linear;
    static AxisScale_  x_axis    = AxisScale_Asinh;
    static int         bin_type  = 1;
    static ImPlotCond  plot_cond = ImPlotCond_Always;
    {
        const ImVec2 button_size = {
            ImGui::CalcTextSize(ICON_FA_ARROWS_LEFT_RIGHT_TO_LINE).x + 2 * ImGui::GetStyle().ItemInnerSpacing.x, 0.f};
        float combo_width =
            std::max(HelloImGui::EmSize(5.f),
                     0.5f * (ImGui::GetContentRegionAvail().x - button_size.x - 2.f * ImGui::GetStyle().ItemSpacing.x) -
                         (ImGui::CalcTextSize("X:").x + ImGui::GetStyle().ItemInnerSpacing.x));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Y:");
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(combo_width);
        ImGui::Combo("##Y-axis type", &y_axis, "Linear\0Log\0\0");
        ImGui::SameLine();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("X:");
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(combo_width);
        ImGui::Combo("##X-axis type", &x_axis, "Linear\0sRGB\0Asinh\0\0");

        ImGui::SameLine();

        static bool lock = true;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(0, ImGui::GetStyle().FramePadding.y));  // Remove frame padding
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0)); // Remove frame padding
        // if (ImGui::Checkbox("Auto-fit axes to exposure", &lock))
        //     plot_cond = lock ? ImPlotCond_Always : ImPlotCond_Once;
        if (ImGui::ToggleButton(ICON_FA_ARROWS_LEFT_RIGHT_TO_LINE, &lock, button_size))
            plot_cond = lock ? ImPlotCond_Always : ImPlotCond_Once;
        ImGui::WrappedTooltip(
            "Toggle between manual panning/zoom in histogram vs. auto fitting histogram axes based on the exposure.");
        ImGui::PopStyleVar(2);
    }

    auto             hovered_pixel = int2{g_app()->pixel_at_app_pos(ImGui::GetIO().MousePos)};
    float4           color32       = g_app()->image_pixel(hovered_pixel);
    auto            &group         = groups[selected_group];
    PixelStatistics *stats[4]      = {nullptr, nullptr, nullptr, nullptr};
    string           names[4];
    auto             colors = group.colors();

    float2 x_limits = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    float2 y_limits = x_limits;
    for (int c = 0; c < std::min(3, group.num_channels); ++c)
    {
        auto &channel = channels[group.channels[c]];
        stats[c]      = channel.get_statistics(exposure, x_axis, y_axis);
        y_limits[0]   = std::min(y_limits[0], stats[c]->histogram.y_limits[0]);
        y_limits[1]   = std::max(y_limits[1], stats[c]->histogram.y_limits[1]);
        x_limits[0]   = std::min(x_limits[0], stats[c]->histogram.x_limits[0]);
        x_limits[1]   = std::max(x_limits[1], stats[c]->histogram.x_limits[1]);
        names[c]      = Channel::tail(channel.name);
    }

    ImPlot::GetStyle().PlotMinSize = {100, 100};

    if (ImPlot::BeginPlot("##Histogram", ImVec2(-1, -1)))
    {
        ImPlot::GetInputMap().ZoomRate = 0.03f;
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoTickLabels);
        ImPlot::SetupAxisScale(ImAxis_Y1, y_axis == AxisScale_Linear ? ImPlotScale_Linear : ImPlotScale_Log10);

        // ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Horizontal);

        auto format_power_of_10 = [](double value, char *buff, int size, void *)
        {
            // Calculate the exponent of the value
            int exponent = int(std::log10(std::fabs(value)));

            // Format the value into the buffer with the exponent using fmt::format_to
            auto result = fmt::format_to_n(buff, size, "10e{}", exponent);
            // Ensure null termination
            if (int(result.size) < size)
                buff[result.size++] = '\0';

            return int(result.size);
        };

        auto powers_of_10_ticks = [&x_limits, &format_power_of_10](int num_orders = 10)
        {
            float avail_width = std::max(ImPlot::GetStyle().PlotMinSize.x, ImGui::GetContentRegionAvail().x) -
                                2.f * ImPlot::GetStyle().PlotPadding.x;
            // auto p_limits = ImPlot::GetPlotLimits();
            // float2 x_limits = float2{p_limits.X.Min, p_limits.X.Max};

            auto calc_pixel = [x_limits, avail_width](float plt)
            {
                float scaleToPixels = avail_width / (x_limits[1] - x_limits[0]);
                float scaleMin      = axis_scale_fwd_xform(x_limits[0], &x_axis);
                float scaleMax      = axis_scale_fwd_xform(x_limits[1], &x_axis);
                float s             = axis_scale_fwd_xform(plt, &x_axis);
                float t             = (s - scaleMin) / (scaleMax - scaleMin);
                plt                 = lerp(x_limits[0], x_limits[1], t);

                return (float)(0 + scaleToPixels * (plt - x_limits[0]));
            };

            auto add_ticks = [num_orders, &format_power_of_10, &calc_pixel](vector<double> &ticks, float limit)
            {
                std::array<char, 64> buff;
                float                sgn     = limit < 0 ? -1.f : 1.f;
                int                  log_max = int(floor(log10(fabs(limit))));
                int                  log_min = log_max - num_orders;

                float prev_pixel = 0.f;
                float prev_width = 0.f;
                float origin     = calc_pixel(0.f);
                for (int i = log_max; i >= log_min; --i)
                {
                    float value = sgn * pow(10.f, float(i));
                    (void)format_power_of_10(value, buff.data(), buff.size(), nullptr);
                    float pixel          = calc_pixel(value);
                    float dist_from_prev = fabs(pixel - prev_pixel);
                    float dist_to_origin = fabs(pixel - origin);
                    float text_width     = ImGui::CalcTextSize(buff.data()).x;

                    if ((0.5f * (text_width + prev_width) < dist_from_prev && text_width < dist_to_origin) ||
                        i == log_max)
                    {
                        ticks.push_back(value);
                        prev_pixel = pixel;
                        prev_width = text_width;
                    }
                }
            };

            vector<double> ticks;
            ticks.reserve(1 + 2 * (num_orders + 1)); // conservative size estimate

            ticks.push_back(0.f);

            // add num_orders orders of magnitude on either size of the origin
            if (x_limits[1] > 0)
                add_ticks(ticks, x_limits[1]);

            if (x_limits[0] < 0)
                add_ticks(ticks, x_limits[0]);
            return ticks;
        };

        if (x_limits[0] == 0)
            x_limits[0] = 1e-14f;

        ImPlot::SetupAxesLimits(x_limits[0], x_limits[1], y_limits[0], y_limits[1], plot_cond);
        // ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::SetupMouseText(ImPlotLocation_SouthEast, ImPlotMouseTextFlags_NoFormat);
        switch (x_axis)
        {
        case AxisScale_Linear: ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear); break;
        case AxisScale_SRGB:
        {
            auto ticks = powers_of_10_ticks();
            ImPlot::SetupAxisScale(ImAxis_X1, axis_scale_fwd_xform, axis_scale_inv_xform, &x_axis);
            ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), ticks.size());
            break;
        }
        case AxisScale_Asinh:
        case AxisScale_SymLog:
        {
            bool crosses_zero = x_limits[0] * x_limits[1] < 0;

            // Hack: for purely positive, log-like x-axes, the internal ImPlot logarithmic ticks look nice, but we still
            // want to use our own forward and inverse xforms. Setting the axis to ImPlotScale_Log10 sets the
            // logarithmic tick locator, which ImPlot keeps even after the subsequent ImPlot::SetupAxisScale call with
            // our own xforms
            if (!crosses_zero)
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);

            ImPlot::SetupAxisScale(ImAxis_X1, axis_scale_fwd_xform, axis_scale_inv_xform, &x_axis);

            if (crosses_zero) // crosses zero
            {
                auto ticks = powers_of_10_ticks();
                ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), ticks.size());
                ImPlot::SetupAxisFormat(ImAxis_X1, format_power_of_10);
            }

            break;
        }
        }

        //
        // now do the actual plotting
        //

        for (int c = 0; c < std::min(3, group.num_channels); ++c)
        {
            ImPlot::PushStyleColor(ImPlotCol_Fill, colors[c]);
            ImPlot::PushStyleColor(ImPlotCol_Line, float4{0.f});
            if (bin_type != 0)
                ImPlot::PlotShaded(names[c].c_str(), stats[c]->histogram.xs.data(), stats[c]->histogram.ys.data(),
                                   Histogram::NUM_BINS);
            else
                ImPlot::PlotStairs(names[c].c_str(), stats[c]->histogram.xs.data(), stats[c]->histogram.ys.data(),
                                   Histogram::NUM_BINS, ImPlotStairsFlags_Shaded);
            ImPlot::PopStyleColor(2);
        }
        for (int c = 0; c < std::min(3, group.num_channels); ++c)
        {
            ImPlot::PushStyleColor(ImPlotCol_Fill, float4{0.f});
            ImPlot::PushStyleColor(ImPlotCol_Line, float4{colors[c].xyz(), 1.0f});
            if (bin_type != 0)
                ImPlot::PlotLine(names[c].c_str(), stats[c]->histogram.xs.data(), stats[c]->histogram.ys.data(),
                                 Histogram::NUM_BINS);
            else
                ImPlot::PlotStairs(names[c].c_str(), stats[c]->histogram.xs.data(), stats[c]->histogram.ys.data(),
                                   Histogram::NUM_BINS);
            ImPlot::PopStyleColor(2);
        }

        for (int c = 0; c < std::min(3, group.num_channels); ++c)
        {
            ImPlot::PushStyleColor(ImPlotCol_InlayText, float4{colors[c].xyz(), 1.0f});
            ImPlot::PushStyleColor(ImPlotCol_Line, float4{colors[c].xyz(), 1.0f});

            ImPlot::PlotInfLines("##min", &stats[c]->minimum, 1);
            ImPlot::PlotText(fmt::format("min({})", names[c]).c_str(), stats[c]->minimum,
                             lerp(y_limits[0], y_limits[1], 0.5f), {-HelloImGui::EmSize(), 0.f},
                             ImPlotTextFlags_Vertical);

            ImPlot::PlotInfLines("##min", &stats[c]->average, 1);
            ImPlot::PlotText(fmt::format("avg({})", names[c]).c_str(), stats[c]->average,
                             lerp(y_limits[0], y_limits[1], 0.5f), {-HelloImGui::EmSize(), 0.f},
                             ImPlotTextFlags_Vertical);

            ImPlot::PlotInfLines("##max", &stats[c]->maximum, 1);
            ImPlot::PlotText(fmt::format("max({})", names[c]).c_str(), stats[c]->maximum,
                             lerp(y_limits[0], y_limits[1], 0.5f), {-HelloImGui::EmSize(), 0.f},
                             ImPlotTextFlags_Vertical);
            ImPlot::PopStyleColor(2);
        }

        if (contains(hovered_pixel) && g_app()->app_pos_in_viewport(ImGui::GetIO().MousePos))
        {
            for (int c = 0; c < std::min(3, group.num_channels); ++c)
            {
                ImPlot::PushStyleColor(ImPlotCol_Fill, float4{0.f});
                ImPlot::PushStyleColor(ImPlotCol_Line, float4{colors[c].xyz(), 1.0f});

                float marker_size = 6.f;

                float c_bin = stats[c]->histogram.value_to_bin(color32[c]);
                float y1    = stats[c]->histogram.bin_y(c_bin);

                // calculate the height of the up marker so that it sits just above the x axis.
                float2 pixel = ImPlot::PlotToPixels(ImPlotPoint(stats[c]->histogram.bin_x(c_bin), y_limits[0]));
                pixel.y -= 0.66f * marker_size; // roughly the
                auto  bottom = ImPlot::PixelsToPlot(pixel);
                float y0     = bottom.y;

                ImPlot::SetNextMarkerStyle(ImPlotMarker_Up, marker_size);
                ImPlot::PlotStems(fmt::format("##hover_{}", c).c_str(), &color32[c], &y0, 1, y1);
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.f);
                ImPlot::PlotStems(fmt::format("##hover_{}", c).c_str(), &color32[c], &y1, 1, y0);

                ImPlot::PopStyleColor(2);
            }
        }

        ImPlot::EndPlot();
    }
}

void Image::draw_channels_list()
{
    static int                  tree_view = 1;
    static ImGuiSelectableFlags selectable_flags =
        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
    static ImGuiTableFlags table_flags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Show channels as a"), ImGui::SameLine();
    ImGui::RadioButton("tree", &tree_view, 1), ImGui::SameLine();
    ImGui::RadioButton("flat list", &tree_view, 0);

    if (ImGui::BeginTable("ChannelList", 3, table_flags))
    {
        const float icon_width  = ImGui::CalcTextSize(ICON_FA_EYE_LOW_VISION).x;
        const float icon_indent = icon_width + ImGui::CalcTextSize(" ").x;

        ImGui::TableSetupColumn(ICON_FA_LIST_OL, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
                                1.75f * icon_width);
        ImGui::TableSetupColumn(ICON_FA_EYE, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
                                icon_width);
        ImGui::TableSetupColumn(tree_view ? "Layer or channel group name" : "Layer.channel group name",
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
        ImGui::TableHeadersRow();

        std::set<string> created_levels;

        for (size_t l = 0; l < layers.size(); ++l)
        {
            auto &layer = layers[l];

            float total_indent = 0.f;
            // if tree view is enabled, list the levels of the layer path and indent
            if (tree_view)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

                // Split the input string by dot
                std::istringstream iss(layer.name);
                string             level, path;
                // Iterate through the layer path levels
                while (std::getline(iss, level, '.'))
                {
                    path = (path.empty() ? "" : path + ".") + level;
                    // if this is the first time we have encountered this folder, list it
                    if (auto result = created_levels.insert(path); result.second)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(fmt::format("{} {}", ICON_FA_FOLDER_OPEN, level).c_str());
                    }

                    ImGui::Indent(icon_indent);
                    total_indent += icon_indent;
                }
                ImGui::PopStyleColor();
            }

            for (size_t g = 0; g < layer.groups.size(); ++g)
            {
                auto  &group = groups[layer.groups[g]];
                string name  = tree_view ? group.name : layer.name + group.name;

                bool is_selected_channel = selected_group == layer.groups[g];

                ImGui::PushRowColors(is_selected_channel, false);
                {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    auto hotkey =
                        layer.groups[g] < 10 ? fmt::format(ICON_FA_ANGLE_UP "{}", mod(layer.groups[g] + 1, 10)) : "";
                    ImGui::AlignCursor(hotkey, 1.0f);
                    if (ImGui::Selectable(hotkey.c_str(), is_selected_channel, selectable_flags))
                    {
                        selected_group = layer.groups[g];
                        set_as_texture(selected_group, *g_app()->shader(), "primary");
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(selected_group == layer.groups[g] ? ICON_FA_EYE : "");

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(
                        fmt::format("{} {}", ICON_FA_LAYER_GROUP, tree_view ? group.name : layer.name + group.name)
                            .c_str());
                }
                ImGui::PopStyleColor(3);
            }
            if (total_indent != 0)
                ImGui::Unindent(total_indent);
        }

        ImGui::EndTable();
    }
}

void Image::draw_info()
{
    auto &io = ImGui::GetIO();

    auto hovered_pixel = int2{g_app()->pixel_at_app_pos(io.MousePos)};

    // float4 hovered = g_app()->image_pixel(hovered_pixel);
    auto &group = groups[selected_group];

    auto sans_font = g_app()->font("sans regular");
    auto bold_font = g_app()->font("sans bold");
    auto mono_font = g_app()->font("mono regular");

    auto property_name = [bold_font](const string &text)
    {
        ImGui::PushFont(bold_font);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
    };
    auto property_value = [](const string &text, ImFont *font, bool wrapped = false)
    {
        ImGui::SameLine();
        ImGui::PushFont(font);

        float avail_w = ImGui::GetContentRegionAvail().x;
        float text_w  = ImGui::CalcTextSize(text.c_str()).x;

        if (text_w < avail_w)
        {
            // place it on the same line as the property name
            ImGui::TextUnformatted(text.c_str());
        }
        else
        {
            // place it indented on the next line
            ImGui::NewLine();
            ImGui::Indent();
            if (wrapped)
                ImGui::TextWrapped("%s", text.c_str());
            else
                ImGui::TextUnformatted(text.c_str());
            ImGui::Unindent();
        }
        ImGui::PopFont();
    };

    property_name("File name:");
    property_value(filename, sans_font, true);

    property_name("Part name:");
    property_value(partname.empty() ? "<none>" : partname, sans_font, true);

    property_name("Resolution:");
    property_value(fmt::format("{} x {}", size().x, size().y), sans_font);

    property_name("Data window:");
    property_value(
        fmt::format("({}, {}) : ({}, {})", data_window.min.x, data_window.min.y, data_window.max.x, data_window.max.y),
        sans_font);

    property_name("Display window:");
    property_value(fmt::format("({}, {}) : ({}, {})", display_window.min.x, display_window.min.y, display_window.max.x,
                               display_window.max.y),
                   sans_font);

    property_name("Luminance weights:");
    if (luminance_weights != Image::Rec709_luminance_weights)
        property_value(
            fmt::format("{:+8.2e}, {:+8.2e}, {:+8.2e}", luminance_weights.x, luminance_weights.y, luminance_weights.z),
            mono_font);
    else
        property_value(fmt::format("<Default Rec. 709 weights>\n{:+8.2e}, {:+8.2e}, {:+8.2e}", luminance_weights.x,
                                   luminance_weights.y, luminance_weights.z),
                       mono_font);

    property_name("To Rec 709 RGB:");
    if (M_to_Rec709 != float4x4{la::identity})
    {
        string mat_text = fmt::format("{:+8.2e}, {:+8.2e}, {:+8.2e}, {:+8.2e}\n"
                                      "{:+8.2e}, {:+8.2e}, {:+8.2e}, {:+8.2e}\n"
                                      "{:+8.2e}, {:+8.2e}, {:+8.2e}, {:+8.2e}\n"
                                      "{:+8.2e}, {:+8.2e}, {:+8.2e}, {:+8.2e}",
                                      M_to_Rec709[0][1], M_to_Rec709[0][1], M_to_Rec709[0][2], M_to_Rec709[0][3],
                                      M_to_Rec709[1][1], M_to_Rec709[1][1], M_to_Rec709[1][2], M_to_Rec709[1][3],
                                      M_to_Rec709[2][1], M_to_Rec709[2][1], M_to_Rec709[2][2], M_to_Rec709[2][3],
                                      M_to_Rec709[3][1], M_to_Rec709[3][1], M_to_Rec709[3][2], M_to_Rec709[3][3]);
        property_value(mat_text, mono_font);
    }
    else
        property_value("<Identity matrix>", mono_font);

    static const ImGuiTableFlags table_flags =
        ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH | ImGuiTableFlags_RowBg;
    ImGui::SeparatorText("Channel statistics");
    // if (ImGui::BeginTable("Channel statistics", 5, table_flags))
    // {
    //     ImGui::PushFont(bold_font);
    //     ImGui::TableSetupColumn(ICON_FA_LAYER_GROUP, ImGuiTableColumnFlags_WidthFixed/*,
    //                                     ImGui::CalcTextSize("channel").x*/);
    //     // ImGui::TableSetupColumn(ICON_FA_CROSSHAIRS, ImGuiTableColumnFlags_WidthStretch);
    //     ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthStretch);
    //     ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthStretch);
    //     ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch);
    //     ImGui::TableSetupColumn(ICON_FA_SKULL_CROSSBONES "NaNs", ImGuiTableColumnFlags_WidthStretch);
    //     ImGui::TableHeadersRow();
    //     ImGui::PopFont();

    //     for (int c = 0; c < group.num_channels; ++c)
    //     {
    //         auto  &channel = channels[group.channels[c]];
    //         string name    = Channel::tail(channel.name);
    //         auto   stats   = channel.get_statistics();
    //         ImGui::TableNextRow(), ImGui::TableNextColumn();

    //         ImGui::PushFont(bold_font);
    //         ImGui::Text(" %s", name.c_str()), ImGui::TableNextColumn();
    //         ImGui::PopFont();
    //         // ImGui::PushFont(mono_font);
    //         // ImGui::Text("%-6.3g", hovered[c]), ImGui::TableNextColumn();
    //         ImGui::Text("%-6.3g", stats->minimum), ImGui::TableNextColumn();
    //         ImGui::Text("%-6.3g", stats->average), ImGui::TableNextColumn();
    //         ImGui::Text("%-6.3g", stats->maximum), ImGui::TableNextColumn();
    //         ImGui::Text("%d", stats->nan_pixels);
    //         // ImGui::PopFont();
    //     }
    //     ImGui::EndTable();
    // }

    // Set the hover and active colors to be the same as the background color
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
    if (ImGui::BeginTable("Channel statistics", group.num_channels + 1, table_flags))
    {
        PixelStatistics *channel_stats[4] = {nullptr, nullptr, nullptr, nullptr};
        string           channel_names[4];
        for (int c = 0; c < group.num_channels; ++c)
        {
            auto &channel    = channels[group.channels[c]];
            channel_stats[c] = channel.get_statistics();
            channel_names[c] = Channel::tail(channel.name);
        }

        // set up header row
        ImGui::PushFont(bold_font);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int c = 0; c < group.num_channels; ++c)
            ImGui::TableSetupColumn(fmt::format("{}{}", ICON_FA_LAYER_GROUP, channel_names[c]).c_str(),
                                    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGui::PopFont();

        const char   *stat_names[] = {"Min", "Avg", "Max", "# of NaNs", "# of Infs"};
        constexpr int NUM_STATS    = sizeof(stat_names) / sizeof(stat_names[0]);
        for (int s = 0; s < NUM_STATS; ++s)
        {
            ImGui::PushFont(bold_font);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
            ImGui::TextUnformatted(stat_names[s]);
            ImGui::PopFont();
            for (int c = 0; c < group.num_channels; ++c)
            {
                ImGui::TableNextColumn();
                switch (s)
                {
                case 0: ImGui::Text("%-6.3g", channel_stats[c]->minimum); break;
                case 1: ImGui::Text("%-6.3g", channel_stats[c]->average); break;
                case 2: ImGui::Text("%-6.3g", channel_stats[c]->maximum); break;
                case 3: ImGui::Text("%d", channel_stats[c]->nan_pixels); break;
                case 4:
                default: ImGui::Text("%d", channel_stats[c]->inf_pixels); break;
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
}
