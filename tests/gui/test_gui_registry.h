/** \file test_gui_registry.h
    \author Wojciech Jarosz
*/

#pragma once

struct ImGuiTestEngine;

void RegisterTests_Smoke(ImGuiTestEngine *engine);
void RegisterTests_Dialogs(ImGuiTestEngine *engine);
void RegisterTests_ImageIO(ImGuiTestEngine *engine);
void RegisterTests_View(ImGuiTestEngine *engine);
void RegisterTests_Tools(ImGuiTestEngine *engine);
void RegisterTests_Stats(ImGuiTestEngine *engine);
void RegisterTests_Filtering(ImGuiTestEngine *engine);
void RegisterTests_Navigation(ImGuiTestEngine *engine);
void RegisterTests_Multipart(ImGuiTestEngine *engine);
void RegisterTests_Session(ImGuiTestEngine *engine);

inline void RegisterAllGuiTests(ImGuiTestEngine *engine)
{
    RegisterTests_Smoke(engine);
    RegisterTests_Dialogs(engine);
    RegisterTests_ImageIO(engine);
    RegisterTests_View(engine);
    RegisterTests_Tools(engine);
    RegisterTests_Stats(engine);
    RegisterTests_Filtering(engine);
    RegisterTests_Navigation(engine);
    RegisterTests_Multipart(engine);
    RegisterTests_Session(engine);
}
