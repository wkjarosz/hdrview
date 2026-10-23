//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/preview.h"

#include "edit/subject.h"
#include "image.h"
#include "shared/edits.h"

#include <algorithm>

std::optional<EditPreview> make_edit_preview(EditCommand &cmd, const EditContext &ctx)
{
    if (!ctx.image)
        return std::nullopt;

    EditPreview p;
    p.command = &cmd;
    p.remap   = cmd.remap_preview(ctx);
    // a remap's result keeps the data window's corner, as resample_image_async() leaves it
    if (p.remap)
        p.window = Box2i{ctx.image->data_window.min, ctx.image->data_window.min + p.remap->size};
    if (const auto edit = cmd.point_edit(ctx))
        p.edit = *edit;
    else if (!p.remap)
        return std::nullopt;
    p.M_to_sRGB = cmd.M_to_sRGB_after(ctx);

    // what apply_point_edit() would cover of the group the viewport shows: for a per-sample op, the subject's
    // channels among those bound to the shader's slots, and for a color op, the group if it is one of the subject's
    const Image &img   = *ctx.image;
    const int    shown = img.active_group_index(Target_Primary);
    if (p.edit.op < shader::EDIT_FIRST_COLOR)
    {
        const auto subject = subject_channels(img, ctx.subject);
        const int4 slots   = img.shader_slots(Target_Primary);
        for (int k = 0; k < 4; ++k)
            if (slots[k] >= 0)
                if (auto it = std::find(subject.begin(), subject.end(), img.groups[shown].channels[slots[k]]);
                    it != subject.end())
                    p.slots[k] = int(it - subject.begin());
    }
    else
    {
        const auto groups = subject_color_groups(img, ctx.subject).first;
        p.color           = std::find(groups.begin(), groups.end(), shown) != groups.end();
    }

    // the subject's rectangle, as resolve_subject() bounds it, in the uv the channel textures span
    Box2i bounds = img.data_window;
    if (ctx.subject.selection_only && ctx.roi.has_volume())
        bounds.intersect(ctx.roi);
    const float2 size = float2(img.data_window.size());
    const float2 lo   = float2(bounds.min - img.data_window.min) / size,
                 hi   = float2(bounds.max - img.data_window.min) / size;
    p.rect            = float4{lo.x, lo.y, hi.x, hi.y};
    return p;
}
