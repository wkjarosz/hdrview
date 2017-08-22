//
// widgets.h: Additional widgets that are not part of NanoGUI
// Created by Wojciech Jarosz on 8/21/17.
//

#pragma once

#include <nanogui/window.h>

/*!
 * A dialog window with included "OK" and "Cancel" buttons, and an extensible body widget
 */
class Dialog : public nanogui::Window
{
public:
    Dialog(Widget *parent, const std::string &title = "Untitled",
           const std::string &buttonText = "OK",
           const std::string &altButtonText = "Cancel", bool altButton = false,
           Widget * body = nullptr);

    std::function<void(int)> callback() const { return m_callback; }
    void setCallback(const std::function<void(int)> &callback) { m_callback = callback; }

protected:
    std::function<void(int)> m_callback;
    Widget * m_bodyWidget;
};
