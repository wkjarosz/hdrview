//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "edit/command.h"

#include <vector>

/*!
    Every edit command, in the order they are built.

    One table rather than each file registering itself as it is loaded: static initialization runs in an
    order nobody chooses, and a list that can be read start to finish is worth more than the line it saves
    per command. Order here is only construction order -- the Edit menu says where each one appears.
*/
std::vector<EditCommandPtr> all_edit_commands();

//
// One appender per file under edit/commands/, called by all_edit_commands(). A new command is added to
// whichever of these it belongs beside, or to a new file with its own appender named here.
//
void add_clipboard_commands(std::vector<EditCommandPtr> &out);
void add_tonal_commands(std::vector<EditCommandPtr> &out);
void add_transform_commands(std::vector<EditCommandPtr> &out);
void add_color_commands(std::vector<EditCommandPtr> &out);
void add_filter_commands(std::vector<EditCommandPtr> &out);
void add_envmap_commands(std::vector<EditCommandPtr> &out);
