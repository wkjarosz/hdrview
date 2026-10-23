//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "edit/command.h"
#include "fwd.h"

#include <optional>

/// What an edit dialog's preview has the viewport show of the image it would edit.
/**
    The image shader applies it to the image's samples as they are drawn; see edit_preview() in
    assets/shaders/image-shader.sglsl. Built by make_edit_preview(), so that it covers what applying the command would.
*/
struct EditPreview
{
    PointEdit edit;          ///< A point-wise edit, or EDIT_NONE
    int4      slots{-1};     ///< For each shader slot, which of the subject's channels it shows, or -1 for none
    bool      color = false; ///< Whether the group shown is one a color op reaches
    float4    rect{0.f};     ///< The subject's rectangle in the shown textures' uv: min, then max

    std::optional<float3x3>    M_to_sRGB; ///< What the image shows its samples through once the edit retags them
    std::optional<EnvmapRemap> remap;     ///< A remap of the whole image, in place of a point-wise edit
    std::optional<ShiftEdit>   shift;     ///< A shift of the subject's channels, in place of a point-wise edit
    std::optional<Box2i>       window;    ///< The display window of the image shown in place of this one, if reshaped
    EditCommand               *command = nullptr; ///< The command previewed, which a drag over the preview may steer
};

/// The preview of \p cmd as applying it in \p ctx would change its image, or none for a command that has none.
/**
    For a point-wise edit or a shift, the subject's channels among those the shader shows of the image's active group,
    or the group itself if a color op reaches it, within the subject's rectangle, as apply_point_edit() and
    modify_channels_async() cover them.
*/
std::optional<EditPreview> make_edit_preview(EditCommand &cmd, const EditContext &ctx);
