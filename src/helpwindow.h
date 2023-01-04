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
#include "dialog.h"
#include <map>
#include <string>

NAMESPACE_BEGIN(nanogui)
class HelpWindow : public Dialog
{
public:
    HelpWindow(nanogui::Widget *parent);

    bool add_section(const std::string &name);
    void add_shortcut(const std::string &section, const std::string &keys, const std::string &desc);
    void add_separator(const std::string &section, int height = 10);

    static const std::string CMD; //! Platform-dependent name for the command/ctrl key
    static const std::string ALT; //! Platform-dependent name for the alt/option key

    //! Takes a fmt-format string and replaces any instances of {CMD} and {ALT} with CMD an ALT.
    static std::string key_string(const std::string &text);

private:
    Widget                         *m_key_bindings;
    std::map<std::string, Widget *> m_sections;
};

NAMESPACE_END(nanogui)
