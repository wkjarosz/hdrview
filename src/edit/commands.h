//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "edit/command.h"

#include <vector>

//! Every edit command. Menu order is set in app-gui.cpp, not here.
std::vector<EditCommandPtr> all_edit_commands();

//
// One appender per file under edit/commands/, called by all_edit_commands().
//
void add_clipboard_commands(std::vector<EditCommandPtr> &out);
void add_tonal_commands(std::vector<EditCommandPtr> &out);
void add_transform_commands(std::vector<EditCommandPtr> &out);
void add_color_commands(std::vector<EditCommandPtr> &out);
void add_channel_commands(std::vector<EditCommandPtr> &out);

//! Menu label for deleting \p groups of \p img: one channel, one group or several. The action's name
//! stays fixed, since that is what addresses it.
std::string delete_channels_label(const ImagePtr &img, const std::vector<int> &groups);
void        add_mipmap_commands(std::vector<EditCommandPtr> &out);
void        add_filter_commands(std::vector<EditCommandPtr> &out);
void        add_envmap_commands(std::vector<EditCommandPtr> &out);
