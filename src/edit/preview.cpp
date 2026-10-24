//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/preview.h"

#include "edit/edit_ops.h"
#include "edit/subject.h"
#include "image.h"
#include "shared/edits.h"
#include "shared/envmap.h"

#include <algorithm>

std::optional<EditPreview> make_edit_preview(EditCommand &cmd, const EditContext &ctx)
{
    if (!ctx.image)
        return std::nullopt;

    EditPreview p;
    p.command = &cmd;
    p.image   = ctx.image;
    p.remap   = cmd.remap_preview(ctx);
    // a remap's or an irradiance map's result keeps the data window's corner, as resample_image_async() leaves it
    if (p.remap)
        p.window = Box2i{ctx.image->data_window.min, ctx.image->data_window.min + p.remap->size};
    p.irradiance = cmd.irradiance_preview(ctx);
    if (p.irradiance)
        p.window = Box2i{ctx.image->data_window.min, ctx.image->data_window.min + p.irradiance->size};
    p.shift = cmd.shift_preview(ctx);
    p.bump  = cmd.bump_preview(ctx);
    if (const auto edit = cmd.point_edit(ctx))
        p.edit = *edit;
    else if (!p.remap && !p.irradiance && !p.shift && !p.bump)
        return std::nullopt;
    p.M_to_sRGB = cmd.M_to_sRGB_after(ctx);

    // What applying would cover of the group the viewport shows: for a per-sample op or a shift (whose op is
    // EDIT_NONE), the subject's channels among those bound to the shader's slots; for a color op or a bump map, which
    // writes whole color groups, the group, if the subject includes it. Within the subject's rectangle, either way.
    const Image &img             = *ctx.image;
    const int    shown           = img.active_group_index(Target_Primary);
    const auto [subject, bounds] = resolve_subject(ctx.image, ctx.subject, ctx.roi);
    if (p.edit.op < shader::EDIT_FIRST_COLOR && !p.bump)
    {
        const int4 slots = img.shader_slots(Target_Primary);
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

    // the rectangle in the uv the channel textures span
    const float2 size = float2(img.data_window.size());
    const float2 lo   = float2(bounds.min - img.data_window.min) / size,
                 hi   = float2(bounds.max - img.data_window.min) / size;
    p.rect            = float4{lo.x, lo.y, hi.x, hi.y};
    return p;
}

namespace
{
/// \p M in the upper left of a 4x4 matrix, as the shader's mat4 uniforms take a mat3.
float4x4 padded(const float3x3 &M) { return float4x4{{M[0], 0.f}, {M[1], 0.f}, {M[2], 0.f}, {0.f, 0.f, 0.f, 1.f}}; }
} // namespace

ShaderEdit shader_edit(const EditPreview &preview)
{
    // a remap or an irradiance map replaces the whole image, so it comes first, as in the shader
    ShaderEdit s;
    if (const auto &e = preview.irradiance)
    {
        const auto &c = e->coefficients;
        s.op          = shader::EDIT_IRRADIANCE;
        s.m0          = float4x4{c[0], c[1], c[2], c[3]};
        s.m1          = float4x4{c[4], c[5], c[6], c[7]};
        s.p0          = c[8];
        s.p1          = float4{float2(e->size), 0.f, 0.f};
        s.i0          = int4{e->mapping, e->shown, 0, 0};
    }
    else if (const auto &r = preview.remap)
    {
        s.op = shader::EDIT_REMAP;
        // what spreads the supersampling set, worked out once here rather than at every pixel in the shader
        const float spread = r->sampling == shader::ENVMAP_SAMPLE_SUPERSAMPLE
                                 ? shader::envmap_gaussian_scale(r->quality, std::max(r->sigma, 0.f))
                                 : 0.f;
        s.p0               = float4{float2(r->size), spread, 0.f};
        s.m0 = padded(r->rotation);
        s.i0 = int4{r->src, r->dst, r->sampling, r->quality};
    }
    else if (const auto &shift = preview.shift)
    {
        s.op = shader::EDIT_SHIFT;
        s.p0 = float4{shift->offset, 0.f, 0.f};
        s.i0 = int4{shift->sampler, shift->borders.x, shift->borders.y, 0};
    }
    else if (const auto &bump = preview.bump)
    {
        s.op = shader::EDIT_BUMP_NORMAL;
        s.p0 = float4{bump->scale, 0.f, 0.f};
        s.i0 = int4{bump->borders, 0, 0};
    }
    else
    {
        const PointEdit &e = preview.edit;
        s                  = ShaderEdit{e.op, e.p0, e.p1, padded(e.m0), padded(e.m1)};
    }
    return s;
}
