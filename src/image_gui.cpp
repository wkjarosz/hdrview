//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "fwd.h"

#include "dear_widgets.h"

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

    ImGui::PushFont(hdrview()->font("sans regular"), 10);
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

void Image::draw_info()
{
    auto sans_font = hdrview()->font("sans regular");
    auto bold_font = hdrview()->font("sans bold");
    auto mono_font = hdrview()->font("mono regular");
    auto mono_bold = hdrview()->font("mono bold");

    float label_size = HelloImGui::EmSize(9.f);
    float min_w      = HelloImGui::EmSize(8.f);

    auto property_name = [sans_font, &label_size](const string &text)
    {
        ImGui::PushFont(sans_font, 14);
        ImGui::PushTextWrapPos((label_size - HelloImGui::EmSize(0.25f)));
        float text_w = ImGui::CalcTextSize(text.c_str()).x;
        auto  shift  = 1.0f * (1.f * (label_size - HelloImGui::EmSize(1.f)) - text_w);
        if (shift > 0.f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    };
    auto property_value = [&label_size, min_w](const string &text, ImFont *font, bool wrapped = false)
    {
        ImGui::SameLine(label_size);
        ImGui::PushFont(font, 14);

        if (wrapped)
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + max(min_w, ImGui::GetContentRegionAvail().x));

        ImGui::TextUnformatted(text);

        if (wrapped)
            ImGui::PopTextWrapPos();

        ImGui::PopFont();
    };

    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
    {
        property_name("File name");
        property_value(filename, bold_font, true);

        property_name("Part name");
        property_value(partname.empty() ? "<none>" : partname, bold_font, true);

        property_name("Loader");
        property_value(metadata.value<string>("loader", "unknown"), bold_font, true);

        property_name("File's bit depth");
        property_value(metadata.value<string>("bit depth", "unknown"), bold_font, true);

        property_name("Resolution");
        property_value(fmt::format("{} {} {}", size().x, ICON_MY_TIMES, size().y), bold_font);

        property_name("Data window");
        property_value(fmt::format("[{}, {}) {} [{}, {})", data_window.min.x, data_window.max.x, ICON_MY_TIMES,
                                   data_window.min.y, data_window.max.y),
                       bold_font);

        property_name("Display window");
        property_value(fmt::format("[{}, {}) {} [{}, {})", display_window.min.x, display_window.max.x, ICON_MY_TIMES,
                                   display_window.min.y, display_window.max.y),
                       bold_font);

        property_name("Straight alpha");
        property_value(fmt::format("{}", file_has_straight_alpha), bold_font);
    }

    if (ImGui::CollapsingHeader("Color space", ImGuiTreeNodeFlags_DefaultOpen))
    {
        property_name("Transfer function");
        ImGui::WrappedTooltip("The transfer function applied at load time to make the values linear.");
        property_value(metadata.value<string>("transfer function", "linear"), bold_font, true);

        {
            property_name("Color gamut");

            ImGui::SameLine(label_size);

            ImGui::PushFont(bold_font, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
            auto csn = color_gamut_names();
            ImGui::SetNextItemWidth(
                ImGui::GetContentRegionAvail().x < min_w ? min_w : -FLT_MIN); // use the full width of the column
            auto open_combo = ImGui::BeginCombo("##Color gamut", color_gamut_name((ColorGamut)color_space),
                                                ImGuiComboFlags_HeightLargest);
            ImGui::WrappedTooltip(
                "Interpret the values stored in the file using the chromaticities of a common color profile.");
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
                    }

                    // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
            ImGui::PopFont();

            property_name("White point");

            ImGui::SameLine(label_size);

            ImGui::PushFont(bold_font, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
            auto wpn = white_point_names();
            ImGui::SetNextItemWidth(
                ImGui::GetContentRegionAvail().x < min_w ? min_w : -FLT_MIN); // use the full width of the column
            open_combo =
                ImGui::BeginCombo("##White point", white_point_name(white_point), ImGuiComboFlags_HeightLargest);
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
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }

        {
            ImDrawList *pDrawList = ImGui::GetWindowDrawList();

            Chromaticities gamut_chr{chromaticities.value_or(Chromaticities{})};
            // our working space is always BT.709/sRGB
            auto rgb2xyz = transpose(mul(M_RGB_to_XYZ, inverse(M_to_sRGB)));
            auto xyz2rgb = transpose(XYZ_to_RGB(Chromaticities{}, 1.f));
            // this is equivalent to:
            // auto           xyz2rgb = transpose(mul(M_to_sRGB, M_XYZ_to_RGB));

            float         pad = 0.01f;
            static float2 vMin{0.f, 0.f};
            static float2 vMax{0.8f, 0.9f};
            static float2 vSize  = vMax - vMin;
            static float  aspect = vSize.x / vSize.y;

            property_name("Diagram");
            ImGui::SameLine(label_size);
            float const size = ImMax(ImGui::GetContentRegionAvail().x, min_w);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImWidgets::DrawChromaticityPlot(
                pDrawList, pos, ImVec2(size, size / aspect), gamut_chr.red, gamut_chr.green, gamut_chr.blue,
                gamut_chr.white, &xyz2rgb.x.x, 200, 64, 64, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.5f), 380.f, 700.f,
                vMin - pad, vMax + pad, vMin, vMax, true, true, true, true, IM_COL32(0, 0, 0, 255), 2.f);

            auto &io = ImGui::GetIO();
            if (hdrview()->vp_pos_in_viewport(hdrview()->vp_pos_at_app_pos(io.MousePos)))
            {
                auto   hovered_pixel = int2{hdrview()->pixel_at_app_pos(io.MousePos)};
                float4 color32       = hdrview()->pixel_value(hovered_pixel, false, 0);

                ImWidgets::DrawChromaticityPointsGeneric(pDrawList, pos, ImVec2(size, size / aspect), &rgb2xyz.x.x,
                                                         &color32.x, 1, vMin.x, vMax.x, vMin.y, vMax.y,
                                                         IM_COL32(0, 0, 0, 255), 2.f, 4);
            }

            if (ImWidgets::ChromaticityPlotDragBehavior("##chromaticityDrag", pos, ImVec2(size, size / aspect),
                                                        (ImVec2 *)&gamut_chr.red, (ImVec2 *)&gamut_chr.green,
                                                        (ImVec2 *)&gamut_chr.blue, (ImVec2 *)&gamut_chr.white,
                                                        {0.0f - pad, 0.0f - pad}, {0.8f + pad, 0.9f + pad}))
            {
                chromaticities = gamut_chr;
                compute_color_transform();
            }
        }

        {
            const Chromaticities chr_orig{chromaticities.value_or(Chromaticities{})};
            Chromaticities       chr{chr_orig};
            bool                 edited = false;

            ImGui::PushFont(mono_bold, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));

            property_name("Red");
            ImGui::SameLine(label_size);

            float w = ImGui::GetContentRegionAvail().x < min_w ? min_w : -FLT_MIN;
            ImGui::SetNextItemWidth(w);
            edited |= ImGui::SliderFloat2("##Red", &chr.red.x, 0.f, 1.f, "%.4f");

            property_name("Green");
            ImGui::SameLine(label_size);
            ImGui::SetNextItemWidth(w);
            edited |= ImGui::SliderFloat2("##Green", &chr.green.x, 0.f, 1.f, "%.4f");

            property_name("Blue");
            ImGui::SameLine(label_size);
            ImGui::SetNextItemWidth(w);
            edited |= ImGui::SliderFloat2("##Blue", &chr.blue.x, 0.f, 1.f, "%.4f");

            ImGui::PopStyleVar();
            ImGui::PopFont();

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
            float2         wp     = chr.white;
            bool           edited = false;

            ImGui::PushFont(mono_bold, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));

            property_name("White");
            ImGui::SameLine(label_size);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x < min_w ? min_w : -FLT_MIN);
            edited |= ImGui::SliderFloat2("##White", &wp.x, 0.f, 1.f, "%.4f");

            ImGui::PopStyleVar();
            ImGui::PopFont();

            if (edited || wp != chr.white)
            {
                chr.white = wp;
                spdlog::info("Setting chromaticities to ({}, {}), ({}, {}), ({}, {}), ({}, {})", chr.red.x, chr.red.y,
                             chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x, chr.white.y);
                chromaticities = chr;
                compute_color_transform();
            }
        }

        if (adopted_neutral)
        {
            property_name("Adopted neutral");
            property_value(fmt::format("({}, {})", adopted_neutral->x, adopted_neutral->y), mono_font);
        }

        {
            // ImGui::AlignTextToFramePadding();
            property_name("Adaptation");

            ImGui::SameLine(label_size);

            ImGui::PushFont(bold_font, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));

            const char *wan[] = {"None", "XYZ scaling", "Bradford", "Von Kries", nullptr};
            ImGui::SetNextItemWidth(
                ImGui::GetContentRegionAvail().x < min_w ? min_w : -FLT_MIN); // use the full width of the column
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

                    // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }

        {
            property_name("Luminance weights");
            // property_value(fmt::format("{:+8.2e}, {:+8.2e}, {:+8.2e}", luminance_weights.x, luminance_weights.y,
            //                            luminance_weights.z),
            //                mono_font);

            ImGui::PushFont(mono_bold, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));

            ImGui::SameLine(label_size);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x < min_w * 2.f ? min_w * 2.f : -FLT_MIN);
            ImGui::InputFloat3("##Yw", &luminance_weights.x, "%+8.2e",
                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);

            ImGui::PopStyleVar();
            ImGui::PopFont();
        }

        {
            property_name("Color matrix");
            // property_value(fmt::format("{:+8.2e}, {:+8.2e}, {:+8.2e}\n"
            //                            "{:+8.2e}, {:+8.2e}, {:+8.2e}\n"
            //                            "{:+8.2e}, {:+8.2e}, {:+8.2e}",
            //                            M_to_sRGB[0][0], M_to_sRGB[0][1], M_to_sRGB[0][2], // prevent wrap
            //                            M_to_sRGB[1][0], M_to_sRGB[1][1], M_to_sRGB[1][2], //
            //                            M_to_sRGB[2][0], M_to_sRGB[2][1], M_to_sRGB[2][2]),
            //                mono_font);

            ImGui::PushFont(mono_bold, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));

            ImGui::SameLine(label_size);
            float w = ImGui::GetContentRegionAvail().x < min_w * 2.f ? min_w * 2.f : -FLT_MIN;
            ImGui::SetNextItemWidth(w);
            ImGui::InputFloat3("##M0", &M_to_sRGB[0][0], "%+8.2e",
                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);

            ImGui::NewLine();
            ImGui::SameLine(label_size);
            ImGui::SetNextItemWidth(w);
            ImGui::InputFloat3("##M1", &M_to_sRGB[1][0], "%+8.2e",
                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);

            ImGui::NewLine();
            ImGui::SameLine(label_size);
            ImGui::SetNextItemWidth(w);
            ImGui::InputFloat3("##M2", &M_to_sRGB[2][0], "%+8.2e",
                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);

            ImGui::PopStyleVar();
            ImGui::PopFont();
        }
    }

    if (metadata.contains("exr header") && metadata["exr header"].is_object())
    {
        if (ImGui::CollapsingHeader("EXR header", ImGuiTreeNodeFlags_Framed))
        {
            static ImGuiTextFilter exr_filter;
            const ImVec2           button_size   = ImGui::IconButtonSize();
            bool                   filter_active = exr_filter.IsActive(); // save here to avoid flicker

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InputTextWithHint(
                    "##exif filter",
                    ICON_MY_FILTER
                    "Filter (format: [include|-exclude][,...]; e.g. \"include_this,-but_not_this,also_include_this\")",
                    exr_filter.InputBuf, IM_ARRAYSIZE(exr_filter.InputBuf)))
                exr_filter.Build();
            if (filter_active)
            {
                ImGui::SameLine(0.f, 0.f);

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
                if (ImGui::IconButton(ICON_MY_DELETE))
                    exr_filter.Clear();
            }

            // calculate the label size based on the longest key
            size_t max_w = 0;
            for (auto &field : metadata["exr header"].items()) max_w = std::max(field.key().length(), max_w);

            label_size = HelloImGui::EmSize(0.55f) * (float)max_w;

            for (auto &field : metadata["exr header"].items())
            {
                const std::string &key       = field.key();
                const auto        &field_obj = field.value();
                if (!field_obj.is_object() || !field_obj.contains("string"))
                    continue;

                auto value  = field_obj["string"].get<std::string>();
                auto concat = key + " " + value;
                if (!exr_filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                    continue;

                property_name(key);
                property_value(value, bold_font);
            }
        }
    }

    if (metadata.contains("exif") && metadata["exif"].is_object())
    {
        if (ImGui::CollapsingHeader("EXIF metadata", ImGuiTreeNodeFlags_Framed))
        {
            static ImGuiTextFilter exif_filter;
            const ImVec2           button_size   = ImGui::IconButtonSize();
            bool                   filter_active = exif_filter.IsActive(); // save here to avoid flicker

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InputTextWithHint(
                    "##exif filter",
                    ICON_MY_FILTER
                    "Filter (format: [include|-exclude][,...]; e.g. \"include_this,-but_not_this,also_include_this\")",
                    exif_filter.InputBuf, IM_ARRAYSIZE(exif_filter.InputBuf)))
                exif_filter.Build();
            if (filter_active)
            {
                ImGui::SameLine(0.f, 0.f);

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
                if (ImGui::IconButton(ICON_MY_DELETE))
                    exif_filter.Clear();
            }

            // calculate the label size based on the longest key
            size_t max_w = 0;
            for (auto &exif_entry : metadata["exif"].items())
            {
                const auto &table_obj = exif_entry.value();
                if (!table_obj.is_object())
                    continue;

                for (auto &field : table_obj.items()) max_w = std::max(field.key().length(), max_w);
            }
            label_size = HelloImGui::EmSize(0.55f) * (float)max_w;

            for (auto &exif_entry : metadata["exif"].items())
            {
                const auto &table_obj = exif_entry.value();
                if (!table_obj.is_object())
                    continue;

                ImGui::SeparatorText(exif_entry.key().c_str());

                for (auto &field : table_obj.items())
                {
                    const std::string &key       = field.key();
                    const auto        &field_obj = field.value();
                    if (!field_obj.is_object() || !field_obj.contains("string"))
                        continue;

                    auto value  = field_obj["string"].get<std::string>();
                    auto concat = key + " " + value;
                    if (!exif_filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                        continue;

                    property_name(key);
                    property_value(value, bold_font);
                }
            }
        }
    }
}

void Image::draw_channel_stats()
{
    auto bold_font = hdrview()->font("sans bold");
    auto mono_font = hdrview()->font("mono regular");

    static const ImGuiTableFlags table_flags =
        ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH | ImGuiTableFlags_RowBg;

    {
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
            ImGui::PushFont(bold_font, 14);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            for (int c = 0; c < group.num_channels; ++c)
                ImGui::TableSetupColumn(fmt::format("{}{}", ICON_MY_CHANNEL_GROUP, channel_names[c]).c_str(),
                                        ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableHeadersRow();
            ImGui::PopFont();

            const char   *stat_names[] = {"Minimum", "Average", "Maximum", "# of NaNs",
                                          "# of Infs"}; //, "# valid pixels"};
            constexpr int NUM_STATS    = sizeof(stat_names) / sizeof(stat_names[0]);
            for (int s = 0; s < NUM_STATS; ++s)
            {
                ImGui::PushFont(bold_font, 14);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
                ImGui::TextUnformatted(stat_names[s]);
                ImGui::PopFont();
                ImGui::PushFont(mono_font, 14);
                for (int c = 0; c < group.num_channels; ++c)
                {
                    ImGui::TableNextColumn();
                    switch (s)
                    {
                    case 0: ImGui::TextFmt("{: > 6.3f}", channel_stats[c]->summary.minimum); break;
                    case 1: ImGui::TextFmt("{: > 6.3f}", channel_stats[c]->summary.average); break;
                    case 2: ImGui::TextFmt("{: > 6.3f}", channel_stats[c]->summary.maximum); break;
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
}
