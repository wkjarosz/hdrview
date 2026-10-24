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
    add_mipmap_commands(commands);
    add_filter_commands(commands);
    add_envmap_commands(commands);

    return commands;
}

const std::vector<EditSubmenu> &edit_submenus()
{
    static const std::vector<EditSubmenu> submenus{
        {"Size and orientation",
         {"Flip image horizontally", "Flip image vertically", "Rotate 90 degrees clockwise",
          "Rotate 90 degrees counter-clockwise", "Shift...", "Crop to selection", "Image size...", "Canvas size...",
          "Generate mipmaps..."}},
        {"Tone",
         {"Invert", "Clamp to [0,1]", "Zap gremlins...", "Exposure/gamma...", "Brightness/contrast...", "Fill..."}},
        {"Color",
         {"Convert color space...", "Channel mixer...", "Hue/saturation...", "Flatten...", "Bump to normal map..."}},
        {"Channels", {"Ungroup channels", "Regroup channels", "Delete channel group"}},
        {"Neighborhood filters", {"Blur...", "Unsharp mask...", "Median filter..."}},
        {"Environment maps", {"Remap envmap...", "Irradiance envmap..."}},
    };
    return submenus;
}

const char *edit_submenu_of(std::string_view name)
{
    for (const auto &submenu : edit_submenus())
        for (const char *command : submenu.commands)
            if (name == command)
                return submenu.title;
    return nullptr;
}
