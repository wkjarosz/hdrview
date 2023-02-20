//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#endif

#include "filterablelist.h"
#include "colorslider.h"
#include "hdrimageview.h"
#include "hdrviewscreen.h"
#include "helpwindow.h"
#include "imagebutton.h"
#include "menu.h"
#include "multigraph.h"
#include "timer.h"
#include "well.h"
#include "widgetutils.h"
#include <nanogui/opengl.h>
#include <set>
#include <spdlog/spdlog.h>
#include <tinydir.h>

using namespace nanogui;
using namespace std;

FilterableList::FilterableList(Widget *parent) : Well(parent, 1, Color(150, 32), Color(0, 50))
{
    set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0));
}

bool FilterableList::is_selected(int index) const
{
    return child_at(index)->visible() && dynamic_cast<const ImageButton *>(child_at(index))->is_selected();
}

bool FilterableList::swap_items(int old_index, int new_index)
{
    if (old_index == new_index || !is_valid(old_index) || !is_valid(new_index))
        // invalid item indices and/or do nothing
        return false;

    std::swap(m_children[old_index], m_children[new_index]);

    // with a simple swap, none of the other imagebuttons are affected

    return true;
}

bool FilterableList::move_item_to(int old_index, int new_index)
{
    if (old_index == new_index || !is_valid(old_index) || !is_valid(new_index))
        // invalid item indices and/or do nothing
        return false;

    // auto *item = child_at(old_index);
    // item->inc_ref();
    // remove_child_at(old_index);
    // add_child(new_index, item);
    // item->dec_ref();
    auto *item = m_children[old_index];
    m_children.erase(m_children.begin() + old_index);
    m_children.insert(m_children.begin() + new_index, item);

    // // update all button item ids in between
    // int start_i = std::min(old_index, new_index);
    // int end_i   = std::max(old_index, new_index);

    // // compute visible index of first item
    // int visible_i = 0;
    // for (int i = 0; i < start_i; ++i)
    //     if (nth_item_is_visible(i))
    //         visible_i++;

    // for (int i = start_i; i <= end_i; ++i)
    // {
    //     auto *b = dynamic_cast<ImageButton *>(child_at(i));
    //     if (nth_item_is_visible(i))
    //         b->set_image_id(++visible_i);
    // }

    // helper function to update an item index from before to after the item move
    auto update_index = [old_index, new_index](int i)
    {
        if (i == old_index)
            i = new_index;
        else if (old_index < new_index)
        {
            if (i > old_index && i <= new_index)
                i -= 1;
        }
        else if (old_index > new_index)
        {
            if (i < old_index && i >= new_index)
                i += 1;
        }
        return i;
    };

    m_current   = update_index(m_current);
    m_reference = update_index(m_reference);

    return true;
}

bool FilterableList::bring_item_forward()
{
    int curr = current_index();
    int next = next_visible_item(curr, Forward);

    if (!move_item_to(curr, next))
        return false;

    return true;
}

bool FilterableList::send_item_backward()
{
    int curr = current_index();
    int next = next_visible_item(curr, Backward);

    if (!move_item_to(curr, next))
        return false;

    return true;
}

bool FilterableList::mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers)
{
    // check if we are trying to drag an item button
    if (down)
    {
        auto w = find_widget(p);
        int  i = child_index(w);
        if (i >= 0)
        {
            m_dragged_item       = w;
            m_dragging_item      = true;
            m_dragging_start_pos = p - w->position();
        }
    }
    else
    {
        m_dragging_item = false;
        perform_layout(screen()->nvg_context());
    }

    return Widget::mouse_button_event(p, button, down, modifiers);
}

bool FilterableList::mouse_motion_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                                        int modifiers)
{
    if (m_dragging_item)
    {
        auto find_other = [this](const nanogui::Vector2i &p) -> nanogui::Widget *
        {
            for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
            {
                Widget *child = *it;
                if (child->visible() && child->contains(p - m_pos) && child != m_dragged_item)
                    return contains(p - m_pos) ? child : nullptr;
            }
            return nullptr;
        };

        auto other_item    = find_other(p);
        int  other_index   = child_index(other_item);
        int  dragged_index = child_index(m_dragged_item);
        spdlog::warn("dragging {} over {}", dragged_index, other_index);
        if (other_index >= 0)
        {
            auto pos = other_item->position();
            pos.y() += (dragged_index - other_index) * other_item->size().y();
            other_item->set_position(pos);
            other_item->mouse_enter_event(p, false);

            move_item_to(dragged_index, other_index);
        }

        perform_layout(screen()->nvg_context());
        m_dragged_item->set_position(p - m_dragging_start_pos);
    }

    return Widget::mouse_motion_event(p, rel, button, modifiers);
}

void FilterableList::draw(NVGcontext *ctx)
{
    // // draw selection
    // int   extra_border    = 0;
    // Color reference_color = Color(180, 100, 100, 255);
    // Color selected_color  = Color(77 * 3 / 4, 124 * 3 / 4, 233 * 3 / 4, 255);
    // Color current_color   = Color(77, 124, 233, 255);
    // if (is_valid(m_current))
    // {
    //     auto *item = m_children[m_current];
    //     nvgBeginPath(ctx);
    //     nvgRoundedRect(ctx, item->position().x() + extra_border, item->position().y() + extra_border,
    //                    item->size().x() - 2 * extra_border, item->size().y() - 2 * extra_border, 3);
    //     nvgFillColor(ctx, current_color);
    //     nvgFill(ctx);
    // }

    Well::draw(ctx);
}

bool FilterableList::set_current_index(int index, bool force_callback)
{
    if (index == m_current && !force_callback)
        return false;

    // bool already_selected = false;
    // if (is_valid(index))
    // {
    //     auto btn         = dynamic_cast<ImageButton *>(child_at(index));
    //     already_selected = btn->is_selected();
    //     btn->set_is_current(true);
    // }

    // for (int i = 0; i < child_count(); ++i)
    //     if (i != index)
    //     {
    //         dynamic_cast<ImageButton *>(child_at(i))->set_is_current(false);
    //         // if the item wasn't already selected, deselect others
    //         if (!already_selected)
    //             dynamic_cast<ImageButton *>(child_at(i))->set_is_selected(false);
    //     }

    m_previous = m_current;
    m_current  = index;

    return true;
}

bool FilterableList::select_index(int index)
{
    int num_selected = 0;
    for (int i = 0; i < child_count(); ++i) num_selected += dynamic_cast<ImageButton *>(child_at(i))->is_selected();

    // logic:
    // if index is not selected, then select it
    // if index is already selected, then deselect it (but only if some other item is selected)
    //   if index was also the current item, then need to find a different current item from the selected one

    if (is_valid(index))
    {
        auto btn = dynamic_cast<ImageButton *>(child_at(index));
        if (!btn->is_selected())
            btn->set_is_selected(true);
        else
        {
            if (num_selected > 1)
            {
                btn->set_is_selected(false);

                if (index == m_current)
                {
                    // make one of the other selected items the current item
                    for (int i = 0; i < child_count(); ++i)
                    {
                        if ((int)i == m_current)
                            continue;

                        // just use the first selected item that isn't the current item
                        auto other = dynamic_cast<ImageButton *>(child_at(i));
                        if (other && other->is_selected())
                            index = i;
                    }

                    m_previous = m_current;
                    m_current  = index;
                }
            }
        }
    }

    return true;
}

bool FilterableList::set_reference_index(int index)
{
    if (index == m_reference)
    {
        if (is_valid(m_reference))
        {
            auto btn = dynamic_cast<ImageButton *>(child_at(m_reference));
            btn->set_is_reference(!btn->is_reference());
            if (!btn->is_reference())
                index = -1;
        }
        else
            return false;
    }
    else
    {
        if (is_valid(m_reference))
            dynamic_cast<ImageButton *>(child_at(m_reference))->set_is_reference(false);
        if (is_valid(index))
            dynamic_cast<ImageButton *>(child_at(index))->set_is_reference(true);
    }

    m_reference = index;

    return true;
}

bool FilterableList::remove_current_item()
{
    if (!is_valid(m_current))
        return false;

    // select the next item down the list, or the previous if removing the bottom-most item
    int next = next_visible_item(m_current, Backward);
    if (next < m_current)
        next = next_visible_item(m_current, Forward);

    remove_child_at(m_current);
    int new_size = child_count();

    int new_index = next;
    if (m_current < next)
        new_index--;
    else if (next >= new_size)
        new_index = new_size - 1;

    set_current_index(new_index, true);
    // for now just forget the previous selection when removing any item
    m_previous = -1;
    return true;
}

void FilterableList::clear_items()
{
    for (int i = 0; i < child_count(); ++i) remove_child_at(i);

    m_current   = -1;
    m_reference = -1;
    m_previous  = -1;
}

int FilterableList::next_visible_item(int index, EDirection direction) const
{
    return next_visible_child(this, index, direction);
}

int FilterableList::nth_visible_item_index(int n) const { return nth_visible_child_index(this, n); }

bool FilterableList::nth_item_is_visible(int n) const
{
    return n >= 0 && n < int(children().size()) && children()[n]->visible();
}
