//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "fwd.h"

#include "app.h"
#include "common.h"
#include "fonts.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "misc/cpp/imgui_stdlib.h"
#include <hello_imgui/dpi_aware.h>

#include <spdlog/fmt/chrono.h>
#include <string>

using namespace std;
using namespace HelloImGui;

static std::chrono::system_clock::time_point to_system_clock(std::filesystem::file_time_type ftime)
{
    using namespace std::chrono;
    return time_point_cast<system_clock::duration>(ftime - std::filesystem::file_time_type::clock::now() +
                                                   system_clock::now());
}

void Image::draw_layer_groups(const Layer &layer, int img_idx, int &id_, bool is_current, bool is_reference,
                              bool short_names, int &visible_group, float &scroll_to)
{
    static constexpr ImGuiTreeNodeFlags tree_node_flags =
        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf |
        ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_Bullet;
    for (size_t g = 0; g < layer.groups.size(); ++g)
    {
        auto  &group      = groups[layer.groups[g]];
        string group_name = group.num_channels == 1 ? group.name : "(" + group.name + ")";
        string name       = string(ICON_MY_CHANNEL_GROUP) + " " + (short_names ? group_name : layer.name + group_name);

        // check if any of the contained channels pass the channel filter
        if (!group.visible)
            continue;

        // The group on screen, and the group's membership of the multi-selection: two different things,
        // and a row can be either without being the other.
        bool is_current_channel   = is_current && selected_group == layer.groups[g];
        bool is_reference_channel = is_reference && reference_group == layer.groups[g];
        bool is_selected_channel  = is_group_selected(layer.groups[g]);

        ImGuiTreeNodeFlags flags = tree_node_flags | (is_current_channel || is_reference_channel || is_selected_channel
                                                          ? ImGuiTreeNodeFlags_Selected
                                                          : ImGuiTreeNodeFlags_None);
        ImGui::TreeRow((void *)(intptr_t)id_++, flags, name.c_str(),
                       [&]
                       {
                           string shortcut = is_current && visible_group < 10
                                                 ? fmt::format(ICON_MY_KEY_CONTROL "{}", mod(visible_group + 1, 10))
                                                 : "";
                           ImGui::TextAligned2(0.0f, -FLT_MIN, shortcut.c_str());
                       },
                       [&] {
                           ImGui::PushRowColors(is_current_channel, is_reference_channel, ImGui::GetIO().KeyShift,
                                                is_selected_channel);
                       });

        // Right-clicking a group points at it without selecting it, so a lone depth channel can be deleted
        // while a color stays on screen. Right-clicking one that is already selected covers the whole
        // selection instead; see HDRViewApp::target_groups().
        const int this_group = layer.groups[g];

        if (ImGui::BeginPopupContextItem())
        {
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();

            // Drawn as if the group were already pointed at, so what each item says and whether it is
            // offered match what choosing it would do on this image, which need not be the current one.
            hdrview()->with_target_group(
                img_idx, this_group,
                [&]
                {
                    for (const char *command : {"Ungroup channels", "Regroup channels", "Delete channel group"})
                    {
                        const auto &a = hdrview()->action(command);

                        // The label says whether it is about to delete one channel, a group, or several;
                        // the action's name stays put, since that is what addresses it.
                        auto         target = hdrview()->target_image();
                        const string label  = string(command) == "Delete channel group"
                                                  ? delete_channels_label(target, hdrview()->target_groups(target))
                                                  : a.names[0];

                        // Spelled out as strings: imgui_ext declares a MenuItemEx taking std::string, and a
                        // null here would bind to that and construct a string from nullptr.
                        if (ImGui::MenuItemEx(label, a.icon, ImGui::GetKeyChordNameTranslated(a.chord), nullptr,
                                              a.enabled()))
                            // Next frame: deleting a group rebuilds the layers and groups this loop is
                            // walking.
                            hdrview()->post_to_main_thread(
                                [command, img_idx, this_group]
                                { hdrview()->invoke_action_on_group(command, img_idx, this_group); });
                    }
                });
            ImGui::EndPopup();
        }

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            // Shift on its own is the reference modifier, as it has always been; everything else is the
            // selection, with ctrl/cmd to toggle one group and ctrl/cmd+shift to take a range.
            auto &io = ImGui::GetIO();
            if (io.KeyShift && !io.KeyCtrl)
            {
                spdlog::trace("Shift-clicked on {}", name);
                // check if we are already the reference channel group
                if (is_reference_channel)
                {
                    spdlog::trace("Clearing reference image");
                    hdrview()->set_reference_image_index(-1, true);
                    reference_group = -1;
                }
                else
                {
                    spdlog::trace("Setting reference image to {}", img_idx);
                    hdrview()->set_reference_image_index(img_idx);
                    reference_group = this_group;
                }
                set_as_texture(Target_Secondary);
            }
            else
            {
                if (io.KeyShift)
                    hdrview()->select_group_range_to(img_idx, this_group);
                else if (io.KeyCtrl)
                    hdrview()->toggle_group_selected(img_idx, this_group);
                else
                    hdrview()->set_current_group(img_idx, this_group);

                // Not necessarily this image: taking the current target out of the selection hands current
                // to whatever is still in it, which can be another image entirely.
                if (auto cur = hdrview()->current_image())
                    cur->set_as_texture(Target_Primary);
            }
        }
        else if (is_current_channel && scroll_to >= -0.5f)
        {
            if (!ImGui::IsItemVisible())
                ImGui::SetScrollHereY(scroll_to);
            scroll_to = -1.f;
        }

        ++visible_group;
    }
}

/**

*/
void Image::draw_layer_node(const LayerTreeNode &node, int img_idx, int &id_, bool is_current, bool is_reference,
                            int &visible_group, float &scroll_to)
{
    static constexpr ImGuiTreeNodeFlags tree_node_flags =
        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull;

    if (node.leaf_layer >= 0)
        // draw this node's leaf channel groups
        draw_layer_groups(layers[node.leaf_layer], img_idx, id_, is_current, is_reference, true, visible_group,
                          scroll_to);

    for (auto &c : node.children)
    {
        const LayerTreeNode &child_node = c.second;
        if (child_node.visible_groups == 0)
            continue;

        bool open =
            ImGui::TreeRow((void *)(intptr_t)id_++, tree_node_flags,
                           fmt::format("{} {}", ICON_MY_OPEN_IMAGE, child_node.name).c_str(), nullptr,
                           [&]
                           {
                               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                               ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGuiCol_Header);
                               ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGuiCol_Header);
                           });
        if (open)
        {
            draw_layer_node(child_node, img_idx, id_, is_current, is_reference, visible_group, scroll_to);
            ImGui::TreePop();
        }
        else
        {
            // still account for visible groups within the closed tree node
            visible_group += child_node.visible_groups;
        }
    }
}

int Image::draw_channel_rows(int img_idx, int &id_, bool is_current, bool is_reference, float &scroll_to)
{
    int visible_group = 0;
    for (size_t l = 0; l < layers.size(); ++l)
        draw_layer_groups(layers[l], img_idx, id_, is_current, is_reference, false, visible_group, scroll_to);

    return visible_group;
}

void Image::draw_info()
{
    std::locale loc("en_US.UTF-8");
    auto        bold_font = hdrview()->font("sans bold");

    static ImGuiTextFilter filter;
    const ImVec2           button_size   = ImGui::IconButtonSize();
    bool                   filter_active = filter.IsActive(); // save here to avoid flicker

    auto draw_filter_input = [&]()
    {
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
    };

    auto filtered_property = [&](const string &property_name, const string &value, const string &tooltip = "")
    {
        if (filter.PassFilter((property_name + " " + value).c_str()))
            ImGui::PE::WrappedText(property_name, value, tooltip, bold_font);
    };

    auto get_tooltip = [](const json &field_obj)
    {
        std::string tt;
        if (field_obj.contains("description") && field_obj["description"].is_string())
            tt += field_obj["description"].get<std::string>() + "\n\n";

        if (field_obj.contains("ifd") && field_obj["ifd"].is_number())
            tt += fmt::format("IFD: {}\n", field_obj["ifd"].get<int>());

        if (field_obj.contains("tag") && field_obj["tag"].is_number())
            tt += fmt::format("Tag: {}\n", field_obj["tag"].get<int>());

        if (field_obj.contains("type") && field_obj["type"].is_string())
            tt += fmt::format("Type: {}\n", field_obj["type"].get<std::string>());

        if (field_obj.contains("value"))
        {
            const auto &v = field_obj["value"];
            if (!v.is_object() && !v.is_string() &&
                (!v.is_array() || (v.is_array() && v.size() > 0 && v.size() <= 5 && v[0].is_number())))
                tt += fmt::format("Value: {}", v.dump());
        }
        return tt;
    };

    // Flat field drawer used for header/exif and other simple metadata sections.
    auto add_fields = [&](const json &fields)
    {
        for (auto &field : fields.items())
        {
            const std::string &key       = field.key();
            const auto        &field_obj = field.value();
            if (!field_obj.is_object() || !field_obj.contains("string"))
                continue;

            auto value  = field_obj["string"].get<std::string>();
            auto concat = key + " " + value;
            if (!filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                continue;

            ImGui::PE::WrappedText(key, value, get_tooltip(field_obj), bold_font);
        }
    };
    // Recursive drawer specifically for XMP nested structures.
    std::function<void(const json &, int, const string &)> add_xmp_fields =
        [&](const json &fields, int depth, const string &prefix)
    {
        for (const auto &[key, field_val] : fields.items())
        {
            // Determine display value
            std::string disp;
            if (field_val.is_string())
                disp = field_val.get<std::string>();
            else if (field_val.is_number())
                disp = field_val.dump();
            else if (field_val.is_boolean())
                disp = field_val.get<bool>() ? "true" : "false";

            auto concat = prefix + ":" + key + " " + disp;
            if (!filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                continue;

            // Handle objects (nested structures)
            if (field_val.is_object())
            {
                if (ImGui::PE::TreeNode(key.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull))
                {
                    add_xmp_fields(field_val, depth + 1, prefix + ":" + key);
                    ImGui::PE::TreePop();
                }
                continue;
            }

            // Handle arrays
            if (field_val.is_array())
            {
                const auto &arr = field_val;

                // If the array is all basic scalar types (string/number/bool), render it as a single
                // wrapped text entry containing the dumped JSON for readability.
                // bool all_basic = true;
                // for (const auto &e : arr)
                // {
                //     if (e.is_object())
                //     {
                //         all_basic = false;
                //         break;
                //     }
                // }

                // if (all_basic)
                // {
                //     ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                //     ImGui::PE::WrappedText(key, field_val.dump(), "", bold_font);
                //     ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                //     continue;
                // }

                // Otherwise, handle mixed or object arrays element-by-element as before.

                // we special case arrays with one element to avoid an unnecessary nesting
                bool open = true;
                if (arr.size() > 1)
                {
                    open = ImGui::PE::TreeNode(key.c_str(),
                                               ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::TextUnformatted(fmt::format("[{} item{}]", arr.size(), arr.size() == 1 ? "" : "s").c_str());
                }
                if (open)
                {
                    for (size_t i = 0; i < arr.size(); ++i)
                    {
                        std::string idx = arr.size() > 1 ? fmt::format("#{}", i + 1) : key;
                        if (arr[i].is_object())
                        {
                            bool open2 = ImGui::PE::TreeNode(idx.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                                                                              ImGuiTreeNodeFlags_DrawLinesFull);
                            if (arr.size() <= 1)
                            {
                                ImGui::TableNextColumn();
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                ImGui::TextUnformatted("[1 item]");
                            }
                            if (open2)
                            {
                                add_xmp_fields(arr[i], depth + 1, prefix + ":" + key);
                                ImGui::PE::TreePop();
                            }
                        }
                        else if (arr[i].is_string())
                        {
                            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                            ImGui::PE::WrappedText(idx, arr[i].get<std::string>(), "", bold_font);
                            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                        }
                        else
                        {
                            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                            ImGui::PE::Entry(idx, arr[i].dump());
                            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                        }
                    }
                    if (arr.size() > 1)
                        ImGui::PE::TreePop();
                }
                continue;
            }

            // Scalar values
            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
            ImGui::PE::WrappedText(key, disp, "", bold_font);
            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
        }
    };

    auto draw_no_metadata_message = [&](const char *message)
    {
        ImGui::Indent(ImGui::GetStyle().CellPadding.x);
        ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase);
        ImGui::BeginDisabled();
        ImGui::TextUnformatted(message);
        ImGui::EndDisabled();
        ImGui::PopFont();
        ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
    };

    bool has_exif   = exif.valid() && metadata.contains("exif") && metadata["exif"].is_object();
    bool has_xmp    = !xmp_data.empty() && metadata.contains("xmp") && metadata["xmp"].is_object();
    bool has_header = metadata.contains("header") && metadata["header"].is_object();

    bool has_view[] = {true, has_header, has_exif, has_xmp, has_xmp};

    static int         selected_view    = 0;
    static const char *views[]          = {"General", "Header", "EXIF", "XMP", "Raw XMP data"};
    static const char *views_disabled[] = {"General", "Header", "EXIF (not present)", "XMP (not present)",
                                           "Raw XMP data (not present)"};

    float w = ImGui::GetContentRegionAvail().x - 1.f * (button_size.x + ImGui::GetStyle().ItemSpacing.x);

    static bool expand_to_listbox = false;

    auto show_view_options = [&]()
    {
        for (int n = 0; n < IM_ARRAYSIZE(views); n++)
        {
            const bool is_selected = (selected_view == n);
            ImGui::PushStyleColor(ImGuiCol_Text, has_view[n] ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(has_view[n] ? views[n] : views_disabled[n], is_selected))
                selected_view = n;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopStyleColor();
        }
    };

    if (!expand_to_listbox || ImGui::GetContentRegionAvail().y < EmSize(20.f))
    {
        ImGui::SetNextItemWidth(w);
        if (ImGui::BeginCombo("##Which view combo",
                              has_view[selected_view] ? views[selected_view] : views_disabled[selected_view]))
        {
            show_view_options();
            ImGui::EndCombo();
        }
    }
    else
    {
        // Custom size: use all width, 5 items tall
        if (ImGui::BeginListBox("##Which view listbox", ImVec2(w, 5.0f * ImGui::GetTextLineHeightWithSpacing() +
                                                                      ImGui::GetStyle().FramePadding.y)))
        {
            show_view_options();
            ImGui::EndListBox();
        }
    }
    ImGui::SameLine();
    ImGui::IconButton(expand_to_listbox ? ICON_MY_EXPAND_ALL : ICON_MY_COLLAPSE_ALL, &expand_to_listbox);
    ImGui::Tooltip(expand_to_listbox ? "Click to collapse info sections to a combobox."
                                     : "Click to expand info sections to a listbox.");

    if (selected_view == 0)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
        if (ImGui::PE::Begin("Image info", ImGuiTableFlags_ScrollY))
        {
            // calculate left column based on the longest property name
            ImGui::PushFont(hdrview()->font("sans bold"), ImGui::GetStyle().FontSizeBase);
            float col_width = ImGui::CalcTextSize("Channel selector").x + ImGui::GetStyle().CellPadding.x;
            ImGui::PopFont();

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, col_width);

            ImGui::Indent(ImGui::GetStyle().CellPadding.x);
            filtered_property("File name", filename);
            filtered_property(
                "File size",
                fmt::format(std::locale("en_US.UTF-8"), "{:.1h} ({:L} bytes)", human_readible{size_bytes}, size_bytes),
                "This is the size of the image file on disk. If the image consists of multiple parts, "
                "this is the size of the entire file.");
            filtered_property("Last modified", fmt::format("{:%b %d, %Y at %I:%M %p}", to_system_clock(last_modified)));
            filtered_property("Part name", partname.empty() ? "<none>" : partname.c_str());
            filtered_property("Channel selector", channel_selector.empty() ? "<none>" : channel_selector.c_str());
            filtered_property("Loader", metadata.value<string>("loader", "unknown"));
            filtered_property("Pixel format", metadata.value<string>("pixel format", "unknown"));
            filtered_property("Resolution", fmt::format("{} {} {}", size().x, ICON_MY_TIMES, size().y));
            filtered_property("Data window", fmt::format("[{}, {}) {} [{}, {})", data_window.min.x, data_window.max.x,
                                                         ICON_MY_TIMES, data_window.min.y, data_window.max.y));
            filtered_property("Display window",
                              fmt::format("[{}, {}) {} [{}, {})", display_window.min.x, display_window.max.x,
                                          ICON_MY_TIMES, display_window.min.y, display_window.max.y));
            // An override says what it displaced, since contradicting a kind the file stated is not the
            // same as filling in one nothing ever did.
            const string from_file = fmt::format("{}{}", transparency_type_name(transparency_from_file),
                                                 transparency_assumed ? " (assumed)" : "");
            filtered_property(
                "Transparency",
                transparency_override
                    ? fmt::format("{} (override; was {})", transparency_type_name(transparency), from_file)
                    : from_file,
                "How this image's alpha channel is read: whether it means transparency, and if so in what "
                "space. \"Assumed\" means nothing in the file stated it and the "
                "loader picked a default, which is where the override below is most likely wanted.");

            if (filter.PassFilter("Transparency override"))
            {
                // draw_info() is only ever drawn for the current image (see the Info window)
                const bool reloadable = hdrview()->can_reload(hdrview()->current_image());
                string     tooltip =
                    "Read the alpha channel as something other than what the file says. \"None\" treats a fourth "
                    "channel "
                    "as ordinary data rather than transparency, so nothing is multiplied by it.\n\nThe two "
                    "premultiplied kinds differ in where the multiply happened; pick the other one for an image "
                    "whose semi-transparent areas read too dark or too bright.\n\nChanging this re-reads the "
                    "image from its source.";
                if (!reloadable)
                    tooltip += "\n\nUnavailable for this image: HDRView has nothing left to read it from.";

                ImGui::BeginDisabled(!reloadable);
                ImGui::PE::Entry(
                    "Transparency override",
                    [this]
                    {
                        static constexpr const char *k_not_overridden = "Not overridden";

                        bool changed = false;
                        if (ImGui::BeginCombo("##Transparency override",
                                              transparency_override ? transparency_override_name(*transparency_override)
                                                                    : k_not_overridden))
                        {
                            if (ImGui::Selectable(k_not_overridden, !transparency_override))
                            {
                                transparency_override.reset();
                                changed = true;
                            }

                            for (TransparencyType_ a = 0; a < TransparencyType_Count; ++a)
                                if (ImGui::Selectable(transparency_override_name(a),
                                                      transparency_override && *transparency_override == a))
                                {
                                    transparency_override = a;
                                    changed               = true;
                                }

                            ImGui::EndCombo();
                        }

                        if (!changed)
                            return false;

                        // The premultiply happens in-place on load, so switching interpretation means reading
                        // the image again; reload_image() carries the new setting through.
                        hdrview()->reload_image(hdrview()->current_image());
                        return true;
                    },
                    tooltip);
                ImGui::EndDisabled();
            }
            if (exif.valid())
                filtered_property("EXIF data", fmt::format("{:.0h}", human_readible{exif.size()}),
                                  "Size of the EXIF metadata block embedded in the image file.");
            if (!xmp_data.empty())
                filtered_property("XMP data", fmt::format("{:.0h}", human_readible{xmp_data.size()}),
                                  "Size of the XMP metadata block embedded in the image file.");
            if (!icc_data.empty())
                filtered_property("ICC data", fmt::format("{:.0h}", human_readible{icc_data.size()}),
                                  "Size of the ICC profile embedded in the image file.");
            ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
            ImGui::PE::End();
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    else if (selected_view == 1)
    {
        if (has_header)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
            if (ImGui::PE::Begin("Header", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize |
                                               ImGuiTableFlags_ScrollY))
            {
                ImGui::Indent(ImGui::GetStyle().CellPadding.x);
                add_fields(metadata["header"]);
                ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
                ImGui::PE::End();
            }
            ImGui::PopStyleColor();
        }
        else
            draw_no_metadata_message("No additional header data present in this image.");
    }
    else if (selected_view == 2)
    {
        if (has_exif)
        {
            draw_filter_input();

            if (ImGui::PE::Begin("EXIF info", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize |
                                                  ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Key");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
                ImGui::TableHeadersRow();

                ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0);
                for (auto &exif_entry : metadata["exif"].items())
                {
                    const auto &table_obj = exif_entry.value();
                    if (!table_obj.is_object())
                        continue;

                    ImGui::PushFont(bold_font, 0.f);
                    auto open = ImGui::PE::TreeNode(exif_entry.key().c_str(), ImGuiTreeNodeFlags_SpanFullWidth |
                                                                                  ImGuiTreeNodeFlags_SpanAllColumns |
                                                                                  ImGuiTreeNodeFlags_DefaultOpen |
                                                                                  ImGuiTreeNodeFlags_DrawLinesFull);
                    ImGui::PopFont();
                    if (open)
                    {
                        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                        add_fields(table_obj);
                        ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                        ImGui::PE::TreePop();
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PE::End();
            }
        }
        else
            draw_no_metadata_message("No XMP data present in this image.");
    }
    else if (selected_view == 3)
    {
        if (has_xmp)
        {
            draw_filter_input();

            json xmlns = metadata["xmp"]["xmlns"];

            if (ImGui::PE::Begin("XMP info", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize |
                                                 ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Key");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
                ImGui::TableHeadersRow();

                ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0);

                // get the namespaces in display order
                std::vector<std::string> namespaces_to_display;
                if (metadata["xmp"].contains("display_order") && metadata["xmp"]["display_order"].is_array())
                {
                    for (const auto &key_json : metadata["xmp"]["display_order"])
                    {
                        if (key_json.is_string())
                        {
                            std::string key = key_json.get<std::string>();
                            if (metadata["xmp"].contains(key) && metadata["xmp"][key].is_object())
                                namespaces_to_display.push_back(key);
                        }
                    }
                }
                else
                {
                    for (const auto &xmp_entry : metadata["xmp"].items())
                    {
                        if (xmp_entry.key() != "xmlns" && xmp_entry.value().is_object())
                            namespaces_to_display.push_back(xmp_entry.key());
                    }
                }

                for (const auto &ns : namespaces_to_display)
                {
                    const auto &table_obj = metadata["xmp"][ns];
                    string      name      = xmlns[ns]["name"];
                    string      uri       = xmlns[ns]["uri"];

                    ImGui::PushFont(bold_font, 0.f);
                    auto open = ImGui::PE::TreeNode(
                        name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_SpanAllColumns |
                                          ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull);
                    ImGui::PopFont();
                    if (open)
                    {
                        add_xmp_fields(table_obj, 0, name + " " + ns);
                        ImGui::PE::TreePop();
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PE::End();
            }
        }
        else
            draw_no_metadata_message("No XMP data present in this image.");
    }
    else if (selected_view == 4)
    {
        if (has_xmp)
        {
            ImGui::BeginChild("##xmp_scroll", ImVec2(-1, -1), ImGuiChildFlags_FrameStyle,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushFont(hdrview()->font("mono regular"), ImGui::GetStyle().FontSizeBase);
            ImGui::TextUnformatted(reinterpret_cast<const char *>(xmp_data.data()),
                                   reinterpret_cast<const char *>(xmp_data.data()) + xmp_data.size());
            ImGui::PopFont();
            ImGui::Tooltip("Click to copy to clipboard.");
            if (ImGui::IsItemClicked())
                ImGui::SetClipboardText(
                    string(reinterpret_cast<const char *>(xmp_data.data()), xmp_data.size()).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::EndChild();
        }
        else
            draw_no_metadata_message("No XMP data present in this image.");
    }
}

void Image::draw_channel_stats()
{
    auto &group      = groups[selected_group];
    int   components = group.num_channels;
    bool  is_color   = group.type == ChannelGroup::RGBA_Channels || group.type == ChannelGroup::RGB_Channels;

    PixelStats *channel_stats[4] = {nullptr, nullptr, nullptr, nullptr};
    string      channel_names[4];
    for (int c = 0; c < components; ++c)
    {
        auto &channel = channels[group.channels[c]];
        channel.update_stats(c, hdrview()->current_image(), hdrview()->reference_image());
        channel_stats[c] = channel.get_stats();
        channel_names[c] = Channel::tail(channel.name);
    }

    float exposure_gain = pow(2.f, hdrview()->exposure_live());

    // Persisted per-row display mode. Module-static, so shared across all images, which only one image's
    // stats being shown at a time makes safe.
    static int mode_min = ImGui::ChannelDisplayMode_Raw, mode_avg = ImGui::ChannelDisplayMode_Raw,
               mode_max = ImGui::ChannelDisplayMode_Raw, mode_stddev = ImGui::ChannelDisplayMode_Raw,
               mode_nan = ImGui::ChannelDisplayMode_Raw, mode_inf = ImGui::ChannelDisplayMode_Raw,
               mode_huge = ImGui::ChannelDisplayMode_Raw;

    auto stat_row = [&](auto &&accessor, ImGuiDataType data_type, const char *format, bool show_swatch,
                        ImGui::ChannelDisplayModeMask enabled_modes, int *mode, const string &label,
                        const string &tooltip = {})
    {
        float raw[4] = {0.f, 0.f, 0.f, 1.f};
        for (int c = 0; c < components; ++c) raw[c] = (float)accessor(c);

        float4 displayed{0.f, 0.f, 0.f, 1.f};
        if (show_swatch)
        {
            // The swatch goes through the tonemap and, in false-color mode, a colormap lookup that indexes
            // with an integer, neither of which a NaN sample survives. The numbers beside it come from
            // `raw` and still report what the file holds.
            float4 finite{raw[0], raw[1], raw[2], raw[3]};
            for (int c = 0; c < 4; ++c)
                if (!std::isfinite(finite[c]))
                    finite[c] = 0.f;
            displayed = linear_to_sRGB(hdrview()->tonemap_value(finite));
        }

        ImGui::PE::Entry(
            label,
            [&]
            {
                ImGui::ChannelValuesRow(label.c_str(), raw, show_swatch ? &displayed.x : nullptr, components, data_type,
                                        format, exposure_gain, mode, enabled_modes,
                                        /*allow_copy=*/true, show_swatch,
                                        ImVec4{displayed.x, displayed.y, displayed.z, displayed.w},
                                        /*label=*/{}, ImGui::PE::ColumnWidth(1));
                return false;
            },
            tooltip);
    };

    // Channel names as a row of their own, positioned via the PE table's value-column width, since a PE
    // table has no shared header row. No left-column label: "Statistics" is in the SeparatorText above.
    ImGui::PE::Entry("",
                     [&]
                     {
                         ImGui::ChannelValuesRowHeader(channel_names, components, ImGui::PE::ColumnWidth(1),
                                                       /*reserve_swatch_gap=*/true);
                         return false;
                     });

    stat_row([&](int c) { return channel_stats[c]->summary.minimum; }, ImGuiDataType_Float, "%g", is_color,
             is_color ? ImGui::ChannelDisplayMode_AllMask : ImGui::ChannelDisplayMode_NoDisplayMask, &mode_min,
             "Minimum");
    stat_row([&](int c) { return channel_stats[c]->summary.average; }, ImGuiDataType_Float, "%g", is_color,
             is_color ? ImGui::ChannelDisplayMode_AllMask : ImGui::ChannelDisplayMode_NoDisplayMask, &mode_avg,
             "Average");
    stat_row([&](int c) { return channel_stats[c]->summary.maximum; }, ImGuiDataType_Float, "%g", is_color,
             is_color ? ImGui::ChannelDisplayMode_AllMask : ImGui::ChannelDisplayMode_NoDisplayMask, &mode_max,
             "Maximum");
    stat_row([&](int c) { return channel_stats[c]->summary.stddev; }, ImGuiDataType_Float, "%g", false,
             ImGui::ChannelDisplayMode_NoDisplayMask, &mode_stddev, "Std. Dev.");
    stat_row([&](int c) { return channel_stats[c]->summary.nan_pixels; }, ImGuiDataType_S32, "%d", false,
             ImGui::ChannelDisplayMode_RawOnlyMask, &mode_nan, "# NaNs");
    stat_row([&](int c) { return channel_stats[c]->summary.inf_pixels; }, ImGuiDataType_S32, "%d", false,
             ImGui::ChannelDisplayMode_RawOnlyMask, &mode_inf, "# Infs");
    stat_row([&](int c) { return channel_stats[c]->summary.huge_pixels; }, ImGuiDataType_S32, "%d", false,
             ImGui::ChannelDisplayMode_RawOnlyMask, &mode_huge, "# Huge",
             "Pixels at \u00b1FLT_MAX (3.40282e+38), the largest magnitude finite 32-bit float. These are "
             "excluded from the summary statistics above.");
}
