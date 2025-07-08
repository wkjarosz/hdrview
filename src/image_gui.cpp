//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

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

    auto property_name = [bold_font](const string &text)
    {
        ImGui::PushFont(bold_font, 14);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
    };
    auto property_value = [](const string &text, ImFont *font, bool wrapped = false)
    {
        ImGui::SameLine();
        ImGui::PushFont(font, 14);

        float avail_w = ImGui::GetContentRegionAvail().x;
        float text_w  = ImGui::CalcTextSize(text.c_str()).x;

        if (text_w < avail_w)
        {
            // place it on the same line as the property name
            ImGui::TextUnformatted(text);
        }
        else
        {
            // place it indented on the next line
            ImGui::NewLine();
            ImGui::Indent();
            if (wrapped)
                ImGui::TextWrapped("%s", text.c_str());
            else
                ImGui::TextUnformatted(text);
            ImGui::Unindent();
        }
        ImGui::PopFont();
    };

    property_name("File name:");
    property_value(filename, sans_font, true);

    property_name("Part name:");
    property_value(partname.empty() ? "<none>" : partname, sans_font, true);

    property_name("Loader:");
    property_value(metadata.value<string>("loader", "unknown"), sans_font, true);

    property_name("File's bit depth:");
    property_value(metadata.value<string>("bit depth", "unknown"), sans_font, true);

    property_name("Linearization function:");
    ImGui::WrappedTooltip("The transfer function applied at load time to make the values linear.");
    property_value(metadata.value<string>("transfer function", "linear"), sans_font, true);

    vector<pair<string, string>> attrib_names = {
        {"owner", "Owner"}, {"comments", "Comments"}, {"capDate", "Capture date"}};

    for (auto a : attrib_names)
    {
        if (auto attrib = header.findTypedAttribute<Imf::StringAttribute>(a.first))
        {
            property_name(a.second + ":");
            property_value(attrib->value(), sans_font, true);
        }
    }

    property_name("Resolution:");
    property_value(fmt::format("{} {} {}", size().x, ICON_MY_TIMES, size().y), sans_font);

    property_name("Data window:");
    property_value(fmt::format("[{}, {}) {} [{}, {})", data_window.min.x, data_window.max.x, ICON_MY_TIMES,
                               data_window.min.y, data_window.max.y),
                   sans_font);

    property_name("Display window:");
    property_value(fmt::format("[{}, {}) {} [{}, {})", display_window.min.x, display_window.max.x, ICON_MY_TIMES,
                               display_window.min.y, display_window.max.y),
                   sans_font);

    auto csn        = color_gamut_names();
    auto open_combo = ImGui::BeginCombo("Color gamut", named_color_space < 0 ? "Unknown" : csn[named_color_space],
                                        ImGuiComboFlags_HeightLargest);
    ImGui::WrappedTooltip(
        "Interpret the values stored in the file using the chromaticities of a common color profile.");
    if (open_combo)
    {
        for (int n = 0; csn[n]; ++n)
        {
            const bool is_selected = (named_color_space == n);
            if (ImGui::Selectable(csn[n], is_selected))
            {
                named_color_space = n;
                spdlog::debug("Switching to color space {}.", n);
                Imf::addChromaticities(header, gamut_chromaticities(csn[n]));
                compute_color_transform();
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    {
        const char *wan[]      = {"None", "XYZ scaling", "Bradford", "Von Kries", nullptr};
        auto        open_combo = ImGui::BeginCombo(
            "Adaptation", adaptation_method <= 0 || adaptation_method > 3 ? "None" : wan[adaptation_method],
            ImGuiComboFlags_HeightLargest);
        ImGui::WrappedTooltip("Method for chromatic adaptation transform.");
        if (open_combo)
        {
            for (int n = 0; wan[n]; ++n)
            {
                const bool is_selected = (adaptation_method == n);
                if (ImGui::Selectable(wan[n], is_selected))
                {
                    adaptation_method = n;
                    spdlog::debug("Switching to adaptation method {}.", n);
                    compute_color_transform();
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (auto attrib = header.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities"))
    {
        auto &chr = attrib->value();
        // property_name("Chromaticities:");
        property_value(
            fmt::format("R primary: ({}, {})\nG primary: ({}, {})\nB primary: ({}, {})\nWhitepoint: ({}, {})",
                        chr.red.x, chr.red.y, chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x,
                        chr.white.y),
            mono_font);
    }
    else
    {
        Imf::Chromaticities chr{};
        // property_name("Chromaticities:");
        ImGui::BeginDisabled();
        property_value(fmt::format("<Assuming BT 709>\nR primary: ({}, {})\nG primary: ({}, {})\nB primary: ({}, "
                                   "{})\nWhitepoint: ({}, {})",
                                   chr.red.x, chr.red.y, chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x,
                                   chr.white.y),
                       mono_font);
        ImGui::EndDisabled();
    }

    if (auto attrib = header.findTypedAttribute<Imf::V2fAttribute>("adoptedNeutral"))
    {
        property_name("Adopted neutral:");
        property_value(fmt::format("({}, {})", attrib->value().x, attrib->value().y), mono_font);
    }

    property_name("Luminance weights:");
    property_value(
        fmt::format("{:+8.2e}, {:+8.2e}, {:+8.2e}", luminance_weights.x, luminance_weights.y, luminance_weights.z),
        mono_font);

    property_name("Color transform matrix:");
    property_value(fmt::format("{:+8.2e}, {:+8.2e}, {:+8.2e}\n"
                               "{:+8.2e}, {:+8.2e}, {:+8.2e}\n"
                               "{:+8.2e}, {:+8.2e}, {:+8.2e}",
                               M_to_Rec709[0][0], M_to_Rec709[0][1], M_to_Rec709[0][2], // prevent wrap
                               M_to_Rec709[1][0], M_to_Rec709[1][1], M_to_Rec709[1][2], //
                               M_to_Rec709[2][0], M_to_Rec709[2][1], M_to_Rec709[2][2]),
                   mono_font);

    property_name("File had straight alpha:");
    property_value(fmt::format("{}", file_has_straight_alpha), sans_font);
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
