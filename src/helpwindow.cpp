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
#include <nanogui/tabwidget.h>
#include <nanogui/window.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace
{
constexpr int fwidth = 450;
}

NAMESPACE_BEGIN(nanogui)

#ifdef __APPLE__
const string HelpWindow::CMD = "Cmd";
#else
const string HelpWindow::CMD = "Ctrl";
#endif

#ifdef __APPLE__
const string HelpWindow::ALT = "Opt";
#else
const string HelpWindow::ALT = "Alt";
#endif

string HelpWindow::key_string(const string &text)
{
    return fmt::format(text, fmt::arg("CMD", HelpWindow::CMD), fmt::arg("ALT", HelpWindow::ALT));
}

HelpWindow::HelpWindow(Widget *parent) : Dialog(parent, "Help", false)
{
    set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 10, 0));

    auto add_text = [](Widget *current, string text, string font = "sans", int fontSize = 18)
    {
        auto row = new Widget{current};
        row->set_layout(new BoxLayout{Orientation::Vertical, Alignment::Middle, 0, 10});
        auto l = new Label{row, text, font, fontSize};
        return l;
    };
    auto add_spacer = [](Widget *current, int space)
    {
        auto row = new Widget{current};
        row->set_height(space);
    };
    auto add_library = [](Widget *current, string name, string desc)
    {
        auto row = new Widget(current);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 3, 20));
        auto left_column = new Widget(row);
        left_column->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Maximum));
        left_column->set_fixed_width(135);

        new Label(left_column, name, "sans-bold", 14);
        new Label(row, desc, "sans", 14);
    };

    add_text(this, "HDRView", "sans-bold", 46);
    add_text(this, fmt::format("version {}", hdrview_version()), "sans-bold", 26);
    add_spacer(this, 5);
    add_text(this, fmt::format("Built using the {} backend on {}.", HDRVIEW_BACKEND, hdrview_build_timestamp()), "sans",
             12);

    add_spacer(this, 15);

    add_text(this,
             "HDRView is a simple research-oriented tool for examining, "
             "comparing, manipulating, and converting high-dynamic range images.\n\n",
             "sans", 16)
        ->set_fixed_width(fwidth);

    auto tab_widget = new TabWidget(this);
    tab_widget->set_tabs_draggable(true);
    // adding a callback seems to be required for the tabwidget to actually update the visibility
    tab_widget->set_callback([](int) { return; });

    Widget *tab;

    tab = new Widget(tab_widget);
    tab->set_fixed_height(300);
    tab->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill));
    tab_widget->append_tab("Keybindings", tab);

    auto side_scroll_panel = new VScrollPanel(tab);
    side_scroll_panel->set_fixed_height(300);
    m_key_bindings = new Widget(side_scroll_panel);
    m_key_bindings->set_layout(new GroupLayout(20, 6));

    tab = new Widget(tab_widget);
    tab->set_fixed_height(300);
    tab->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill));
    tab_widget->append_tab("Credits", tab);
    side_scroll_panel = new VScrollPanel(tab);
    side_scroll_panel->set_fixed_height(300);
    auto credits = new Widget(side_scroll_panel);
    credits->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 20, 4));

    add_text(credits,
             "HDRView is developed by Wojciech Jarosz and is freely available under a 3-clause BSD license.\n\n"
             "It additionally makes use of the following techniques and external libraries:\n\n",
             "sans", 16)
        ->set_fixed_width(fwidth);

    add_library(credits, "NanoGUI", "Widget library for OpenGL/Metal");
    add_library(credits, "NanoVG", "Vector graphics in OpenGL/Metal");
    add_library(credits, "GLFW", "Multi-platform OpenGL/windowing library on the desktop");
    add_library(credits, "OpenEXR", "High Dynamic-Range (HDR) image file format");
    add_library(credits, "stb_image/write/resize", "Single-Header libraries for loading/writing/resizing images");
    add_library(credits, "CLI11", "Command line parser for C++11");
    add_library(credits, "spdlog", "Fast C++ logging library");
    add_library(credits, "{fmt}", "A modern formatting library");
    add_library(credits, "PlatformFolders", "Cross-platform library to find special directories");
    add_library(credits, "filesystem", "Lightweight path manipulation library");
    add_library(credits, "tinydir", "Lightweight and portable aC directory and file reader");
    add_library(credits, "tinydngloader", "Header-only tiny DNG/TIFF loader in C++");
    add_library(credits, "json", "JSON for Modern C++");
    add_library(credits, "alphanum", "Natural alpha-numeric sorting");
    add_library(credits, "Yuksel splines", "Cem Yuksel's hybrid C^2 splines for smooth mouse strokes");
    add_library(credits, "tev", "Some code is adapted from Thomas Müller's tev");
    add_library(credits, "colormaps", "Matt Zucker's degree 6 polynomial colormaps");

    center();
}

bool HelpWindow::add_section(const std::string &desc)
{
    if (m_sections.count(desc))
        return false;

    new Label(m_key_bindings, desc, "sans-bold", 16);
    auto w = new Widget(m_key_bindings);
    w->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));
    w->set_fixed_width(fwidth);
    m_sections[desc] = w;
    return true;
}

void HelpWindow::add_shortcut(const string &section, const string &keys, const string &desc)
{
    add_section(section);

    auto w = m_sections[section];

    auto row = new Widget(w);
    row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 0));
    (new Label(row, desc, "sans", 14))->set_fixed_width(0.6 * fwidth);
    new Label(row, key_string(keys), "sans-bold", 14);
};

NAMESPACE_END(nanogui)
