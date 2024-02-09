//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

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
    static ImPlotScale y_axis   = ImPlotScale_Linear;
    static AxisScale_  x_axis   = AxisScale_Asinh;
    static int         bin_type = 1;
    {
        float combo_width = 0.5f * (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) -
                            ImGui::CalcTextSize("X:").x - ImGui::GetStyle().ItemSpacing.x;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Y:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(combo_width);
        ImGui::Combo("##Y-axis type", &y_axis, "Linear\0Log\0\0");
        ImGui::SameLine();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("X:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(combo_width);
        ImGui::Combo("##X-axis type", &x_axis, "Linear\0sRGB\0Asinh\0\0");

        // ImGui::SetNextItemWidth(combo_width);
        // ImGui::RadioButton("constant", &bin_type, 0);
        // ImGui::SameLine();
        // ImGui::SetNextItemWidth(combo_width);
        // ImGui::RadioButton("linear", &bin_type, 1);
    }

    auto            &group    = groups[selected_group];
    PixelStatistics *stats[4] = {nullptr, nullptr, nullptr, nullptr};
    string           names[4];
    auto             colors = group.colors();

    float2 x_limits = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    float2 y_limits = x_limits;
    for (int c = 0; c < std::min(3, group.num_channels); ++c)
    {
        auto &channel = channels[group.channels[c]];
        stats[c]      = channel.get_statistics(exposure, x_axis, y_axis);
        names[c]      = Channel::tail(channel.name);
        y_limits[0]   = std::min(y_limits[0], stats[c]->histogram.y_limits[0]);
        y_limits[1]   = std::max(y_limits[1], stats[c]->histogram.y_limits[1]);
        x_limits[0]   = std::min(x_limits[0], stats[c]->histogram.x_limits[0]);
        x_limits[1]   = std::max(x_limits[1], stats[c]->histogram.x_limits[1]);
    }

    ImPlot::GetStyle().PlotMinSize = {100, 100};

    if (ImPlot::BeginPlot("##Histogram", ImVec2(-1, -1)))
    {
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

        auto powers_of_10_ticks = [x_limits, &format_power_of_10](int num_orders = 10)
        {
            float avail_width = std::max(ImPlot::GetStyle().PlotMinSize.x, ImGui::GetContentRegionAvail().x) -
                                2.f * ImPlot::GetStyle().PlotPadding.x;

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
            }

            break;
        }
        }

        ImPlot::SetupAxesLimits(x_limits[0], x_limits[1], y_limits[0], y_limits[1], ImPlotCond_Always);

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
                    ImGui::AlignCursor(fmt::format(ICON_FA_ANGLE_UP "{}", layer.groups[g] + 1), 1.0f);
                    if (ImGui::Selectable(fmt::format(ICON_FA_ANGLE_UP "{}##group_number", layer.groups[g] + 1).c_str(),
                                          is_selected_channel, selectable_flags))
                    {
                        selected_group = layer.groups[g];
                        // set_as_texture(selected_group, *m_shader, "primary");
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

// void Image::draw() const
// {
//     const float            TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
//     static ImGuiTableFlags flags           = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
//                                    ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody;

//     static ImGuiTreeNodeFlags tree_node_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;

//     if (ImGui::BeginTable("3ways", 2, flags))
//     {
//         // The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
//         ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
//         ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 18.0f);
//         ImGui::TableHeadersRow();

//         // for (size_t p = 0; p < parts.size(); ++p)
//         // {
//         //     auto &part = parts[p];

//         //     ImGui::TableNextRow();
//         //     ImGui::TableNextColumn();
//         //     string partname = filename + (part.name.length() ? (":"s + part.name) : part.name);
//         //     bool   part_open = ImGui::TreeNodeEx(partname.c_str(), tree_node_flags);
//         //     ImGui::TableNextColumn();
//         //     ImGui::TextUnformatted("File Part");
//         //     if (part_open)
//         //     {
//         //         for (size_t l = 0; l < part.layers.size(); ++l)
//         //         {
//         //             auto &layer = part.layers[l];

//         //             ImGui::TableNextRow();
//         //             ImGui::TableNextColumn();
//         //             bool layer_open = ImGui::TreeNodeEx(layer.name.c_str(), tree_node_flags);
//         //             ImGui::TableNextColumn();
//         //             ImGui::TextUnformatted("Layer");

//         //             if (layer_open)
//         //             {
//         //                 for (size_t g = 0; g < layer.groups.size(); ++g)
//         //                 {
//         //                     auto &group = layer.groups[g];

//         //                     ImGui::TableNextRow();
//         //                     ImGui::TableNextColumn();
//         //                     ImGui::TreeNodeEx(group.name.c_str(), tree_node_flags | ImGuiTreeNodeFlags_Leaf |
//         //                                                               ImGuiTreeNodeFlags_Bullet |
//         //                                                               ImGuiTreeNodeFlags_NoTreePushOnOpen);
//         //                     ImGui::TableNextColumn();
//         //                     ImGui::TextUnformatted(
//         //                         fmt::format("Channel{}", group.num_channels > 1 ? " group" : "").c_str());
//         //                 }
//         //                 ImGui::TreePop();
//         //             }
//         //         }
//         //         ImGui::TreePop();
//         //     }
//         // }

//         // static ImVector<int> selection;
//         static int selected_item = -1;

//         ImGui::TableNextRow();
//         ImGui::TableNextColumn();
//         bool file_open = ImGui::TreeNodeEx(filename.c_str(), tree_node_flags);
//         ImGui::TableNextColumn();
//         ImGui::TextUnformatted(parts.size() > 1 ? "Multi-part image" : "Image");

//         int itemID = 0;
//         if (file_open)
//         {
//             for (size_t p = 0; p < parts.size(); ++p)
//             {
//                 auto  &part        = parts[p];
//                 string part_prefix = parts.size() > 1 ? part.partname : ""s;

//                 if (part_prefix.length())
//                     part_prefix = part_prefix + "."s;

//                 for (size_t l = 0; l < part.layers.size(); ++l)
//                 {
//                     auto &layer = part.layers[l];

//                     for (size_t g = 0; g < layer.groups.size(); ++g)
//                     {
//                         auto &group = layer.groups[g];

//                         string name = part_prefix + layer.name + group.name;

//                         ImGui::TableNextRow();
//                         ImGui::TableNextColumn();

//                         // const bool item_is_selected = selection.contains(++itemID);
//                         const bool item_is_selected = ++itemID == selected_item;

//                         ImGuiSelectableFlags selectable_flags =
//                             ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
//                         if (ImGui::Selectable(name.c_str(), item_is_selected, selectable_flags))
//                         {
//                             selected_item = itemID;
//                             // if (ImGui::GetIO().KeyCtrl)
//                             // {
//                             //     if (item_is_selected)
//                             //         selection.find_erase_unsorted(itemID);
//                             //     else
//                             //         selection.push_back(itemID);
//                             // }
//                             // else
//                             // {
//                             //     selection.clear();
//                             //     selection.push_back(itemID);
//                             // }
//                         }
//                         ImGui::TableNextColumn();
//                         ImGui::TextUnformatted(
//                             fmt::format("Channel{}", group.num_channels > 1 ? " group" : "").c_str());

//                         // ImGui::TreeNodeEx(name.c_str(), tree_node_flags | ImGuiTreeNodeFlags_Leaf |
//                         //                                     // ImGuiTreeNodeFlags_Bullet |
//                         //                                     ImGuiTreeNodeFlags_NoTreePushOnOpen);
//                         // ImGui::TableNextColumn();
//                         // ImGui::TextUnformatted(
//                         //     fmt::format("Channel{}", group.num_channels > 1 ? " group" : "").c_str());
//                     }
//                 }
//             }
//             ImGui::TreePop();
//         }

//         ImGui::EndTable();
//     }
// }
