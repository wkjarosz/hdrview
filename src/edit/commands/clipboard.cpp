//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard.cpp
    \author Wojciech Jarosz

    Cut, copy and paste, over a clipboard that holds an image rather than anything the system knows about.
    A pasted region carries its full precision and its channels, which is the point of having it here: the
    system clipboard would flatten an HDR selection to eight-bit RGBA on the way through.
*/

#include "edit/commands.h"

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

namespace
{

//! The rectangle these operate on: the selection when the subject asks for it, else the whole image.
Box2i target_region(const EditContext &ctx)
{
    auto img = ctx.image();
    if (!img)
        return Box2i{};

    Box2i box = img->data_window;
    if (ctx.subject().selection_only && ctx.selection().has_volume())
        box.intersect(ctx.selection());
    return box;
}

class Copy final : public EditCommand
{
public:
    Info info() const override
    {
        Info i{{"Copy", "Copy selection"}, ICON_MY_COPY, ImGuiMod_Ctrl | ImGuiKey_C};
        // Reading an image is not editing it, so this is offered for one whose pixels a renderer owns --
        // taking a copy is in fact how a frame of one is kept.
        i.needs_editable = false;
        return i;
    }

    bool enabled(const EditContext &ctx) const override { return ctx.image() != nullptr; }

    void apply(EditContext &ctx) override
    {
        if (auto img = ctx.image())
            ctx.set_clipboard(img->duplicate(target_region(ctx)));
    }
};

class Cut final : public EditCommand
{
public:
    Info info() const override { return {{"Cut", "Cut selection"}, ICON_MY_CUT, ImGuiMod_Ctrl | ImGuiKey_X}; }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        // Copied before it is cleared, and over exactly the rectangle that is about to be cleared, so the
        // two cannot describe different regions.
        ctx.set_clipboard(img->duplicate(target_region(ctx)));

        // Zero throughout, rather than 1.8's "keep the color and clear the alpha": these samples are held
        // premultiplied, where a color with no alpha is not invisible but additive, and what is left
        // behind would glow.
        ctx.modify_pixels("Cut", [](float, int2, int) { return 0.f; });
    }
};

class Paste final : public EditCommand
{
public:
    Info info() const override { return {{"Paste"}, ICON_MY_PASTE, ImGuiMod_Ctrl | ImGuiKey_V}; }

    bool enabled(const EditContext &ctx) const override { return ctx.clipboard() != nullptr; }

    void apply(EditContext &ctx) override
    {
        auto clip = ctx.clipboard();
        auto img  = ctx.image();
        if (!clip || !img || clip->groups.empty())
            return;

        // Landed at the top-left of what is being pasted into, as 1.8 landed it -- and clipped to that
        // rectangle, so pasting into a selection stays inside it.
        const int2 origin = target_region(ctx).min;

        const auto &group    = clip->groups[size_t(clip->selected_group)];
        const int   n        = group.num_channels;
        const int4  channels = group.channels;
        const int2  extent   = clip->data_window.size();

        ctx.modify_colors("Paste",
                          [clip, channels, n, origin, extent](const float4 &dst, int2 p)
                          {
                              const int2 q = p - origin;
                              if (q.x < 0 || q.y < 0 || q.x >= extent.x || q.y >= extent.y)
                                  return dst; // past the end of what was copied; leave what is there

                              // Read as stored rather than through raw_pixel(), which would divide a straight-alpha
                              // image's color back out and hand back something that is not what will be written.
                              float4 src{0.f, 0.f, 0.f, 1.f};
                              for (int c = 0; c < n; ++c) src[c] = clip->channels[size_t(channels[c])](q);

                              // Source-over on premultiplied values, which is the same expression for color and alpha
                              // alike. A fully opaque source replaces; a transparent one changes nothing.
                              return src + dst * (1.f - src.w);
                          });
    }
};

} // namespace

void add_clipboard_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<Cut>());
    out.push_back(std::make_unique<Copy>());
    out.push_back(std::make_unique<Paste>());
}
