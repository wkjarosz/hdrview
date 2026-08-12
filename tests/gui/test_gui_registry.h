/** \file test_gui_registry.h
    \author Wojciech Jarosz
*/

#pragma once

struct ImGuiTestEngine;

void RegisterTests_Smoke(ImGuiTestEngine *engine);
void RegisterTests_Dialogs(ImGuiTestEngine *engine);
void RegisterTests_ImageIO(ImGuiTestEngine *engine);

inline void RegisterAllGuiTests(ImGuiTestEngine *engine)
{
    RegisterTests_Smoke(engine);
    RegisterTests_Dialogs(engine);
    RegisterTests_ImageIO(engine);
}
