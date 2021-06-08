//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// Adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include "common.h"

#include <nanogui/window.h>

#include <string>

class HelpWindow : public nanogui::Window
{
public:
    HelpWindow(nanogui::Widget *parent, std::function<void()> closeCallback);

    bool keyboard_event(int key, int scancode, int action, int modifiers) override;

    static std::string COMMAND;
    static std::string ALT;

private:
    std::function<void()> m_close_callback;
};
