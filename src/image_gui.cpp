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

#include "imgui_ext.h"
#include "imgui_internal.h"

#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

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
