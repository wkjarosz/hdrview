/** \file test_gui_registry.h
    \author Wojciech Jarosz
*/

#pragma once

struct ImGuiTestEngine;

void RegisterTests_Smoke(ImGuiTestEngine *engine);
void RegisterTests_Dialogs(ImGuiTestEngine *engine);
void RegisterTests_ImageIO(ImGuiTestEngine *engine);
void RegisterTests_View(ImGuiTestEngine *engine);
void RegisterTests_Viewport(ImGuiTestEngine *engine);
void RegisterTests_Display(ImGuiTestEngine *engine);
void RegisterTests_Tools(ImGuiTestEngine *engine);
void RegisterTests_Stats(ImGuiTestEngine *engine);
void RegisterTests_Filtering(ImGuiTestEngine *engine);
void RegisterTests_Navigation(ImGuiTestEngine *engine);
void RegisterTests_Multipart(ImGuiTestEngine *engine);
void RegisterTests_Session(ImGuiTestEngine *engine);
void RegisterTests_SessionBundle(ImGuiTestEngine *engine);
void RegisterTests_Lifetime(ImGuiTestEngine *engine);
void RegisterTests_Loader(ImGuiTestEngine *engine);

inline void RegisterAllGuiTests(ImGuiTestEngine *engine)
{
    RegisterTests_Smoke(engine);
    RegisterTests_Dialogs(engine);
    RegisterTests_ImageIO(engine);
    RegisterTests_View(engine);
    RegisterTests_Viewport(engine);
    RegisterTests_Display(engine);
    RegisterTests_Tools(engine);
    RegisterTests_Stats(engine);
    RegisterTests_Filtering(engine);
    RegisterTests_Navigation(engine);
    RegisterTests_Multipart(engine);
    RegisterTests_Session(engine);
    RegisterTests_SessionBundle(engine);
    RegisterTests_Lifetime(engine);
    RegisterTests_Loader(engine);
}
