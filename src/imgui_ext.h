#include "fwd.h"
#include "imgui.h"

#include <string>

namespace ImGui
{

inline bool ToggleButton(const char *label, bool *active)
{
    ImGui::PushStyleColor(ImGuiCol_Button, *active ? GetColorU32(ImGuiCol_ButtonActive) : GetColorU32(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetColorU32(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetColorU32(ImGuiCol_FrameBgActive));

    bool ret;
    if ((ret = ImGui::Button(label)))
        *active = !*active;
    ImGui::PopStyleColor(3);
    return ret;
}

inline void Text(const std::string &text) { return Text("%s", text.c_str()); }

inline void TextUnformatted(const std::string &text) { return TextUnformatted(text.c_str()); }

// return true when activated.
inline bool MenuItem(const std::string &label, const std::string &shortcut = "", bool selected = false,
                     bool enabled = true)
{
    return MenuItem(label.c_str(), shortcut.c_str(), selected, enabled);
}

// return true when activated + toggle (*p_selected) if p_selected != NULL
inline bool MenuItem(const std::string &label, const std::string &shortcut, bool *p_selected, bool enabled = true)
{
    return MenuItem(label.c_str(), shortcut.c_str(), p_selected, enabled);
}

void AddTextAligned(ImDrawList *draw_list, float2 pos, ImU32 color, const std::string &text,
                    float2 align = float2{0.f});

void ScrollWhenDraggingOnVoid(const ImVec2 &delta, ImGuiMouseButton mouse_button);

} // namespace ImGui