//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// Adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include "helpwindow.h"

#include <nanogui/button.h>
#include <nanogui/icons.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/window.h>
#include "well.h"
#include <spdlog/spdlog.h>

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

    auto add_row = [](Widget* current, string keys, string desc)
    {
        auto row = new Widget(current);
        row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 0));
        (new Label(row, desc, "sans", 14))->set_fixed_width(185);
        new Label(row, keys, "sans-bold", 14);
    };

	new Label(this, "About", "sans-bold", 18);

	auto copyright_widget = new Widget(this);
	copyright_widget->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 0));

	string about = fmt::format(
		"HDRView {}. Copyright (c) Wojciech Jarosz\n\n"
		"(built on {} from git {}-{}-{} using {} backend)\n\n"
		"HDRView is a simple research-oriented tool for examining, "
		"comparing, manipulating, and converting high-dynamic range images.\n\n"
		"HDRView is freely available under a 3-clause BSD license.\n\n",
		HDRVIEW_VERSION, hdrview_timestamp(),
		hdrview_git_branch(), hdrview_git_version(), hdrview_git_revision(),
		HDRVIEW_BACKEND);
	(new Label(copyright_widget, about))->set_fixed_width(715);

	new Label(this, "Keybindings", "sans-bold", 18);

	auto key_bindings_widget = new Well(this);
	key_bindings_widget->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 10, 0));

	auto new_column = [key_bindings_widget]()
	{
		auto w = new Widget(key_bindings_widget);
		w->set_layout(new GroupLayout(0));
		w->set_fixed_width(350);
		return w;
	};

	auto column = new_column();

	new Label(column, "Images and Layer List", "sans-bold", 16);
	auto image_loading = new Widget(column);
	image_loading->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	add_row(image_loading, COMMAND + "+O", "Open Image");
	add_row(image_loading, COMMAND + "+S", "Save Image");
	add_row(image_loading, COMMAND + "+W or Delete", "Close Image");
	add_row(image_loading, COMMAND + "+Shift+W", "Close All Images");
	add_row(image_loading, "Left Click", "Select Image");
	add_row(image_loading, "Shift+Left Click", "Select/Deselect Reference Image");
	add_row(image_loading, "1…9", "Select the N-th Image");
	add_row(image_loading, "Down / Up", "Select Previous/Next Image");
	add_row(image_loading, COMMAND + "+Down / " + COMMAND + "+Up", "Send Image Forward/Backward");
	add_row(image_loading, ALT + "+Tab", "Jump Back To Previously Selected Image");
	add_row(image_loading, COMMAND + "+F", "Find Image");

	new Label(column, "Display/Tonemapping Options", "sans-bold", 16);
	auto image_selection = new Widget(column);
	image_selection->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	add_row(image_selection, "E / Shift+E", "Decrease/Increase Exposure");
	add_row(image_selection, "G / Shift+G", "Decrease/Increase Gamma");
	add_row(image_selection, "R", "Reset tonemapping");
	add_row(image_selection, "N", "Normalize Image to [0,1]");
	add_row(image_selection, COMMAND + "+1…7", "Cycle through Color Channels");
	add_row(image_selection, "Shift+1…8", "Cycle through Blend Modes");

	column = new_column();

	new Label(column, "Image Edits", "sans-bold", 16);
	auto edits = new Widget(column);
	edits->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	add_row(edits, COMMAND + "+Z / " + COMMAND + "+Shift+Z", "Undo/Redo");
	add_row(edits, COMMAND + "+C / " + COMMAND + "+V", "Copy/Paste");\

	new Label(column, "Panning/Zooming/Selecting", "sans-bold", 16);
	auto panning_zooming = new Widget(column);
	panning_zooming->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	add_row(panning_zooming, "Space", "Switch mouse to pan/zoom mode");
	add_row(panning_zooming, "Left Click+Drag / Shift+Scroll", "Pan image");
	add_row(panning_zooming, "Scroll", "Zoom In and Out Continuously");
	add_row(panning_zooming, "- / +", "Zoom In and Out by Powers of 2");
    add_row(panning_zooming, COMMAND + "+0", "Fit Image to Screen");
	add_row(panning_zooming, "M", "Switch mouse to selection mode");
	add_row(panning_zooming, COMMAND + "+A", "Select entire image");
	add_row(panning_zooming, COMMAND + "+D", "Deselect");

	new Label(column, "Interface", "sans-bold", 16);
	auto interface = new Widget(column);
	interface->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));

	add_row(interface, "H", "Show/Hide Help (this Window)");
	add_row(interface, "T", "Show/Hide the Top Toolbar");
	add_row(interface, "Tab", "Show/Hide the Side Panel");
	add_row(interface, "Shift+Tab", "Show/Hide All Panels");
	add_row(interface, COMMAND + "+Q or Esc", "Quit");
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
