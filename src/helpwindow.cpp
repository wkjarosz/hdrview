//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// Adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include "helpwindow.h"

#include "well.h"
#include <nanogui/button.h>
#include <nanogui/icons.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/window.h>
#include <spdlog/spdlog.h>

using namespace std;

NAMESPACE_BEGIN(nanogui)

#ifdef __APPLE__
const string HelpWindow::COMMAND = "Cmd";
#else
const string HelpWindow::COMMAND = "Ctrl";
#endif

#ifdef __APPLE__
const string HelpWindow::ALT = "Opt";
#else
const string HelpWindow::ALT     = "Alt";
#endif

HelpWindow::HelpWindow(Widget *parent) : Dialog(parent, "Help", false)
{
    set_layout(new GroupLayout());

    new Label(this, "About", "sans-bold", 18);

    auto copyright_widget = new Widget(this);
    copyright_widget->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 0));

    string about = fmt::format("HDRView {}. Copyright (c) Wojciech Jarosz\n\n"
                               "(built on {} from git branch {} {} using {} backend)\n\n"
                               "HDRView is a simple research-oriented tool for examining, "
                               "comparing, manipulating, and converting high-dynamic range images.\n\n"
                               "HDRView is freely available under a 3-clause BSD license.\n\n",
                               hdrview_git_version(), hdrview_timestamp(), hdrview_git_branch(), hdrview_git_revision(),
                               HDRVIEW_BACKEND);
    (new Label(copyright_widget, about))->set_fixed_width(715);

    new Label(this, "Keybindings", "sans-bold", 18);

    m_key_bindings = new Well(this);
    m_key_bindings->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 10, 0));

    add_column();

    center();
}

void HelpWindow::add_column()
{
    auto w = new Widget(m_key_bindings);
    w->set_layout(new GroupLayout(0, 0));
    w->set_fixed_width(350);
}

bool HelpWindow::add_section(const std::string &desc)
{
    if (m_sections.count(desc))
        return false;

    auto column = m_key_bindings->child_at(m_key_bindings->child_count() - 1);
    new Label(column, desc, "sans-bold", 16);
    auto w = new Widget(column);
    w->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));
    m_sections[desc] = w;
    return true;
}

void HelpWindow::add_shortcut(const string &section, const string &keys, const string &desc)
{
    if (m_sections.count(section) == 0)
        add_section(section);

    auto w = m_sections[section];

    auto row = new Widget(w);
    row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 0));
    (new Label(row, desc, "sans", 14))->set_fixed_width(185);
    new Label(row, keys, "sans-bold", 14);
};

NAMESPACE_END(nanogui)
