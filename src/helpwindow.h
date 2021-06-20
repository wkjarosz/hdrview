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

#include <string>

NAMESPACE_BEGIN(nanogui)
class HelpWindow : public Dialog
{
public:
    HelpWindow(nanogui::Widget *parent);

    static std::string COMMAND;
    static std::string ALT;
};

NAMESPACE_END(nanogui)
