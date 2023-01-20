
#include "dialog.h"
#include <nanogui/button.h>
#include <nanogui/formhelper.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>

NAMESPACE_BEGIN(nanogui)

Dialog::Dialog(Widget *parent, const std::string &title, bool form) : Window(parent, title)
{
    set_modal(true);

    request_focus();
    if (form)
        make_form();
}

void Dialog::make_form()
{
    // copied from FormHelper
    auto layout = new AdvancedGridLayout({10, 0, 10, 0}, {});
    layout->set_margin(10);
    layout->set_col_stretch(2, 1);
    set_layout(layout);
}

Widget *Dialog::add_buttons(const std::string &button_text, bool alt_button, const std::string &alt_button_text)
{
    Widget *button_panel = new Widget(this);
    button_panel->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 5));

    if (alt_button)
    {
        Button *button = new Button(button_panel, alt_button_text, m_theme->m_message_alt_button_icon);
        button->set_callback(
            [this]
            {
                if (m_callback)
                    m_callback(1);
                dispose();
            });
    }
    Button *button = new Button(button_panel, button_text, m_theme->m_message_primary_button_icon);
    button->set_callback(
        [this]
        {
            if (m_callback)
                m_callback(0);
            dispose();
        });

    return button_panel;
}

SimpleDialog::SimpleDialog(Widget *parent, Type type, const std::string &title, const std::string &message,
                           const std::string &button_text, const std::string &alt_button_text, bool alt_button) :
    Dialog(parent, title, false)
{
    set_layout(new BoxLayout(Orientation::Vertical, Alignment::Middle, 10, 10));

    Widget *message_panel = new Widget(this);
    message_panel->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 10, 15));
    int icon = 0;
    switch (type)
    {
    case Type::Empty: icon = 0; break;
    case Type::Information: icon = m_theme->m_message_information_icon; break;
    case Type::Question: icon = m_theme->m_message_question_icon; break;
    case Type::Warning: icon = m_theme->m_message_warning_icon; break;
    }
    Label *icon_label = new Label(message_panel, std::string(utf8(icon).data()), "icons");
    icon_label->set_font_size(50);
    m_message_label = new Label(message_panel, message);
    m_message_label->set_fixed_width(icon ? 200 : 0);

    add_buttons(button_text, alt_button, alt_button_text);

    center();
    request_focus();
}

NAMESPACE_END(nanogui)
