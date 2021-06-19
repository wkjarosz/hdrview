
#pragma once

#include <nanogui/messagedialog.h>
#include <nanogui/nanogui.h>

NAMESPACE_BEGIN(nanogui)

/// Similar to MessageDialog but extensible and can be used with FormHelper class
class Dialog : public Window
{
public:
    Dialog(Widget *parent, const std::string &title = "Untitled", bool form = true);

    void    make_form();
    Widget *add_buttons(const std::string &button_text = "OK", const std::string &alt_button_text = "Cancel",
                        bool alt_button = true);

    std::function<void(int)> callback() const { return m_callback; }
    void                     set_callback(const std::function<void(int)> &callback) { m_callback = callback; }

protected:
    std::function<void(int)> m_callback;
};

NAMESPACE_END(nanogui)
