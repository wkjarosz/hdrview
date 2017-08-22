//
// Created by Wojciech Jarosz on 8/21/17.
//

#include "widgets.h"
#include <nanogui/messagedialog.h>
#include <nanogui/layout.h>
#include <nanogui/button.h>
#include <nanogui/entypo.h>
#include <nanogui/label.h>

using namespace nanogui;


Dialog::Dialog(Widget *parent, const std::string &title,
               const std::string &buttonText, const std::string &altButtonText, bool altButton,
               Widget * body) :
    Window(parent, title)
{
    setLayout(new BoxLayout(Orientation::Vertical, Alignment::Middle, 10, 10));
    setModal(true);

    if (body)
        addChild(body);

    auto buttonPanel = new Widget(this);
    buttonPanel->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 15));

    if (altButton)
    {
        Button *button = new Button(buttonPanel, altButtonText, ENTYPO_ICON_CIRCLED_CROSS);
        button->setCallback([&] { if (m_callback) m_callback(1); dispose(); });
    }
    Button *button = new Button(buttonPanel, buttonText, ENTYPO_ICON_CHECK);
    button->setCallback([&] { if (m_callback) m_callback(0); dispose(); });

    center();
    requestFocus();
}

