//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// Adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include "HelpWindow.h"

#include <nanogui/button.h>
#include <nanogui/icons.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/window.h>
#include "Well.h"

using namespace nanogui;
using namespace std;

#ifdef __APPLE__
string HelpWindow::COMMAND = "Cmd";
#else
string HelpWindow::COMMAND = "Ctrl";
#endif

#ifdef __APPLE__
string HelpWindow::ALT = "Opt";
#else
string HelpWindow::ALT = "Alt";
#endif

HelpWindow::HelpWindow(Widget *parent, function<void()> closeCallback)
    : Window{parent, "Help"}, m_close_callback{closeCallback}
{

    auto closeButton = new Button{button_panel(), "", FA_TIMES};
    closeButton->set_callback(m_close_callback);

    set_layout(new GroupLayout());

    auto addRow = [](Widget* current, string keys, string desc)
    {
        auto row = new Widget(current);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 0));
        auto descWidget = new Label(row, desc, "sans", 14);
        descWidget->set_fixed_width(185);
        new Label{row, keys, "sans-bold", 14};
    };

	new Label(this, "About", "sans-bold", 18);

	auto copyW = new Widget(this);
	copyW->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 0));
	auto copy = new Label(copyW, "HDRView " HDRVIEW_VERSION ". Copyright (c) Wojciech Jarosz\n\n"
		"HDRView is a simple research-oriented tool for examining, "
		"comparing, manipulating, and converting high-dynamic range images.\n\n"
		"HDRView is freely available under a 3-clause BSD license.");
	copy->set_fixed_width(715);

	new Label(this, "Keybindings", "sans-bold", 18);

	auto keyBindingsWidget = new Well(this);
	keyBindingsWidget->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 10, 0));

	auto newColumn = [keyBindingsWidget]()
	{
		auto w = new Widget(keyBindingsWidget);
		w->set_layout(new GroupLayout(0));
		w->set_fixed_width(350);
		return w;
	};

	auto column = newColumn();

	new Label(column, "Images and Layer List", "sans-bold", 16);
	auto imageLoading = new Widget(column);
	imageLoading->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	addRow(imageLoading, COMMAND + "+O", "Open Image");
	addRow(imageLoading, COMMAND + "+S", "Save Image");
	addRow(imageLoading, COMMAND + "+W or Delete", "Close Image");
	addRow(imageLoading, COMMAND + "+Shift+W", "Close All Images");
	addRow(imageLoading, "Left Click", "Select Image");
	addRow(imageLoading, "Shift+Left Click", "Select/Deselect Reference Image");
	addRow(imageLoading, "1…9", "Select the N-th Image");
	addRow(imageLoading, "Down / Up", "Select Previous/Next Image");
	addRow(imageLoading, COMMAND + "+Down / " + COMMAND + "+Up", "Send Image Forward/Backward");
	addRow(imageLoading, ALT + "+Tab", "Jump Back To Previously Selected Image");
	addRow(imageLoading, COMMAND + "+F", "Find Image");

	new Label(column, "Display/Tonemapping Options", "sans-bold", 16);
	auto imageSelection = new Widget(column);
	imageSelection->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	addRow(imageSelection, "E / Shift+E", "Decrease/Increase Exposure");
	addRow(imageSelection, "G / Shift+G", "Decrease/Increase Gamma");
	addRow(imageSelection, "R", "Reset tonemapping");
	addRow(imageSelection, "N", "Normalize Image to [0,1]");
	addRow(imageSelection, COMMAND + "+1…7", "Cycle through Color Channels");
	addRow(imageSelection, "Shift+1…8", "Cycle through Blend Modes");

	column = newColumn();

	new Label(column, "Image Edits", "sans-bold", 16);
	auto edits = new Widget(column);
	edits->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	addRow(edits, "F", "Flip image about horizontal axis");
	addRow(edits, "M", "Mirror image about vertical axis");
	addRow(edits, COMMAND + "+Z / " + COMMAND + "+Shift+Z", "Undo/Redo");

	new Label(column, "Panning/Zooming", "sans-bold", 16);
	auto panningZooming = new Widget(column);
	panningZooming->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	addRow(panningZooming, "Left Click+Drag / Shift+Scroll", "Pan image");
	addRow(panningZooming, "Scroll", "Zoom In and Out Continuously");
	addRow(panningZooming, "- / +", "Zoom In and Out by Powers of 2");
	addRow(panningZooming, "Space", "Re-Center View");
    addRow(panningZooming, COMMAND + "+0", "Fit Image to Screen");

	new Label(column, "Interface", "sans-bold", 16);
	auto interface = new Widget(column);
	interface->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	addRow(interface, "H", "Show/Hide Help (this Window)");
	addRow(interface, "T", "Show/Hide the Top Toolbar");
	addRow(interface, "Tab", "Show/Hide the Side Panel");
	addRow(interface, "Shift+Tab", "Show/Hide All Panels");
	addRow(interface, COMMAND + "+Q or Esc", "Quit");
}

bool HelpWindow::keyboard_event(int key, int scancode, int action, int modifiers)
{
    if (Window::keyboard_event(key, scancode, action, modifiers))
    {
        return true;
    }

    if (key == GLFW_KEY_ESCAPE)
    {
        m_close_callback();
        return true;
    }

    return false;
}
