#include "imgui_ext.h"
#include "box.h"
#include "imgui_internal.h"

using namespace std;

void ImGui::AddTextAligned(ImDrawList *draw_list, float2 pos, ImU32 color, const string &text, float2 align)
{
    draw_list->AddText(pos - align * float2{ImGui::CalcTextSize(text.c_str())}, color, text.c_str());
}

// from https://github.com/ocornut/imgui/issues/3379#issuecomment-1678718752
void ImGui::ScrollWhenDraggingOnVoid(const ImVec2 &delta, ImGuiMouseButton mouse_button)
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