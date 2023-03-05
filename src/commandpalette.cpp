
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

static constexpr int MAX_MATCHES = 256;

NAMESPACE_BEGIN(nanogui)

class CommandPalette::PaletteItem : public MenuItem
{
public:
    PaletteItem(Widget *parent, const std::string &caption = "Untitled", int button_icon = 0,
                const std::vector<Shortcut> &s = {{0, 0}}) :
        MenuItem(parent, caption, button_icon, s)
    {
        // empty
    }

    void set_matches(const uint8_t matches[], int match_count)
    {
        m_match_count = std::min(MAX_MATCHES, match_count);
        memcpy(m_matches, matches, m_match_count);
    }

    virtual void draw(NVGcontext *ctx) override;

protected:
    int     m_match_count          = 0;
    uint8_t m_matches[MAX_MATCHES] = {0};
};

void CommandPalette::PaletteItem::draw(NVGcontext *ctx)
{
    Widget::draw(ctx);

    NVGcolor grad_top = m_theme->m_button_gradient_top_unfocused;
    NVGcolor grad_bot = m_theme->m_button_gradient_bot_unfocused;

    if (m_highlighted)
    {
        grad_top = m_theme->m_button_gradient_top_focused;
        grad_bot = m_theme->m_button_gradient_bot_focused;
    }

    nvgBeginPath(ctx);

    nvgRoundedRect(ctx, m_pos.x() + 1, m_pos.y() + 1.0f, m_size.x() - 2, m_size.y() - 2,
                   m_theme->m_button_corner_radius - 1);

    if (m_background_color.w() != 0)
    {
        nvgFillColor(ctx, Color(m_background_color[0], m_background_color[1], m_background_color[2], 1.f));
        nvgFill(ctx);
        if (m_pushed)
        {
            grad_top.a = grad_bot.a = 0.8f;
        }
        else
        {
            double v   = 1 - m_background_color.w();
            grad_top.a = grad_bot.a = m_enabled ? v : v * .5f + .5f;
        }
    }

    NVGpaint bg = nvgLinearGradient(ctx, m_pos.x(), m_pos.y(), m_pos.x(), m_pos.y() + m_size.y(), grad_top, grad_bot);

    nvgFillPaint(ctx, bg);
    nvgFill(ctx);

    nvgBeginPath(ctx);
    nvgStrokeWidth(ctx, 1.0f);
    nvgRoundedRect(ctx, m_pos.x() + 0.5f, m_pos.y() + (m_pushed ? 0.5f : 1.5f), m_size.x() - 1,
                   m_size.y() - 1 - (m_pushed ? 0.0f : 1.0f), m_theme->m_button_corner_radius);
    nvgStrokeColor(ctx, m_theme->m_border_light);
    nvgStroke(ctx);

    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, m_pos.x() + 0.5f, m_pos.y() + 0.5f, m_size.x() - 1, m_size.y() - 2,
                   m_theme->m_button_corner_radius);
    nvgStrokeColor(ctx, m_theme->m_border_dark);
    nvgStroke(ctx);

    int font_size = m_font_size == -1 ? m_theme->m_button_font_size : m_font_size;
    nvgFontSize(ctx, font_size);
    nvgFontFace(ctx, "sans-bold");

    Vector2f center = Vector2f(m_pos) + Vector2f(m_size) * 0.5f;
    Vector2f text_pos(6, center.y() - 1);
    NVGcolor text_color = m_text_color.w() == 0 ? m_theme->m_text_color : m_text_color;
    if (!m_enabled)
        text_color = m_theme->m_disabled_text_color;

    auto  icon = m_icon && !m_pushed ? utf8(m_icon) : utf8(FA_CHECK);
    float ih   = font_size * icon_scale();
    nvgFontSize(ctx, ih);
    nvgFontFace(ctx, "icons");
    float iw = nvgTextBounds(ctx, 0, 0, icon.data(), nullptr, nullptr);

    if (m_caption != "")
        ih += m_size.y() * 0.15f;

    nvgFillColor(ctx, text_color);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    Vector2f icon_pos(m_pos.x() + 6, center.y() - 1);

    text_pos.x() = icon_pos.x() + ih + 2;

    if (m_pushed || m_icon)
        nvgText(ctx, icon_pos.x() + (ih - iw - 3) / 2, icon_pos.y() + 1, icon.data(), nullptr);

    auto draw_shadowed_text =
        [this, &text_color, ctx, &text_pos](bool highlighted, const char *start, const char *end = nullptr)
    {
        nvgFontFace(ctx, highlighted ? "sans-bold" : "sans");
        nvgFillColor(ctx, m_theme->m_text_color_shadow);
        nvgText(ctx, text_pos.x(), text_pos.y(), start, end);
        nvgFillColor(ctx, highlighted ? Color(1.f, 1.f, 1.f, 1.f) : text_color);
        return nvgText(ctx, text_pos.x(), text_pos.y() + 1, start, end);
    };

    // The following code that performs the substring highlighting logic is adapted from ImGui Command Palette
    // The MIT License (MIT)
    //
    // Copyright (c) 2021 hnOsmium0001
    //
    // Permission is hereby granted, free of charge, to any person obtaining a copy
    // of this software and associated documentation files (the "Software"), to deal
    // in the Software without restriction, including without limitation the rights
    // to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    // copies of the Software, and to permit persons to whom the Software is
    // furnished to do so, subject to the following conditions:
    //
    // The above copyright notice and this permission notice shall be included in all
    // copies or substantial portions of the Software.
    //
    // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    // IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    // FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    // AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    // LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    // OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    // SOFTWARE.

    nvgFontSize(ctx, font_size);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    if (m_match_count <= 0)
        // draw text as-is, without highlighting matches
        draw_shadowed_text(false, m_caption.c_str());
    else
    {
        auto text = m_caption.c_str();
        int  range_begin;
        int  range_end;
        int  last_range_end = 0;

        auto draw_current_range = [&]()
        {
            if (range_begin != last_range_end)
                // Draw normal text between last highlighted range end and current highlighted range start
                text_pos.x() = draw_shadowed_text(false, text + last_range_end, text + range_begin);

            text_pos.x() = draw_shadowed_text(true, text + range_begin, text + range_end);
        };

        range_begin = m_matches[0];
        range_end   = range_begin;

        int last_char_idx = -1;
        for (int j = 0; j < m_match_count; ++j)
        {
            int char_idx = m_matches[j];

            if (char_idx == last_char_idx + 1)
                // These 2 indices are equal, extend our current range by 1
                ++range_end;
            else
            {
                draw_current_range();
                last_range_end = range_end;
                range_begin    = char_idx;
                range_end      = char_idx + 1;
            }

            last_char_idx = char_idx;
        }

        // Draw the remaining range (if any)
        if (range_begin != range_end)
            draw_current_range();

        // Draw the text after the last range (if any)
        draw_shadowed_text(false, text + range_end);
    }

    if (!shortcut().text.size())
        return;

    // float    sw = nvgTextBounds(ctx, 0, 0, shortcut().text.c_str(), nullptr, nullptr);
    Vector2f hotkey_pos(m_pos.x() + m_size.x() - 8, center.y() - 1);

    nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, m_theme->m_text_color_shadow);
    nvgText(ctx, hotkey_pos.x(), hotkey_pos.y(), shortcut().text.c_str(), nullptr);
    nvgFillColor(ctx, m_theme->m_disabled_text_color);
    nvgText(ctx, hotkey_pos.x(), hotkey_pos.y() + 1, shortcut().text.c_str(), nullptr);
}

CommandPalette::CommandPalette(Widget *parent, const std::vector<MenuItem *> &commands) :
    Dialog(parent, "", false), m_original_commands(commands)
{
    auto menu_theme                             = new Theme(screen()->nvg_context());
    menu_theme->m_standard_font_size            = 16;
    menu_theme->m_button_font_size              = 15;
    menu_theme->m_text_box_font_size            = 18;
    menu_theme->m_window_corner_radius          = 8;
    menu_theme->m_drop_shadow                   = Color(0, 150);
    menu_theme->m_button_corner_radius          = 4;
    menu_theme->m_border_light                  = menu_theme->m_transparent;
    menu_theme->m_border_dark                   = menu_theme->m_transparent;
    menu_theme->m_button_gradient_top_focused   = Color(77, 124, 233, 255);
    menu_theme->m_button_gradient_bot_focused   = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_bot_pushed    = menu_theme->m_button_gradient_top_focused;
    menu_theme->m_button_gradient_top_unfocused = menu_theme->m_transparent;
    menu_theme->m_button_gradient_bot_unfocused = menu_theme->m_transparent;
    menu_theme->m_text_color_shadow             = menu_theme->m_transparent;
    set_theme(menu_theme);

    set_layout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 5, 5});

    m_search_box = new SearchBox{this, ""};
    m_search_box->set_editable(true);
    m_search_box->set_alignment(TextBox::Alignment::Left);
    m_search_box->set_placeholder("Filter commands...");
    m_search_box->set_tooltip("Search for commands.");

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
            int     original_index       = 0;
            int     score                = 0;
            int     match_count          = 0;
            uint8_t matches[MAX_MATCHES] = {0};
        };

        auto all_items = [this]()
        {
            std::vector<ScoredItem> items;
            for (int i = 0; i < (int)m_original_commands.size(); ++i)
            {
                MenuItem *entry = m_original_commands[i];
                if (!entry->enabled())
                    continue;

                items.emplace_back(ScoredItem{i, (int)m_original_commands.size() - i});
            }

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
                if (!entry->enabled())
                    continue;

                ScoredItem result;
                if (fts::fuzzy_match(pattern.c_str(), entry->caption().c_str(), result.score, result.matches,
                                     MAX_MATCHES, result.match_count))
                {
                    result.original_index = i;
                    matches.push_back(result);

                    spdlog::trace("Matched \"{}\" with score {} and {} matches", entry->caption().c_str(), result.score,
                                  result.match_count);
                    for (int j = 0; j < result.match_count; ++j) spdlog::trace("   {}", result.matches[j]);
                }
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
        for (const auto &i : items)
        {
            MenuItem *orig_item = m_original_commands[i.original_index];
            if (!orig_item->enabled())
                continue;
            size_t j = m_commands.size();
            spdlog::debug("adding item \"{}\" with score {}", orig_item->caption(), i.score);
            auto item =
                new PaletteItem{m_commandlist, orig_item->caption(), orig_item->icon(), {orig_item->shortcut(0)}};
            item->set_matches(i.matches, i.match_count);
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
            auto non_found = new PaletteItem{m_commandlist, "No matching commands"};
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

    // this is needed so that the sizes of the widgets are already computed upon first draw
    update_geometry();

    m_search_box->request_focus();
}

void CommandPalette::update_geometry()
{
    auto ctx = screen()->nvg_context();

    constexpr int window_top = 75;

    auto screen_size       = screen()->size();
    auto vscroll_to_window = size() - m_vscroll->size();

    auto commandlist_size = m_commandlist->preferred_size(ctx);

    // vscroll needs to be wide enough for the commandlist and the last widget which contains keyboard navigation help
    int           min_vscroll_w = std::max(commandlist_size.x(), child_at(child_count() - 1)->preferred_size(ctx).x());
    constexpr int max_vscroll_w = 500;
    int           max_vscroll_h = std::max(60, screen_size.y() - window_top - vscroll_to_window.y() - 50);
    constexpr int margin_w      = 60;

    // if the screen is large enough, draw the command palette at the max width and include a margin
    if (screen_size.x() > max_vscroll_w + vscroll_to_window.x() + margin_w)
        m_vscroll->set_fixed_width(max_vscroll_w);
    // if the screen is not quite large enough, still include a margin but shrink the width of the command list
    else if (screen_size.x() > min_vscroll_w + vscroll_to_window.x() + margin_w)
        m_vscroll->set_fixed_width(screen_size.x() - vscroll_to_window.x() - margin_w);
    // if the command palette can't even fit on screen, don't use a margin and just draw the palette at its minimum
    // width
    else if (screen_size.x() < min_vscroll_w + vscroll_to_window.x() + margin_w)
        m_vscroll->set_fixed_width(min_vscroll_w);

    // if we can fit the entire command list on screen, then have it figure out its preferred height
    if (commandlist_size.y() < max_vscroll_h)
        m_vscroll->set_fixed_height(0);
    // otherwise, use the calculated maximum height and include a vertical scrollbar
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
    {
        nvgResetScissor(ctx);

        // Fade everything else on the screen
        nvgBeginPath(ctx);
        nvgRect(ctx, 0, 0, screen()->width(), screen()->height());
        nvgFillColor(ctx, m_theme->m_drop_shadow);
        nvgFill(ctx);

        // Draw a drop shadow
        nvgBeginPath(ctx);
        nvgRect(ctx, m_pos.x() - ds, m_pos.y() - ds + 0.25 * ds, m_size.x() + 2 * ds, m_size.y() + 2 * ds);
        nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr);
        nvgPathWinding(ctx, NVG_HOLE);
        nvgFillPaint(ctx, nvgBoxGradient(ctx, m_pos.x(), m_pos.y() + 0.25 * ds, m_size.x(), m_size.y(), cr * 2, ds * 2,
                                         m_theme->m_drop_shadow, m_theme->m_transparent));
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
    }
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
