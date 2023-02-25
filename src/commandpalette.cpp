
#include "commandpalette.h"
#include "menu.h"
#include "well.h"
#include "widgetutils.h"
#include <nanogui/button.h>
#include <nanogui/formhelper.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

#define FTS_FUZZY_MATCH_IMPLEMENTATION 1

#include "fts_fuzzy_match.h"

using namespace std;

NAMESPACE_BEGIN(nanogui)

CommandPalette::CommandPalette(Widget *parent, const std::vector<MenuItem *> &commands) :
    Dialog(parent, "", false), m_original_commands(commands)
{
    auto menu_theme                             = new Theme(screen()->nvg_context());
    menu_theme->m_standard_font_size            = 16;
    menu_theme->m_button_font_size              = 15;
    menu_theme->m_text_box_font_size            = 18;
    menu_theme->m_window_corner_radius          = 8;
    menu_theme->m_window_fill_unfocused         = Color(32, 255);
    menu_theme->m_window_fill_focused           = Color(32, 255);
    menu_theme->m_drop_shadow                   = Color(0, 100);
    menu_theme->m_window_header_height          = 0;
    menu_theme->m_window_drop_shadow_size       = 50;
    menu_theme->m_button_corner_radius          = 4;
    menu_theme->m_border_light                  = menu_theme->m_transparent;
    menu_theme->m_border_dark                   = menu_theme->m_transparent;
    menu_theme->m_button_gradient_top_focused   = Color(77, 124, 233, 255);
    menu_theme->m_button_gradient_bot_focused   = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_bot_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_unfocused = menu_theme->m_transparent;
    menu_theme->m_button_gradient_bot_unfocused = menu_theme->m_transparent;
    menu_theme->m_window_popup                  = Color(25, 245);
    menu_theme->m_text_color_shadow             = menu_theme->m_transparent;
    set_theme(menu_theme);

    set_layout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 5, 5});

    m_search_box = new SearchBox{this, ""};
    m_search_box->set_editable(true);
    m_search_box->set_alignment(TextBox::Alignment::Left);
    m_search_box->set_placeholder("Filter commands...");
    m_search_box->set_tooltip("Search for commands.");

    // auto well = new Well{this, 3, Color(0, 64), Color(0, 128), Color(5, 255)};
    // auto well = new Well{this};
    auto well = new Well{this, 3, Color(0, 16), Color(0, 32), Color(0, 64)};
    well->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0));

    m_vscroll = new VScrollPanel{well};
    m_vscroll->set_fixed_height(300);

    m_commandlist = new Widget{m_vscroll};
    m_commandlist->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 5));

    {
        auto grid = new Widget{this};
        grid->set_layout(new GridLayout{Orientation::Horizontal, 5, Alignment::Middle});
        new Widget{grid};
        {
            auto group = new Widget{grid};
            group->set_layout(new BoxLayout{Orientation::Horizontal, Alignment::Middle, 5, 0});
            group->add<Label>("navigate ", "sans-bold", 12);
            group->add<Label>("(Up/Down)", "sans", 12);
        }
        {
            auto group = new Widget{grid};
            group->set_layout(new BoxLayout{Orientation::Horizontal, Alignment::Middle, 5, 0});
            group->add<Label>("use", "sans-bold", 12);
            group->add<Label>("(Return)", "sans", 12);
        }
        {
            auto group = new Widget{grid};
            group->set_layout(new BoxLayout{Orientation::Horizontal, Alignment::Middle, 5, 0});
            group->add<Label>("dismiss", "sans-bold", 12);
            group->add<Label>("(Esc)", "sans", 12);
        }
        new Widget{grid};
    }

    auto create_items = [this](const string &pattern)
    {
        spdlog::debug("creating command list for pattern \"{}\"", pattern);
        // first, delete all current items in the commandlist
        spdlog::trace("deleting {} items from commandlist", m_commandlist->child_count());
        while (m_commandlist->child_count()) m_commandlist->remove_child_at(m_commandlist->child_count() - 1);
        spdlog::trace("commandlist now has {} items", m_commandlist->child_count());

        struct ScoredItem
        {
            int original_index;
            int score;
        };

        auto all_items = [this]()
        {
            std::vector<ScoredItem> items;
            for (int i = 0; i < (int)m_original_commands.size(); ++i)
                items.emplace_back(ScoredItem{i, (int)m_original_commands.size() - i});

            std::sort(items.begin(), items.end(),
                      [this](auto &&a, auto &&b)
                      {
                          // for items with the same score, sort alphabetically
                          return m_original_commands[a.original_index]->caption() <
                                 m_original_commands[b.original_index]->caption();
                      });

            return items;
        };

        auto scored_matches = [this](const std::string &pattern)
        {
            std::vector<ScoredItem> matches;
            for (int i = 0; i < (int)m_original_commands.size(); ++i)
            {
                MenuItem *entry = m_original_commands[i];
                int       score = -10000000;
                fts::fuzzy_match(pattern.c_str(), entry->caption().c_str(), score);
                matches.emplace_back(ScoredItem{i, score});
                spdlog::trace("Matching {} score {}", entry->caption().c_str(), score);
            }

            std::sort(matches.begin(), matches.end(),
                      [this](auto &&a, auto &&b)
                      {
                          if (a.score != b.score)
                              return a.score > b.score;

                          // for items with the same score, sort alphabetically
                          return m_original_commands[a.original_index]->caption() <
                                 m_original_commands[b.original_index]->caption();
                      });

            return matches;
        };

        auto scroll_to_ensure_visible = [this](int j)
        {
            spdlog::debug("scroll_to_ensure_visible({})", j);
            if (j >= m_commandlist->child_count())
                return;

            auto item = dynamic_cast<MenuItem *>(m_commandlist->child_at(j));

            spdlog::trace("scroll_to_ensure_visible({}): item {}", j, item->caption());
            spdlog::trace("item->position().y(): {}; m_commandlist->height(): {}; m_vscroll->height(): {}",
                          item->position().y(), m_commandlist->height(), m_vscroll->height());

            // compute visible range
            int visible_top    = -m_commandlist->position().y();
            int visible_bottom = visible_top + m_vscroll->height();
            spdlog::trace("{} : {}", visible_top, visible_bottom);

            int item_top    = item->position().y();
            int item_bottom = item_top + item->height();
            if (item_bottom <= visible_top)
            {
                // item is above the visible region, need to scroll up
                spdlog::trace("item is above the visible region, need to scroll up");
                float new_scroll = item_top / float(m_commandlist->height() - m_vscroll->height());
                spdlog::trace("setting scroll to {}", new_scroll);
                m_vscroll->set_scroll(new_scroll);
            }
            else if (item_top >= visible_bottom)
            {
                // item is below the visible region, need to scroll down
                spdlog::trace("item is below the visible region, need to scroll down");
                float new_scroll = (item_top - m_vscroll->height() + item->height()) /
                                   float(m_commandlist->height() - m_vscroll->height());
                spdlog::trace("setting scroll to {}", new_scroll);
                m_vscroll->set_scroll(new_scroll);
            }
            else
            {
                // item is visible
                spdlog::trace("item is visible");
            }
        };

        auto items = pattern.empty() ? all_items() : scored_matches(pattern);

        m_commands.clear();
        for (size_t i = 0; i < items.size() && items[i].score >= 0; ++i)
        {
            MenuItem *orig_item = m_original_commands[items[i].original_index];
            if (!orig_item->enabled())
                continue;
            size_t j = m_commands.size();
            spdlog::debug("adding item \"{}\" with score {}", orig_item->caption(), items[i].score);
            auto item = new MenuItem{m_commandlist, orig_item->caption(), orig_item->icon(), {orig_item->shortcut(0)}};
            item->set_flags(orig_item->flags());
            item->set_tooltip(orig_item->tooltip());
            item->set_callback(
                [cb = orig_item->callback(), this]()
                {
                    dispose();
                    cb();
                });
            if (orig_item->flags() & Button::ToggleButton || orig_item->flags() & Button::RadioButton)
            {
                item->set_pushed(orig_item->pushed());
                auto tmp = [this, cb = orig_item->change_callback()](bool b)
                {
                    spdlog::debug("change_callback({}) on command", b);
                    dispose();
                    return cb(b);
                };
                if (orig_item->change_callback())
                {
                    spdlog::debug("set_change_callback() on command {}", item->caption());
                    item->set_change_callback(tmp);
                }
                else
                {
                    spdlog::error("No change_callback() on toggle or radio item {}", item->caption());
                }
            }
            item->set_highlight_callback(
                [this, j, scroll_to_ensure_visible](bool b)
                {
                    if (b)
                    {
                        m_current = j;
                        scroll_to_ensure_visible(j);
                    }
                });
            item->set_visible(orig_item->enabled());
            m_commands.push_back(item);
        }

        spdlog::debug("Listed {} matching commands", m_commands.size());

        // highlight the first result
        m_current = next_visible_child(m_commandlist, m_commandlist->child_count() - 1, Forward);
        if (m_current >= 0 && m_current < m_commandlist->child_count())
            dynamic_cast<MenuItem *>(m_commandlist->child_at(m_current))->set_highlighted(true, true, true);

        if (m_commands.empty())
        {
            auto non_found = new MenuItem{m_commandlist, "No matching commands"};
            non_found->set_enabled(false);
            m_commands.push_back(non_found);
        }
    };

    create_items("");

    m_search_box->set_temporary_callback(
        [create_items](const string &pattern)
        {
            create_items(pattern);
            return true;
        });

    auto first_item = dynamic_cast<MenuItem *>(m_commandlist->child_at(0));
    first_item->set_highlighted(true, true, true);

    m_vscroll->set_scroll(0.f);

    set_callback([this](int result) { this->set_visible(result != 0); });

    m_search_box->request_focus();
}

void CommandPalette::update_geometry()
{
    auto ctx = screen()->nvg_context();

    constexpr int window_top = 75;

    auto screen_size       = screen()->size();
    auto vscroll_to_window = size() - m_vscroll->size();

    auto cl_ps = m_commandlist->preferred_size(ctx);

    int min_vscroll_w = std::max(cl_ps.x(), child_at(child_count() - 1)->preferred_size(ctx).x());
    int max_vscroll_w = 500;
    int max_vscroll_h = std::max(60, screen_size.y() - window_top - vscroll_to_window.y() - 50);
    int margin_w      = 60;

    if (screen_size.x() > max_vscroll_w + vscroll_to_window.x() + margin_w)
        m_vscroll->set_fixed_width(max_vscroll_w);
    else if (screen_size.x() < min_vscroll_w + vscroll_to_window.x() + margin_w)
        m_vscroll->set_fixed_width(min_vscroll_w);
    else
        m_vscroll->set_fixed_width(screen_size.x() - vscroll_to_window.x() - margin_w);

    if (cl_ps.y() < max_vscroll_h)
        m_vscroll->set_fixed_height(0);
    else
        m_vscroll->set_fixed_height(max_vscroll_h);

    set_size(preferred_size(ctx));
    set_position({(screen_size.x() - width()) / 2, window_top});
    perform_layout(ctx);
}

void CommandPalette::draw(NVGcontext *ctx)
{
    if (!m_visible)
        return;

    update_geometry();

    int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

    nvgSave(ctx);
    nvgResetScissor(ctx);

    // Draw a drop shadow
    NVGpaint shadow_paint = nvgBoxGradient(ctx, m_pos.x(), m_pos.y() + 0.25 * ds, m_size.x(), m_size.y(), cr * 2,
                                           ds * 2, m_theme->m_drop_shadow, m_theme->m_transparent);

    nvgBeginPath(ctx);
    nvgRect(ctx, m_pos.x() - ds, m_pos.y() - ds + 0.25 * ds, m_size.x() + 2 * ds, m_size.y() + 2 * ds);
    nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr);
    nvgPathWinding(ctx, NVG_HOLE);
    nvgFillPaint(ctx, shadow_paint);
    nvgFill(ctx);

    // Draw window
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr);
    nvgStrokeWidth(ctx, 3.f);
    nvgStrokeColor(ctx, Color(6, 255));
    nvgStroke(ctx);
    nvgStrokeWidth(ctx, 2.f);
    nvgStrokeColor(ctx, Color(89, 255));
    nvgStroke(ctx);
    nvgFillColor(ctx, m_theme->m_window_popup);
    nvgFill(ctx);

    nvgRestore(ctx);

    Widget::draw(ctx);
}

bool CommandPalette::keyboard_event(int key, int scancode, int action, int modifiers)
{
    try
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
                m_current = next_visible_child(m_commandlist, m_current, key == GLFW_KEY_UP ? Backward : Forward);
                auto item = dynamic_cast<MenuItem *>(m_commandlist->child_at(m_current));
                item->set_highlighted(true, true, true);
                return true;
            }
            else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
            {
                auto item = dynamic_cast<MenuItem *>(m_commandlist->child_at(m_current));
                if (item->flags() & Button::NormalButton)
                {
                    if (item->callback())
                        item->callback()();
                }
                else
                {
                    if (item->change_callback())
                    {
                        item->set_pushed(!item->pushed());
                        item->change_callback()(item->pushed());
                    }
                }

                return true;
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("Caught an exception in CommandPalette::keyboard_event(): {}", e.what());
    }

    return false;
}

NAMESPACE_END(nanogui)
