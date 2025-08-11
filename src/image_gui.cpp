//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "fwd.h"

#include "app.h"
#include "common.h"
#include "image.h"
#include "shader.h"
#include "texture.h"

#include "fonts.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"

#include "nvgui/property_editor.hpp"

#include <ImfStandardAttributes.h>

#include <sstream>
#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

void Image::draw_histogram()
{
    static int        bin_type  = 1;
    static ImPlotCond plot_cond = ImPlotCond_Always;
    {
        const ImVec2 button_size = ImGui::IconButtonSize();
        float        combo_width =
            std::max(HelloImGui::EmSize(5.f),
                     0.5f * (ImGui::GetContentRegionAvail().x - button_size.x - 2.f * ImGui::GetStyle().ItemSpacing.x) -
                         (ImGui::CalcTextSize("X:").x + ImGui::GetStyle().ItemInnerSpacing.x));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Y:");
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(combo_width);
        ImGui::Combo("##Y-axis type", &hdrview()->histogram_y_scale(), "Linear\0Log\0\0");
        ImGui::SameLine();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("X:");
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(combo_width);
        ImGui::Combo("##X-axis type", &hdrview()->histogram_x_scale(), "Linear\0sRGB\0Asinh\0\0");

        ImGui::SameLine();

        if (ImGui::IconButton(plot_cond == ImPlotCond_Always ? ICON_MY_FIT_AXES : ICON_MY_MANUAL_AXES))
            plot_cond = (plot_cond == ImPlotCond_Always) ? ImPlotCond_Once : ImPlotCond_Always;
        ImGui::WrappedTooltip((plot_cond == ImPlotCond_Always)
                                  ? "Click to allow manually panning/zooming in histogram"
                                  : "Click to auto-fit histogram axes based on the exposure.");
    }

    auto        hovered_pixel = int2{hdrview()->pixel_at_app_pos(ImGui::GetIO().MousePos)};
    float4      color32       = raw_pixel(hovered_pixel);
    auto       &group         = groups[selected_group];
    PixelStats *stats[4]      = {nullptr, nullptr, nullptr, nullptr};
    string      names[4];
    auto        colors = group.colors();

    float2 x_limits = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    float2 y_limits = x_limits;
    for (int c = 0; c < std::min(4, group.num_channels); ++c)
    {
        auto &channel = channels[group.channels[c]];
        channel.update_stats(c, hdrview()->current_image(), hdrview()->reference_image());
        stats[c]    = channel.get_stats();
        y_limits[0] = std::min(y_limits[0], stats[c]->hist_y_limits[0]);
        y_limits[1] = std::max(y_limits[1], stats[c]->hist_y_limits[1]);
        auto xl     = stats[c]->x_limits(hdrview()->exposure_live(), hdrview()->histogram_x_scale());
        x_limits[0] = std::min(x_limits[0], xl[0]);
        x_limits[1] = std::max(x_limits[1], xl[1]);
        names[c]    = Channel::tail(channel.name);
    }

    ImPlot::GetStyle().PlotMinSize = {100, 100};

    ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
    if (ImPlot::BeginPlot("##Histogram", ImVec2(-1, -1)))
    {
        ImPlot::GetInputMap().ZoomRate = 0.03f;
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoTickLabels);
        ImPlot::SetupAxisScale(ImAxis_Y1, hdrview()->histogram_y_scale() == AxisScale_Linear ? ImPlotScale_Linear
                                                                                             : ImPlotScale_SymLog);

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
                float scaleMin      = (float)axis_scale_fwd_xform(x_limits[0], &hdrview()->histogram_x_scale());
                float scaleMax      = (float)axis_scale_fwd_xform(x_limits[1], &hdrview()->histogram_x_scale());
                float s             = (float)axis_scale_fwd_xform(plt, &hdrview()->histogram_x_scale());
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
                    (void)format_power_of_10(value, buff.data(), (int)buff.size(), nullptr);
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
        switch (hdrview()->histogram_x_scale())
        {
        case AxisScale_Linear: ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear); break;
        case AxisScale_SRGB:
        {
            auto ticks = powers_of_10_ticks();
            ImPlot::SetupAxisScale(ImAxis_X1, axis_scale_fwd_xform, axis_scale_inv_xform,
                                   &hdrview()->histogram_x_scale());
            ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), (int)ticks.size());
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

            ImPlot::SetupAxisScale(ImAxis_X1, axis_scale_fwd_xform, axis_scale_inv_xform,
                                   &hdrview()->histogram_x_scale());

            if (crosses_zero) // crosses zero
            {
                auto ticks = powers_of_10_ticks();
                ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), (int)ticks.size());
                ImPlot::SetupAxisFormat(ImAxis_X1, format_power_of_10);
            }

            break;
        }
        }

        //
        // now do the actual plotting
        //

        for (int c = 0; c < std::min(4, group.num_channels); ++c)
        {
            ImPlot::PushStyleColor(ImPlotCol_Fill, colors[c]);
            ImPlot::PushStyleColor(ImPlotCol_Line, float4{0.f});
            if (bin_type != 0)
                ImPlot::PlotShaded(names[c].c_str(), stats[c]->hist_xs.data(), stats[c]->hist_ys.data(),
                                   PixelStats::NUM_BINS);
            else
                ImPlot::PlotStairs(names[c].c_str(), stats[c]->hist_xs.data(), stats[c]->hist_ys.data(),
                                   PixelStats::NUM_BINS, ImPlotStairsFlags_Shaded);
            ImPlot::PopStyleColor(2);
        }
        for (int c = 0; c < std::min(4, group.num_channels); ++c)
        {
            ImPlot::PushStyleColor(ImPlotCol_Fill, float4{0.f});
            ImPlot::PushStyleColor(ImPlotCol_Line, float4{colors[c].xyz(), 1.0f});
            if (bin_type != 0)
                ImPlot::PlotLine(names[c].c_str(), stats[c]->hist_xs.data(), stats[c]->hist_ys.data(),
                                 PixelStats::NUM_BINS);
            else
                ImPlot::PlotStairs(names[c].c_str(), stats[c]->hist_xs.data(), stats[c]->hist_ys.data(),
                                   PixelStats::NUM_BINS);
            ImPlot::PopStyleColor(2);
        }

        for (int c = 0; c < std::min(4, group.num_channels); ++c)
        {
            ImPlot::PushStyleColor(ImPlotCol_InlayText, float4{colors[c].xyz(), 1.0f});
            ImPlot::PushStyleColor(ImPlotCol_Line, float4{colors[c].xyz(), 1.0f});

            ImPlot::PlotInfLines("##min", &stats[c]->summary.minimum, 1);
            ImPlot::PlotText(fmt::format("min({})", names[c]).c_str(), stats[c]->summary.minimum,
                             lerp(y_limits[0], y_limits[1], 0.5f), {-HelloImGui::EmSize(), 0.f},
                             ImPlotTextFlags_Vertical);

            ImPlot::PlotInfLines("##min", &stats[c]->summary.average, 1);
            ImPlot::PlotText(fmt::format("avg({})", names[c]).c_str(), stats[c]->summary.average,
                             lerp(y_limits[0], y_limits[1], 0.5f), {-HelloImGui::EmSize(), 0.f},
                             ImPlotTextFlags_Vertical);

            ImPlot::PlotInfLines("##max", &stats[c]->summary.maximum, 1);
            ImPlot::PlotText(fmt::format("max({})", names[c]).c_str(), stats[c]->summary.maximum,
                             lerp(y_limits[0], y_limits[1], 0.5f), {-HelloImGui::EmSize(), 0.f},
                             ImPlotTextFlags_Vertical);
            ImPlot::PopStyleColor(2);
        }

        if (contains(hovered_pixel) && hdrview()->app_pos_in_viewport(ImGui::GetIO().MousePos))
        {
            for (int c = 0; c < std::min(4, group.num_channels); ++c)
            {
                ImPlot::PushStyleColor(ImPlotCol_Fill, float4{0.f});
                ImPlot::PushStyleColor(ImPlotCol_Line, float4{colors[c].xyz(), 1.0f});

                float marker_size = 6.f;

                float c_bin = (float)stats[c]->value_to_bin(color32[c]);
                float y1    = stats[c]->bin_y((int)c_bin);

                // calculate the height of the up marker so that it sits just above the x axis.
                float2 pixel = ImPlot::PlotToPixels(ImPlotPoint(stats[c]->bin_x((int)c_bin), y_limits[0]));
                pixel.y -= 0.66f * marker_size; // roughly the
                auto  bottom = ImPlot::PixelsToPlot(pixel);
                float y0     = (float)bottom.y;

                ImPlot::SetNextMarkerStyle(ImPlotMarker_Up, marker_size);
                ImPlot::PlotStems(fmt::format("##hover_{}", c).c_str(), &color32[c], &y0, 1, y1);
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.f);
                ImPlot::PlotStems(fmt::format("##hover_{}", c).c_str(), &color32[c], &y1, 1, y0);

                ImPlot::PopStyleColor(2);
            }
        }

        ImPlot::EndPlot();
    }
    ImGui::PopFont();
}

void Image::draw_layer_groups(const Layer &layer, int img_idx, int &id, bool is_current, bool is_reference,
                              bool short_names, int &visible_group)
{
    static ImGuiTreeNodeFlags tree_node_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;
    for (size_t g = 0; g < layer.groups.size(); ++g)
    {
        auto  &group      = groups[layer.groups[g]];
        string group_name = group.num_channels == 1 ? group.name : "(" + group.name + ")";
        string name       = string(ICON_MY_CHANNEL_GROUP) + " " + (short_names ? group_name : layer.name + group_name);

        // check if any of the contained channels pass the channel filter
        if (!group.visible)
            continue;

        bool is_selected_channel  = is_current && selected_group == layer.groups[g];
        bool is_reference_channel = is_reference && reference_group == layer.groups[g];

        ImGui::PushRowColors(is_selected_channel, is_reference_channel, ImGui::GetIO().KeyShift);
        {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            string shortcut = is_current && visible_group < 10
                                  ? fmt::format(ICON_MY_KEY_CONTROL "{}", mod(visible_group + 1, 10))
                                  : "";
            ImGui::TextAligned(shortcut, 1.0f);

            // ImGui::TableNextColumn();
            // ImGui::TextUnformatted(is_selected_channel ? ICON_MY_VISIBILITY : "");

            ImGui::TableNextColumn();
            ImGui::TreeNodeEx((void *)(intptr_t)id++,
                              tree_node_flags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                  (is_selected_channel || is_reference_channel ? ImGuiTreeNodeFlags_Selected
                                                                               : ImGuiTreeNodeFlags_None),
                              "%s", name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                if (ImGui::GetIO().KeyShift)
                {
                    spdlog::trace("Shift-clicked on {}", name);
                    // check if we are already the reference channel group
                    if (is_reference_channel)
                    {
                        spdlog::trace("Clearing reference image");
                        hdrview()->set_reference_image_index(-1, true);
                        reference_group = 0;
                    }
                    else
                    {
                        spdlog::trace("Setting reference image to {}", img_idx);
                        hdrview()->set_reference_image_index(img_idx);
                        reference_group = layer.groups[g];
                    }
                    set_as_texture(Target_Secondary);
                }
                else
                {
                    hdrview()->set_current_image_index(img_idx);
                    selected_group = layer.groups[g];
                    set_as_texture(Target_Primary);
                }
            }
        }
        ImGui::PopStyleColor(3);
        ++visible_group;
    }
}

/*!

*/
void Image::draw_layer_node(const LayerTreeNode &node, int img_idx, int &id, bool is_current, bool is_reference,
                            int &visible_group)
{
    static ImGuiTreeNodeFlags tree_node_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;

    if (node.leaf_layer >= 0)
        // draw this node's leaf channel groups
        draw_layer_groups(layers[node.leaf_layer], img_idx, id, is_current, is_reference, true, visible_group);

    for (auto &c : node.children)
    {
        const LayerTreeNode &child_node = c.second;
        if (child_node.visible_groups == 0)
            continue;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGuiCol_Header);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGuiCol_Header);
        bool open = ImGui::TreeNodeEx((void *)(intptr_t)id++, tree_node_flags, "%s %s", ICON_MY_OPEN_IMAGE,
                                      child_node.name.c_str());
        ImGui::PopStyleColor(3);
        if (open)
        {
            draw_layer_node(child_node, img_idx, id, is_current, is_reference, visible_group);
            ImGui::TreePop();
        }
        else
        {
            // still account for visible groups within the closed tree node
            visible_group += child_node.visible_groups;
        }
    }
}

int Image::draw_channel_rows(int img_idx, int &id, bool is_current, bool is_reference)
{
    int visible_group = 0;
    for (size_t l = 0; l < layers.size(); ++l)
        draw_layer_groups(layers[l], img_idx, id, is_current, is_reference, false, visible_group);

    return visible_group;
}

void Image::draw_channels_list(bool is_reference, bool is_current)
{
    static int                       tree_view = 1;
    static constexpr ImGuiTableFlags table_flags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Show channels as a"), ImGui::SameLine();
    ImGui::RadioButton("tree", &tree_view, 1), ImGui::SameLine();
    ImGui::RadioButton("flat list", &tree_view, 0);

    if (ImGui::BeginTable("ChannelList", 2, table_flags))
    {
        const float icon_width = ImGui::IconSize().x;

        ImGui::TableSetupColumn(ICON_MY_LIST_OL, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
                                1.25f * icon_width);
        // ImGui::TableSetupColumn(ICON_MY_VISIBILITY,
        //                         ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, icon_width);
        ImGui::TableSetupColumn(tree_view ? "Layer or channel group name" : "Layer.channel group name",
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
        ImGui::TableHeadersRow();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.5f * icon_width);

        int id = 0;

        if (tree_view)
        {
            // ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
            draw_channel_tree(hdrview()->current_image_index(), id, is_current, is_reference);
            // ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
        }
        else
        {
            ImGui::Unindent(1.f * ImGui::GetTreeNodeToLabelSpacing());
            draw_channel_rows(hdrview()->current_image_index(), id, is_current, is_reference);
            ImGui::Indent(1.f * ImGui::GetTreeNodeToLabelSpacing());
        }

        ImGui::PopStyleVar(2);

        ImGui::EndTable();
    }
}

template <typename Type>
inline Type ScaleFromNormalized(Type const x, Type const newMin, Type const newMax)
{
    return x * (newMax - newMin) + newMin;
}

void Image::draw_info()
{
    auto bold_font = hdrview()->font("sans bold");

    namespace PE = nvgui::PropertyEditor;

    static ImGuiTextFilter filter;
    const ImVec2           button_size   = ImGui::IconButtonSize();
    bool                   filter_active = filter.IsActive(); // save here to avoid flicker

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InputTextWithHint("##metadata filter",
                                 ICON_MY_FILTER "Filter (format: [include|-exclude][,...]; e.g. "
                                                "\"include_this,-but_not_this,also_include_this\")",
                                 filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf)))
        filter.Build();
    if (filter_active)
    {
        ImGui::SameLine(0.f, 0.f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
        if (ImGui::IconButton(ICON_MY_DELETE))
            filter.Clear();
    }

    static const ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg;
    if (PE::begin("Image info", table_flags))
    {
        auto filtered_property = [&](const string &property_name, const string &value, const string &tooltip = "")
        {
            if (filter.PassFilter((property_name + " " + value).c_str()))
                ImGui::WrappedTextProperty(property_name, value, tooltip, bold_font);
        };
        filtered_property("File name", filename);
        filtered_property("Part name", partname.empty() ? "<none>" : partname.c_str());
        filtered_property("Channel selector", channel_selector.empty() ? "<none>" : channel_selector.c_str());
        filtered_property("Loader", metadata.value<string>("loader", "unknown"));
        filtered_property("File's bit depth", metadata.value<string>("bit depth", "unknown"));
        filtered_property("Resolution", fmt::format("{} {} {}", size().x, ICON_MY_TIMES, size().y));
        filtered_property("Data window", fmt::format("[{}, {}) {} [{}, {})", data_window.min.x, data_window.max.x,
                                                     ICON_MY_TIMES, data_window.min.y, data_window.max.y));
        filtered_property("Display window",
                          fmt::format("[{}, {}) {} [{}, {})", display_window.min.x, display_window.max.x, ICON_MY_TIMES,
                                      display_window.min.y, display_window.max.y));
        filtered_property("Straight alpha", fmt::format("{}", file_has_straight_alpha));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (ImGui::TreeNodeEx("Header", ImGuiTreeNodeFlags_SpanAllColumns))
        {
            if (metadata.contains("header") && metadata["header"].is_object())
            {
                for (auto &field : metadata["header"].items())
                {
                    const std::string &key       = field.key();
                    const auto        &field_obj = field.value();
                    if (!field_obj.is_object() || !field_obj.contains("string"))
                        continue;

                    auto value  = field_obj["string"].get<std::string>();
                    auto concat = key + " " + value;
                    if (!filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                        continue;

                    ImGui::WrappedTextProperty(key, value, "", bold_font);
                }
            }

            if (metadata.contains("exif") && metadata["exif"].is_object())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (ImGui::TreeNodeEx("EXIF", ImGuiTreeNodeFlags_SpanAllColumns))
                {
                    for (auto &exif_entry : metadata["exif"].items())
                    {
                        const auto &table_obj = exif_entry.value();
                        if (!table_obj.is_object())
                            continue;

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        if (ImGui::TreeNodeEx(exif_entry.key().c_str(),
                                              ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            for (auto &field : table_obj.items())
                            {
                                const std::string &key       = field.key();
                                const auto        &field_obj = field.value();
                                if (!field_obj.is_object() || !field_obj.contains("string"))
                                    continue;

                                auto value  = field_obj["string"].get<std::string>();
                                auto concat = key + " " + value;
                                if (!filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                                    continue;

                                ImGui::WrappedTextProperty(key, value, "", bold_font);
                            }
                            ImGui::TreePop();
                        }
                    }

                    ImGui::TreePop();
                }
            }

            ImGui::TreePop();
        }

        PE::end();
    }
}

void Image::draw_colorspace()
{
    auto bold_font = hdrview()->font("sans bold");

    namespace PE = nvgui::PropertyEditor;

    static const ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg;
    if (PE::begin("Colorspace", table_flags))
    {
        ImGui::WrappedTextProperty("Transfer function", metadata.value<string>("transfer function", "linear"),
                                   "The transfer function applied at load time to make the values linear.", bold_font);

        PE::entry("Color gamut",
                  [&]
                  {
                      bool modified   = false;
                      auto csn        = color_gamut_names();
                      auto open_combo = ImGui::BeginCombo("##Color gamut", color_gamut_name((ColorGamut)color_space),
                                                          ImGuiComboFlags_HeightLargest);
                      if (open_combo)
                      {
                          for (ColorGamut_ n = ColorGamut_FirstNamed; n <= ColorGamut_LastNamed; ++n)
                          {
                              auto       cg          = (ColorGamut)n;
                              const bool is_selected = (color_space == n);
                              if (ImGui::Selectable(csn[n], is_selected))
                              {
                                  color_space = cg;
                                  spdlog::debug("Switching to color space {}.", n);
                                  chromaticities = ::gamut_chromaticities(cg);
                                  compute_color_transform();
                                  modified = true;
                              }

                              // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                              if (is_selected)
                                  ImGui::SetItemDefaultFocus();
                          }
                          ImGui::EndCombo();
                      }
                      return modified;
                  });
        ImGui::WrappedTooltip(
            "Interpret the values stored in the file using the chromaticities of a common color profile.");

        PE::entry("White point",
                  [&]
                  {
                      bool modified = false;
                      auto wpn      = white_point_names();
                      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x < HelloImGui::EmSize(8.f)
                                                  ? HelloImGui::EmSize(8.f)
                                                  : -FLT_MIN); // use the full width of the column
                      auto open_combo = ImGui::BeginCombo("##White point", white_point_name(white_point),
                                                          ImGuiComboFlags_HeightLargest);
                      if (open_combo)
                      {
                          for (WhitePoint_ n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                          {
                              auto       wp          = (WhitePoint)n;
                              const bool is_selected = (white_point == n);
                              if (ImGui::Selectable(wpn[n], is_selected))
                              {
                                  white_point = wp;
                                  spdlog::debug("Switching to white point {}.", n);
                                  if (!chromaticities)
                                      chromaticities = Chromaticities{};
                                  chromaticities->white = ::white_point(wp);
                                  compute_color_transform();
                              }

                              // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                              if (is_selected)
                                  ImGui::SetItemDefaultFocus();
                          }
                          ImGui::EndCombo();
                      }
                      return modified;
                  });

        {
            const Chromaticities chr_orig{chromaticities.value_or(Chromaticities{})};
            Chromaticities       chr{chr_orig};
            bool                 edited = false;

            edited |= PE::SliderFloat2("Red", &chr.red.x, 0.f, 1.f, "%.4f");
            edited |= PE::SliderFloat2("Green", &chr.green.x, 0.f, 1.f, "%.4f");
            edited |= PE::SliderFloat2("Blue", &chr.blue.x, 0.f, 1.f, "%.4f");

            if (chr_orig != chr || edited)
            {
                spdlog::debug("Setting chromaticities to ({}, {}), ({}, {}), ({}, {}), ({}, {})", chr.red.x, chr.red.y,
                              chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x, chr.white.y);
                chromaticities = chr;
                compute_color_transform();
            }
        }

        {
            Chromaticities chr{chromaticities.value_or(Chromaticities{})};
            float2         wp = chr.white;

            if (PE::SliderFloat2("White point", &wp.x, 0.f, 1.f, "%.4f") || wp != chr.white)
            {
                chr.white = wp;
                spdlog::info("Setting chromaticities to ({}, {}), ({}, {}), ({}, {}), ({}, {})", chr.red.x, chr.red.y,
                             chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x, chr.white.y);
                chromaticities = chr;
                compute_color_transform();
            }

            PE::entry(
                "Adopted neutral",
                [&]
                {
                    bool has_an = adopted_neutral.has_value();
                    if (ImGui::Checkbox("##hidden", &has_an))
                    {
                        if (has_an)
                            adopted_neutral = wp;
                        else
                            adopted_neutral.reset();
                        compute_color_transform();
                    }

                    ImGui::SetNextItemWidth(-FLT_MIN);

                    if (has_an && ImGui::SliderFloat2("##hidden", &adopted_neutral->x, 0.f, 1.f, "%.4f"))
                        compute_color_transform();

                    return false;
                },
                "Specifies the CIE (x,y) coordinates that should be considered neutral during "
                "color rendering. Pixels in the image file whose (x,y) coordinates match the "
                "adoptedNeutral value should be mapped to neutral values on the display.");
        }

        PE::entry("Adaptation",
                  [&]
                  {
                      const char *wan[] = {"None", "XYZ scaling", "Bradford", "Von Kries", nullptr};

                      bool modified   = false;
                      auto open_combo = ImGui::BeginCombo("##Adaptation",
                                                          adaptation_method <= AdaptationMethod_Identity ||
                                                                  adaptation_method >= AdaptationMethod_Count
                                                              ? "None"
                                                              : wan[adaptation_method],
                                                          ImGuiComboFlags_HeightLargest);
                      ImGui::WrappedTooltip("Method for chromatic adaptation transform.");
                      if (open_combo)
                      {
                          for (AdaptationMethod_ n = 0; wan[n]; ++n)
                          {
                              auto       am          = (AdaptationMethod)n;
                              const bool is_selected = (adaptation_method == am);
                              if (ImGui::Selectable(wan[n], is_selected))
                              {
                                  adaptation_method = am;
                                  spdlog::debug("Switching to adaptation method {}.", n);
                                  compute_color_transform();
                              }

                              // Set the initial focus when opening the combo (scrolling + keyboard navigation
                              // focus)
                              if (is_selected)
                                  ImGui::SetItemDefaultFocus();
                          }
                          ImGui::EndCombo();
                      }
                      return modified;
                  });

        PE::InputFloat3("Yw", &luminance_weights.x, "%+8.2e",
                        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly,
                        "Channel weights to compute the luminance (Y) of a pixel.");

        {
            PE::entry(
                "Color matrix",
                [&]
                {
                    bool modified = false;
                    modified |= ImGui::InputFloat3("##M0", &M_to_sRGB[0][0], "%+8.2e",
                                                   ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                    // ImGui::NewLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    modified |= ImGui::InputFloat3("##M1", &M_to_sRGB[1][0], "%+8.2e",
                                                   ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                    // ImGui::NewLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    modified |= ImGui::InputFloat3("##M2", &M_to_sRGB[2][0], "%+8.2e",
                                                   ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                    return modified;
                });
        }

        PE::end();
    }

    {
        static float2 vMin{-0.05f, -0.05f};
        static float2 vMax{0.75f, 0.9f};
        static float2 vSize  = vMax - vMin;
        static float  aspect = vSize.x / vSize.y;

        // property_name("Diagram");
        // ImGui::SameLine(label_size);
        // ImGui::Indent();
        float const size = ImMax(ImGui::GetContentRegionAvail().x, HelloImGui::EmSize(8.f));

        ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase);

        float4 plot_bg{0.35f, 0.35f, 0.35f, 1.f};
        ImGui::PushStyleColor(ImGuiCol_WindowBg, plot_bg);
        if (ImPlot::BeginPlot("##Chromaticity diagram", ImVec2(size, size / aspect * 0.95f),
                              ImPlotFlags_Crosshairs | ImPlotFlags_Equal | ImPlotFlags_NoLegend | ImPlotFlags_NoTitle))
        {
            static constexpr float lambda_min   = 400.f;
            static constexpr float lambda_max   = 680.f;
            static constexpr int   sample_count = 200;

            auto text_color_f  = float4{0.f, 0.f, 0.f, 1.f};
            auto text_color_fc = contrasting_color(text_color_f);

            ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImGui::GetColorU32(text_color_fc));

            ImPlot::GetInputMap().ZoomRate = 0.03f;
            ImPlot::SetupAxis(ImAxis_X1, "x", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_Foreground);
            ImPlot::SetupAxis(ImAxis_Y1, "y", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_Foreground);
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            ImPlot::SetupAxesLimits(vMin.x, vMax.x, vMin.y, vMax.y);
            ImPlot::SetupMouseText(ImPlotLocation_NorthEast, ImPlotMouseTextFlags_NoFormat);

            ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
            ImPlot::SetupFinish();
            ImGui::PopFont();

            ImPlot::PushPlotClipRect();

            //
            // plot background texture
            //
            ImPlot::PlotImage("##chromaticity_image", (ImTextureRef)chromaticity_texture()->texture_handle(),
                              ImPlotPoint(0.0, 0.0), ImPlotPoint(.73f, .83f), {0.f, .83f}, {.73f, 0.f});

            auto normal_to_plot_tangent = [](const float2 &tangent, float pixel_length) -> float2
            {
                float2 p0            = ImPlot::PlotToPixels(0, 0);
                float2 tangent_px    = float2(ImPlot::PlotToPixels(tangent.x, tangent.y)) - p0;
                float2 normal_px     = pixel_length * normalize(float2{-tangent_px.y, tangent_px.x});
                auto   plot_tick_end = ImPlot::PixelsToPlot(p0 + normal_px);
                return float2(plot_tick_end.x, plot_tick_end.y);
            };

            //
            // draw the spectral locus
            //
            float pixels_per_texel     = 1.f;
            float pixels_per_plot_unit = 1.f;
            float scale_factor         = 1.f;
            {
                float2 plot_size = ImPlot::GetPlotSize();
                auto   plot_rect = ImPlot::GetPlotLimits();
                pixels_per_plot_unit =
                    length(plot_size / float2(plot_rect.X.Max - plot_rect.X.Min, plot_rect.Y.Max - plot_rect.Y.Min));
                // compute width in pixels of a chromaticity texture texel
                pixels_per_texel = 1.f / 256.f * pixels_per_plot_unit;
                scale_factor     = std::clamp(pixels_per_texel * 1.2f, 1.f, 4.f);

                // ImPlot::PlotLine draws ugly, unrounded, line segments, so we use AddPolyline ourselves
                ImVector<float2> poly;
                poly.resize(sample_count);
                // Iterate over all entries in poly and map them to pixel coordinates
                for (int i = 0; i < poly.Size; ++i)
                {
                    float wavelength = lerp(lambda_min, lambda_max, ((float)i) / ((float)(sample_count - 1)));
                    auto  pos        = wavelength_to_xy(wavelength);
                    poly[i]          = ImPlot::PlotToPixels(pos.x, pos.y);
                }

                ImPlot::GetPlotDrawList()->AddPolyline((ImVec2 *)&poly[0], poly.Size, ImGui::GetColorU32(text_color_f),
                                                       ImDrawFlags_Closed, std::max(1.f, 1.2f * pixels_per_texel));
            }

            //
            // draw wavelength tick marks
            //
            {
                ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
                // Draw tick marks stored in tick_marks
                const float minor_tick_pixel_length = std::max(1.f, 2.f * pixels_per_texel);
                const float major_tick_pixel_length = std::max(1.f, 3.f * pixels_per_texel);

                static const float first_tick = (ImFloor(lambda_min / 10.f)) * 10.f;
                int                tick_count = (int)ImFloor((lambda_max - lambda_min) / 10.0f) + 1;
                for (int i = 0; i < tick_count; ++i)
                {
                    // Compute wavelength for this tick
                    float nm = first_tick + i * 10.0f;
                    // Check if tick is at a 100 nm multiple
                    bool is_major = (static_cast<int>(nm) % 100 == 0);

                    float lambda = first_tick + i * 10.0f;

                    // Compute chromaticity at this wavelength
                    float2 pos     = wavelength_to_xy(lambda);
                    float2 tangent = wavelength_to_xy(lambda + 1) - wavelength_to_xy(lambda - 1);
                    float2 normal =
                        -normal_to_plot_tangent(tangent, is_major ? major_tick_pixel_length : minor_tick_pixel_length);

                    // Tick mark parameters
                    float2 tick[2] = {pos, pos + normal};

                    ImPlot::SetNextMarkerStyle(ImPlotMarker_None);
                    ImPlot::SetNextLineStyle({0.f, 0.f, 0.f, 1.f}, 0.5f * scale_factor);
                    ImPlot::PlotLine("##CCT_tick", &tick[0].x, &tick[0].y, 2, 0, 0, sizeof(float2));

                    // Add text label for major ticks (100 nm multiples)
                    if (is_major)
                    {
                        static char label[8];
                        ImFormatString(label, sizeof(label), "%d nm", static_cast<int>(nm));

                        float4 bg = contrasting_color(contrasting_color(plot_bg));
                        bg.w      = 0.5f;

                        ImPlot::Annotation(tick[1].x, tick[1].y, bg, float2{1.f, -1.f} * round(normalize(normal)),
                                           false, "%s", label);
                    }
                }
                ImGui::PopFont();
            }

            //
            // draw the locus of D (daylight) CCTs
            //
            {
                auto create_CCT_locus = []()
                {
                    ImVector<float2> poly;
                    poly.resize(sample_count);

                    for (int i = 0; i < sample_count; ++i)
                    {
                        float T = lerp(1668.f, 25000.f, ((float)i) / ((float)(sample_count - 1)));
                        poly[i] = Kelvin_to_xy(T);
                    }

                    return poly;
                };
                const static ImVector<float2> cct_locus = create_CCT_locus();

                // ImPlot::PlotLine draws ugly, unrounded, line segments, so we use AddPolyline ourselves
                ImVector<float2> poly = cct_locus;
                // Iterate over all entries in poly and map them to pixel coordinates
                for (int i = 0; i < poly.Size; ++i) poly[i] = ImPlot::PlotToPixels({poly[i].x, poly[i].y});

                ImPlot::GetPlotDrawList()->AddPolyline((ImVec2 *)&poly[0], poly.Size, ImGui::GetColorU32(text_color_f),
                                                       ImDrawFlags_None, scale_factor);

                const float label_font_scale = 1.0f;
                const float scale            = 1.f;
                ImGui::PushFont(hdrview()->font("sans regular"),
                                ImGui::GetStyle().FontSizeBase * label_font_scale * scale / 2.f);

                constexpr int temp_step = 1000;
                for (int temp = 2000; temp <= 25000; temp += temp_step)
                {
                    float2 xy = Kelvin_to_xy((float)temp);
                    char   label[8];
                    snprintf(label, sizeof(label), "%dK", temp);
                    float2 text_size = ImGui::CalcTextSize(label);

                    // Compute tangent and normal
                    float2 tangent = normalize(Kelvin_to_xy((float)(temp - 1)) - Kelvin_to_xy((float)(temp + 1)));
                    float2 normal  = normal_to_plot_tangent(tangent, scale_factor * 2.f);

                    // Tick mark parameters
                    float2 tick[2] = {xy, xy + normal};

                    // Only draw this tick if it doesn't overlap with the previous tick
                    static float2 prev_tick_end = {-100000.f,
                                                   -100000.f}; // large negative to ensure first tick is drawn
                    const float   min_dist      = 5.0f;        // minimum pixel distance between ticks

                    float2 tick_end_px      = ImPlot::PlotToPixels(ImPlotPoint(tick[1].x, tick[1].y));
                    float2 prev_tick_end_px = ImPlot::PlotToPixels(ImPlotPoint(prev_tick_end.x, prev_tick_end.y));
                    bool   draw             = length(tick_end_px - prev_tick_end_px) > min_dist &&
                                (2.f * text_size.y < abs(tick_end_px.y - prev_tick_end_px.y) ||
                                 1.5f * text_size.x < abs(tick_end_px.x - prev_tick_end_px.x));

                    if (draw)
                    {
                        ImPlot::SetNextMarkerStyle(ImPlotMarker_None);
                        ImPlot::SetNextLineStyle(text_color_f, 0.5f * scale_factor);
                        ImPlot::PlotLine("##CCT_tick", &tick[0].x, &tick[0].y, 2, 0, 0, sizeof(float2));
                        prev_tick_end = tick[1];

                        ImPlot::Annotation(tick[1].x, tick[1].y, ImVec4(1, 1, 1, 0.5), ImVec2(1, 1), false, "%s",
                                           label);
                    }
                }

                ImGui::PopFont();
            }

            //
            // draw the primaries, gamut triangle, whitepoint, and text labels
            //
            {
                Chromaticities gamut_chr{chromaticities.value_or(Chromaticities{})};
                ImVec4         colors[] = {
                    {0.8f, 0.f, 0.f, 1.f}, {0.f, 0.8f, 0.f, 1.f}, {0.f, 0.f, 0.8f, 1.f}, {0.5f, 0.5f, 0.5f, 1.f}};
                const char *names[]     = {"R", "G", "B", "W"};
                double2     primaries[] = {double2(gamut_chr.red), double2(gamut_chr.green), double2(gamut_chr.blue),
                                           double2(gamut_chr.red)};
                static bool clicked[4]  = {false, false, false, false};
                static bool hovered[4]  = {false, false, false, false};
                static bool held[4]     = {false, false, false, false};

                primaries[3] = double2(gamut_chr.red);

                ImPlot::SetNextMarkerStyle(ImPlotMarker_None);
                ImPlot::SetNextLineStyle(text_color_fc, scale_factor);
                ImPlot::PlotLine("##gamut_triangle", &primaries[0].x, &primaries[0].y, 4, ImPlotLineFlags_None, 0,
                                 sizeof(double2));

                primaries[3] = double2(gamut_chr.white);

                // ImPlot::PlotScatter draws ugly, unrounded, circles, so we use AddPolyline ourselves
                // Iterate over all entries in poly and map them to pixel coordinates
                std::array<ImVec2, 4> poly = {ImPlot::PlotToPixels(ImPlotPoint(primaries[0].x, primaries[0].y)),
                                              ImPlot::PlotToPixels(ImPlotPoint(primaries[1].x, primaries[1].y)),
                                              ImPlot::PlotToPixels(ImPlotPoint(primaries[2].x, primaries[2].y)),
                                              ImPlot::PlotToPixels(ImPlotPoint(primaries[3].x, primaries[3].y))};
                for (int i = 0; i < 4; ++i)
                    ImPlot::GetPlotDrawList()->AddCircleFilled(
                        poly[i], 2.5f * scale_factor,
                        ImGui::GetColorU32(clicked[i] || hovered[i] || held[i] ? text_color_f : text_color_fc), 0);

                // ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.f);
                // ImPlot::SetNextLineStyle(ImVec4{0.f, 0.f, 0.f, 1.0f});
                // ImPlot::PlotScatter("##white_point", &primaries[3].x, &primaries[3].y, 1,
                // ImPlotScatterFlags_None,
                //                     0, sizeof(double2));

                ImGui::PushFont(hdrview()->font("sans bold"), ImGui::GetStyle().FontSizeBase * scale_factor / 2.f);
                for (int i = 0; i < 4; ++i)
                {
                    if (ImPlot::DragPoint(i, &primaries[i].x, &primaries[i].y, colors[i], 1.5f * scale_factor,
                                          ImPlotDragToolFlags_Delayed, &clicked[i], &hovered[i], &held[i]))
                    {
                        gamut_chr.red   = float2(primaries[0].x, primaries[0].y);
                        gamut_chr.green = float2(primaries[1].x, primaries[1].y);
                        gamut_chr.blue  = float2(primaries[2].x, primaries[2].y);
                        gamut_chr.white = float2(primaries[3].x, primaries[3].y);
                        chromaticities  = gamut_chr;
                        compute_color_transform();
                    }

                    // draw text label shadow
                    ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_f));
                    float2 offset{4.f * scale_factor, -4.f * scale_factor};
                    ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                    ImPlot::PopStyleColor();

                    // draw text label
                    ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_fc));
                    offset -= 1.f;
                    ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                    ImPlot::PopStyleColor();

                    // ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_f));
                    // ImVec2 offset{4.f * scale, -4.f * scale};
                    // ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                    // ImPlot::PopStyleColor();
                }

                ImGui::PopFont();
            }

            //
            // draw the hovered pixel in the chromaticity diagram
            //
            {
                auto &io      = ImGui::GetIO();
                auto  rgb2xyz = mul(M_RGB_to_XYZ, inverse(M_to_sRGB));
                ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4{0.f, 0.f, 0.f, 1.0f});
                ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2.f);
                if (hdrview()->vp_pos_in_viewport(hdrview()->vp_pos_at_app_pos(io.MousePos)))
                {
                    auto   hovered_pixel = int2{hdrview()->pixel_at_app_pos(io.MousePos)};
                    float4 color32       = hdrview()->pixel_value(hovered_pixel, false, 0);

                    float3 XYZ = mul(rgb2xyz, color32.xyz());
                    float2 xy  = XYZ.xy() / (XYZ.x + XYZ.y + XYZ.z);

                    ImPlot::PlotScatter("##HoveredPixel", &xy.x, &xy.y, 1);
                }
                ImPlot::PopStyleColor();
                ImPlot::PopStyleVar();
            }
            ImPlot::PopPlotClipRect();

            ImPlot::PopStyleColor();

            ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
            ImPlot::EndPlot();
            ImGui::PopFont();
        }
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
}

void Image::draw_channel_stats()
{
    auto bold_font = hdrview()->font("sans bold");
    auto mono_font = hdrview()->font("mono regular");

    static const ImGuiTableFlags table_flags =
        ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH | ImGuiTableFlags_RowBg;

    static int value_mode = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##Value mode", &value_mode, "Raw values\0Exposure-adjusted\0\0");
    float gain = value_mode == 0 ? 1.f : pow(2.f, hdrview()->exposure_live());

    auto &group = groups[selected_group];
    // Set the hover and active colors to be the same as the background color
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
    if (ImGui::BeginTable("Channel statistics", group.num_channels + 1, table_flags))
    {
        PixelStats *channel_stats[4] = {nullptr, nullptr, nullptr, nullptr};
        string      channel_names[4];
        for (int c = 0; c < group.num_channels; ++c)
        {
            auto &channel = channels[group.channels[c]];
            channel.update_stats(c, hdrview()->current_image(), hdrview()->reference_image());
            channel_stats[c] = channel.get_stats();
            channel_names[c] = Channel::tail(channel.name);
        }

        // set up header row
        ImGui::PushFont(bold_font, ImGui::GetStyle().FontSizeBase);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int c = 0; c < group.num_channels; ++c)
            ImGui::TableSetupColumn(fmt::format("{}{}", ICON_MY_CHANNEL_GROUP, channel_names[c]).c_str(),
                                    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();
        ImGui::PopFont();

        const char   *stat_names[] = {"Minimum", "Average", "Maximum", "# of NaNs", "# of Infs"}; //, "# valid pixels"};
        constexpr int NUM_STATS    = sizeof(stat_names) / sizeof(stat_names[0]);
        for (int s = 0; s < NUM_STATS; ++s)
        {
            ImGui::PushFont(bold_font, ImGui::GetStyle().FontSizeBase);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
            ImGui::TextUnformatted(stat_names[s]);
            ImGui::PopFont();
            ImGui::PushFont(mono_font, ImGui::GetStyle().FontSizeBase);
            for (int c = 0; c < group.num_channels; ++c)
            {
                ImGui::TableNextColumn();
                switch (s)
                {
                case 0: ImGui::TextFmt("{:f}", channel_stats[c]->summary.minimum * gain); break;
                case 1: ImGui::TextFmt("{:f}", channel_stats[c]->summary.average * gain); break;
                case 2: ImGui::TextFmt("{:f}", channel_stats[c]->summary.maximum * gain); break;
                case 3: ImGui::TextFmt("{: > 6d}", channel_stats[c]->summary.nan_pixels); break;
                case 4:
                default:
                    ImGui::TextFmt("{: > 6d}", channel_stats[c]->summary.inf_pixels);
                    break;
                    // case 5:
                    // default: ImGui::TextFmt("{:d}", channel_stats[c]->summary.valid_pixels); break;
                }
            }
            ImGui::PopFont();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
}
