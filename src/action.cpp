//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "action.h"
#include <map>
#include <nanogui/opengl.h>
#include <spdlog/fmt/ostr.h>

using std::string;
using std::vector;

//! Platform-dependent name for the command/ctrl key
#ifdef __APPLE__
static const string CMD = "Cmd";
#else
static const string CMD = "Ctrl";
#endif

//! Platform-dependent name for the alt/option key
#ifdef __APPLE__
static const string ALT = "Opt";
#else
static const string ALT = "Alt";
#endif

NAMESPACE_BEGIN(nanogui)

string Shortcut::key_string(const string &text)
{
    return fmt::format(text, fmt::arg("CMD", CMD), fmt::arg("ALT", ALT));
}

Shortcut::Shortcut(int m, int k) : modifiers(m), key(k)
{
    if (modifiers & SYSTEM_COMMAND_MOD)
        text += key_string("{CMD}+");
    if (modifiers & GLFW_MOD_ALT)
        text += key_string("{ALT}+");
    if (modifiers & GLFW_MOD_SHIFT)
        text += "Shift+";

    // printable characters
    if (32 < key && key < 128)
        text += char(key);
    // function keys
    else if (GLFW_KEY_F1 <= key && key <= GLFW_KEY_F25)
        text += fmt::format("F{}", key - GLFW_KEY_F1 + 1);
    else if (GLFW_KEY_KP_0 <= key && key <= GLFW_KEY_KP_0)
        text += fmt::format("{}", key - GLFW_KEY_KP_0);

    static const std::map<int, string> key_map = {
        {GLFW_KEY_SPACE, "Space"},
        {GLFW_KEY_ESCAPE, "Esc"},
        {GLFW_KEY_ENTER, "Enter"},
        {GLFW_KEY_TAB, "Tab"},
        {GLFW_KEY_BACKSPACE, "Backspace"},
        {GLFW_KEY_INSERT, "Insert"},
        {GLFW_KEY_DELETE, "Delete"},
        {GLFW_KEY_RIGHT, "Right"},
        {GLFW_KEY_LEFT, "Left"},
        {GLFW_KEY_DOWN, "Down"},
        {GLFW_KEY_UP, "Up"},
        {GLFW_KEY_PAGE_UP, "Page Up"},
        {GLFW_KEY_PAGE_DOWN, "Page Down"},
        {GLFW_KEY_HOME, "Home"},
        {GLFW_KEY_END, "End"},
        {GLFW_KEY_CAPS_LOCK, "Caps lock"},
        {GLFW_KEY_SCROLL_LOCK, "Scroll lock"},
        {GLFW_KEY_NUM_LOCK, "Num lock"},
        {GLFW_KEY_PRINT_SCREEN, "Print"},
        {GLFW_KEY_PAUSE, "Pause"},
        {GLFW_KEY_KP_DECIMAL, "."},
        {GLFW_KEY_KP_DIVIDE, "/"},
        {GLFW_KEY_KP_MULTIPLY, "*"},
        {GLFW_KEY_KP_SUBTRACT, "-"},
        {GLFW_KEY_KP_ADD, "+"},
        {GLFW_KEY_KP_ENTER, "Enter"},
        {GLFW_KEY_KP_EQUAL, "="},
    };

    if (auto search = key_map.find(key); search != key_map.end())
        text += search->second;
}

Action::Action(const string &text, int icon, ActionGroup *group, const vector<Shortcut> &shortcuts) :
    m_text(text), m_icon(icon), m_shortcuts(shortcuts)
{
    set_group(group);
}

void Action::trigger()
{
    bool checked_backup = m_checked;

    if (!m_checked)
    {
        // first uncheck all other (checkable) actions in the group
        for (auto a : m_group->actions)
        {
            if (a != this && a->checkable() && a->checked())
            {
                a->set_checked(false);
                if (a->toggled_callback())
                    a->toggled_callback()(false);
            }
        }

        m_checked = true;
        if (m_triggered_callback)
            m_triggered_callback();
    }
    else
        m_checked = false;

    if (checked_backup != m_checked && m_toggled_callback)
        m_toggled_callback(m_checked);
}

NAMESPACE_END(nanogui)