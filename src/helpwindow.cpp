// Adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include "helpwindow.h"

#include <nanogui/button.h>
#include <nanogui/entypo.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/window.h>

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
    : Window{parent, "Help"}, mCloseCallback{closeCallback}
{

    auto closeButton = new Button{buttonPanel(), "", ENTYPO_ICON_CROSS};
    closeButton->setCallback(mCloseCallback);

    setLayout(new GroupLayout{});

    setFixedWidth(435);
    auto addRow = [](Widget* current, string keys, string desc)
    {
        auto row = new Widget(current);
        row->setLayout(new BoxLayout{Orientation::Horizontal, Alignment::Fill, 0, 10});
        auto descWidget = new Label(row, desc, "sans");
        descWidget->setFixedWidth(210);
        new Label{row, keys, "sans-bold"};
    };

	new Label(this, "About", "sans-bold", 20);

	auto copyW = new Widget(this);
	copyW->setLayout(new BoxLayout{Orientation::Horizontal, Alignment::Fill, 0, 10});
	auto copy = new Label(copyW, "Copyright (c) Wojciech Jarosz\n\n"
		"HDRView is a simple research-oriented tool for examining, "
		"comparing, manipulating, and converting high-dynamic range images.\n\n"
		"HDRView is freely available under a 3-clause BSD license.");
	copy->setFixedWidth(400);

	new Label(this, "Keybindings", "sans-bold", 20);

	new Label(this, "Images and Layer List", "sans-bold", 18);
	auto imageLoading = new Widget(this);
	imageLoading->setLayout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 0, 0});

	addRow(imageLoading, COMMAND + "+O", "Open Image");
	addRow(imageLoading, COMMAND + "+S", "Save Image");
	addRow(imageLoading, COMMAND + "+W or Delete", "Close Image");
	addRow(imageLoading, COMMAND + "+[ / " + COMMAND + "+]", "Send Image Forward/Backward");
	addRow(imageLoading, "Down / Up or Page Down / Up", "Select Previous/Next Image");

    new Label(this, "Display/Tonemapping Options", "sans-bold", 18);
    auto imageSelection = new Widget(this);
    imageSelection->setLayout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 0, 0});

	addRow(imageSelection, "E / Shift+E", "Decrease/Increase Exposure");
	addRow(imageSelection, "G / Shift+G", "Decrease/Increase Gamma");
	addRow(imageSelection, "D", "Toggle Dithering");
	addRow(imageSelection, "1/2/3/4", "View the R/G/B/RGB Channels");

	new Label(this, "Panning/Zooming", "sans-bold", 18);
	auto panningZooming = new Widget(this);
	panningZooming->setLayout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 0, 0});

	addRow(panningZooming, "Scroll", "Zoom In and Out Continuously");
	addRow(panningZooming, "Left Click+Drag", "Pan image");
	addRow(panningZooming, "- / +", "Zoom In and Out by Powers of 2");
	addRow(panningZooming, "Space", "Re-Center View");
    addRow(panningZooming, "Home", "Fit Image to Screen");

	new Label(this, "Image Edits", "sans-bold", 18);
	auto edits = new Widget(this);
	edits->setLayout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 0, 0});

	addRow(edits, "F", "Flip image about horizontal axis");
	addRow(edits, "M", "Mirror image about vertical axis");
	addRow(edits, COMMAND + "+Z / " + COMMAND + "+Shift+Z", "Undo/Redo");

    new Label(this, "Interface", "sans-bold", 18);
    auto interface = new Widget(this);
    interface->setLayout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 0, 0});

    addRow(interface, "H", "Show/Hide Help (this Window)");
    addRow(interface, "T", "Show/Hide the Top Toolbar");
	addRow(interface, "Tab", "Show/Hide the Side Panel");
	addRow(interface, "Shift+Tab", "Show/Hide All Panels");
    addRow(interface, COMMAND + "+Q or Esc", "Quit");
}

bool HelpWindow::keyboardEvent(int key, int scancode, int action, int modifiers)
{
    if (Window::keyboardEvent(key, scancode, action, modifiers))
    {
        return true;
    }

    if (key == GLFW_KEY_ESCAPE)
    {
        mCloseCallback();
        return true;
    }

    return false;
}
