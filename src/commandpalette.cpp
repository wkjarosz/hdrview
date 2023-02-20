
#include "commandpalette.h"
#include "menu.h"
#include <nanogui/button.h>
#include <nanogui/formhelper.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

NAMESPACE_BEGIN(nanogui)

CommandPalette::CommandPalette(Widget *parent, const std::vector<MenuItem *> &commands) : Dialog(parent, "", false)
{
    FormHelper *gui = new FormHelper(screen());

    {
        auto layout = new AdvancedGridLayout({0, 0, 0, 0}, {});
        layout->set_margin(5);
        layout->set_col_stretch(2, 1);
        set_layout(layout);
    }

    gui->set_window(this);

    auto search_box = new SearchBox(this, "");
    search_box->set_editable(true);
    search_box->set_alignment(TextBox::Alignment::Left);
    search_box->set_placeholder("Select a command...");

    gui->add_widget("", search_box);

    auto vscroll = new VScrollPanel{this};
    vscroll->set_fixed_height(300);

    m_commandlist = new FilterableList{vscroll};

    auto scroll_to_ensure_visible = [this, vscroll](int j)
    {
        if (j >= m_commandlist->child_count())
            return;

        auto item = dynamic_cast<MenuItem *>(m_commandlist->child_at(j));

        spdlog::trace("scroll_to_ensure_visible({}): item {}", j, item->caption());
        spdlog::trace("item->position().y(): {}; m_commandlist->height(): {}; vscroll->height(): {}",
                      item->position().y(), m_commandlist->height(), vscroll->height());

        // compute visible range

        int visible_top    = -m_commandlist->position().y();
        int visible_bottom = visible_top + vscroll->height();
        spdlog::trace("{} : {}", visible_top, visible_bottom);

        int item_top    = item->position().y();
        int item_bottom = item_top + item->height();
        if (item_bottom <= visible_top)
        {
            // item is above the visible region, need to scroll up
            spdlog::trace("item is above the visible region, need to scroll up");
            float new_scroll = item_top / float(m_commandlist->height() - vscroll->height());
            spdlog::trace("setting scroll to {}", new_scroll);
            vscroll->set_scroll(new_scroll);
        }
        else if (item_top >= visible_bottom)
        {
            // item is below the visible region, need to scroll down
            spdlog::trace("item is below the visible region, need to scroll down");
            float new_scroll =
                (item_top - vscroll->height() + item->height()) / float(m_commandlist->height() - vscroll->height());
            spdlog::trace("setting scroll to {}", new_scroll);
            vscroll->set_scroll(new_scroll);
        }
        else
        {
            // item is visible
            spdlog::trace("item is visible");
        }
    };

    for (size_t j = 0; j < commands.size(); ++j)
    {
        auto *c = commands[j];
        auto  i = new MenuItem{m_commandlist, c->caption(), c->icon(), {c->shortcut(0)}};
        i->set_callback(
            [c, cb = c->callback(), this]()
            {
                dispose();
                cb();
            });
        if (c->flags() & Button::ToggleButton)
        {
            i->set_pushed(c->pushed());
            if (c->change_callback())
                i->set_change_callback([c, cb = c->change_callback(), this](bool b) { return cb(b); });
        }
        i->set_highlight_callback(
            [this, j, scroll_to_ensure_visible](bool b)
            {
                if (b)
                {
                    m_commandlist->set_current_index(j);
                    scroll_to_ensure_visible(j);
                }
            });
        i->set_visible(c->enabled());
    }

    gui->add_widget("", vscroll);

    auto first_item = dynamic_cast<MenuItem *>(m_commandlist->child_at(0));
    first_item->set_highlighted(true, true, true);

    // auto dialog = new Dialog(this, "");
    set_callback([this](int result) { this->set_visible(result != 0); });

    // auto flat_theme                             = new Theme(nvg_context());
    // flat_theme->m_standard_font_size            = 16;
    // flat_theme->m_button_font_size              = 15;
    // flat_theme->m_text_box_font_size            = 16;
    // flat_theme->m_window_corner_radius          = 8;
    // flat_theme->m_window_fill_unfocused         = Color(50, 255);
    // flat_theme->m_window_fill_focused           = Color(52, 255);
    // flat_theme->m_window_header_height          = 0;
    // flat_theme->m_drop_shadow                   = Color(0, 100);
    // flat_theme->m_window_drop_shadow_size       = 100;
    // flat_theme->m_button_corner_radius          = 4;
    // flat_theme->m_border_light                  = flat_theme->m_transparent;
    // flat_theme->m_border_dark                   = flat_theme->m_transparent;
    // flat_theme->m_button_gradient_top_focused   = Color(77, 124, 233, 255);
    // flat_theme->m_button_gradient_bot_focused   = flat_theme->m_button_gradient_top_focused;
    // flat_theme->m_button_gradient_top_unfocused = flat_theme->m_transparent;
    // flat_theme->m_button_gradient_bot_unfocused = flat_theme->m_transparent;
    // flat_theme->m_button_gradient_top_pushed    = flat_theme->m_transparent;
    // flat_theme->m_button_gradient_bot_pushed    = flat_theme->m_button_gradient_top_pushed;
    // flat_theme->m_window_popup                  = Color(38, 255);
    // flat_theme->m_text_color_shadow             = flat_theme->m_transparent;
    // dialog->set_theme(flat_theme);

    auto menu_theme                             = new Theme(screen()->nvg_context());
    menu_theme->m_standard_font_size            = 16;
    menu_theme->m_button_font_size              = 15;
    menu_theme->m_text_box_font_size            = 18;
    menu_theme->m_window_corner_radius          = 8;
    menu_theme->m_window_fill_unfocused         = Color(25, 255);
    menu_theme->m_window_fill_focused           = Color(25, 255);
    menu_theme->m_drop_shadow                   = Color(0, 100);
    menu_theme->m_window_header_height          = 0;
    menu_theme->m_window_drop_shadow_size       = 100;
    menu_theme->m_button_corner_radius          = 4;
    menu_theme->m_border_light                  = menu_theme->m_transparent;
    menu_theme->m_border_dark                   = menu_theme->m_transparent;
    menu_theme->m_button_gradient_top_focused   = Color(77, 124, 233, 255);
    menu_theme->m_button_gradient_bot_focused   = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_bot_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_unfocused = menu_theme->m_transparent;
    menu_theme->m_button_gradient_bot_unfocused = menu_theme->m_transparent;
    menu_theme->m_window_popup                  = Color(38, 255);
    menu_theme->m_text_color_shadow             = menu_theme->m_transparent;

    set_theme(menu_theme);

    {
        if (size() == 0)
        {
            set_size(preferred_size(screen()->nvg_context()));
            perform_layout(screen()->nvg_context());
        }
        auto pos = (screen()->size() - size()) / 2;
        pos.y() /= 2;
        set_position(pos);
    }
    search_box->request_focus();
}

bool CommandPalette::keyboard_event(int key, int scancode, int action, int modifiers)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
    {
        dispose();
        return true;
    }

    if (Dialog::keyboard_event(key, scancode, action, modifiers))
        return true;

    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        if (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN)
        {
            m_commandlist->set_current_index(m_commandlist->next_visible_item(m_commandlist->current_index(),
                                                                              key == GLFW_KEY_UP ? Backward : Forward));
            auto item = dynamic_cast<MenuItem *>(m_commandlist->child_at(m_commandlist->current_index()));
            item->set_highlighted(true, true, true);
            return true;
        }
        else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
        {
            auto item = dynamic_cast<MenuItem *>(m_commandlist->child_at(m_commandlist->current_index()));
            if (item->flags() & Button::ToggleButton || item->flags() & Button::RadioButton)
            {
                if (item->change_callback())
                    item->change_callback()(!item->pushed());
            }
            else
            {
                if (item->callback())
                    item->callback()();
            }
            return true;
        }
    }

    return false;
}

NAMESPACE_END(nanogui)
