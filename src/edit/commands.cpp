//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/commands.h"

std::vector<EditCommandPtr> all_edit_commands()
{
    std::vector<EditCommandPtr> commands;

    add_clipboard_commands(commands);
    add_tonal_commands(commands);
    add_transform_commands(commands);
    add_color_commands(commands);
    add_channel_commands(commands);
    add_filter_commands(commands);
    add_envmap_commands(commands);

    return commands;
}
