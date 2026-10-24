//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "edit/command.h"

#include <string_view>
#include <vector>

/// Every edit command, in the order the action registry and the command palette list them.
std::vector<EditCommandPtr> all_edit_commands();

/// A submenu of the Edit menu's Filters section: its title, and the names of the commands it lists, in order.
struct EditSubmenu
{
    const char               *title;
    std::vector<const char *> commands;
};

/// The Edit menu's Filters submenus, which list every command but the clipboard's, which sit in the menu itself.
const std::vector<EditSubmenu> &edit_submenus();

/// The title of the Edit menu submenu that lists the command named \p name, or null for one in the menu itself.
const char *edit_submenu_of(std::string_view name);

//
// One appender per file under edit/commands/, called by all_edit_commands().
//
void add_clipboard_commands(std::vector<EditCommandPtr> &out);
void add_tonal_commands(std::vector<EditCommandPtr> &out);
void add_transform_commands(std::vector<EditCommandPtr> &out);
void add_color_commands(std::vector<EditCommandPtr> &out);
void add_channel_commands(std::vector<EditCommandPtr> &out);

/// Menu label for deleting \p groups of \p img: one channel, one group or several.
/**
    The action's name stays fixed, since that is what addresses it.
*/
std::string delete_channels_label(const ImagePtr &img, const std::vector<int> &groups);
void        add_mipmap_commands(std::vector<EditCommandPtr> &out);
void        add_filter_commands(std::vector<EditCommandPtr> &out);
void        add_envmap_commands(std::vector<EditCommandPtr> &out);
