
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

/// Identical to MessageDialog but derived from the above Dialog class.
class SimpleDialog : public Dialog
{
public:
    /// Classification of the type of message this SimpleDialog represents.
    enum class Type
    {
        Empty,
        Information,
        Question,
        Warning
    };

    SimpleDialog(Widget *parent, Type type, const std::string &title = "Untitled",
                 const std::string &message = "Message", const std::string &button_text = "OK",
                 const std::string &alt_button_text = "Cancel", bool alt_button = false);

    Label *      message_label() { return m_message_label; }
    const Label *message_label() const { return m_message_label; }

protected:
    Label *m_message_label;
};

NAMESPACE_END(nanogui)
