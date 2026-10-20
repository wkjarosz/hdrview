//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "save_options.h"
#include "fonts.h"
#include "imgui.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

TransferFunction labeled_curve(const TransferFunction &asked, TransferFunction::Type_ curve, const char *format)
{
    if (asked.type != curve)
        spdlog::warn("{} cannot record the {} transfer function; writing {} instead.", format,
                     transfer_function_name(asked), transfer_function_name(curve));
    return curve;
}

void caveat_entry(const Caveat &caveat)
{
    if (!caveat.text)
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.f, 0.75f, 0.25f, 1.f});
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(fmt::format(ICON_MY_LOG_LEVEL_WARN "  {}", caveat.text).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void no_options_entry()
{
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("This format has no options.");
    ImGui::EndDisabled();
}
