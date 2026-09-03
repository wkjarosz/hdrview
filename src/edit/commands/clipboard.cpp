//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard.cpp
    \author Wojciech Jarosz

    Cut, copy and paste, over a clipboard that holds an image. A pasted region keeps its full precision
    and its channels; the system clipboard would flatten an HDR selection to eight-bit RGBA.
*/

#include "edit/commands.h"

#include "edit/edit_ops.h"
#include "edit/poisson.h"

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <cmath>

namespace
{

/// The rectangle these operate on: the selection when the subject asks for it, else the whole image.
Box2i target_region(const EditContext &ctx)
{
    auto img = ctx.image;
    if (!img)
        return Box2i{};

    Box2i box = img->data_window;
    if (ctx.subject.selection_only && ctx.roi.has_volume())
        box.intersect(ctx.roi);
    return box;
}

class Copy final : public EditCommand
{
public:
    Copy() : EditCommand({{"Copy", "Copy selection"}, ICON_MY_COPY, ImGuiMod_Ctrl | ImGuiKey_C})
    {
        m_info.needs_editable = false; // copying a live image is fine
        m_info.fans_out       = false; // one clipboard
    }

    bool enabled(const EditContext &ctx) const override { return ctx.image && ctx.clipboard; }

    void apply(const EditContext &ctx) override
    {
        if (ctx.image && ctx.clipboard)
            *ctx.clipboard = ctx.image->duplicate(target_region(ctx));
    }
};

class Cut final : public EditCommand
{
public:
    Cut() : EditCommand({{"Cut", "Cut selection"}, ICON_MY_CUT, ImGuiMod_Ctrl | ImGuiKey_X})
    {
        // the copy half cannot fan out; see Copy
        m_info.fans_out = false;
    }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img || !ctx.clipboard)
            return;

        // copied before it is cleared, over the same rectangle
        *ctx.clipboard = img->duplicate(target_region(ctx));

        // zero throughout: these samples are held premultiplied, where a color with no alpha is additive
        // and not invisible
        modify_pixels(ctx, "Cut", [](float, int2, int) { return 0.f; });
    }
};

class Paste final : public EditCommand
{
public:
    Paste() : EditCommand({{"Paste"}, ICON_MY_PASTE, ImGuiMod_Ctrl | ImGuiKey_V}) {}

    bool enabled(const EditContext &ctx) const override { return ctx.clipboard && *ctx.clipboard; }

    void apply(const EditContext &ctx) override
    {
        auto clip = ctx.clipboard ? *ctx.clipboard : nullptr;
        auto img  = ctx.image;
        if (!clip || !img || clip->groups.empty())
            return;

        // landed at the top-left of what is being pasted into, and clipped to it
        const int2 origin = target_region(ctx).min;

        const auto &group    = clip->groups[size_t(clip->selected_group)];
        const int   n        = group.num_channels;
        const int4  channels = group.channels;
        const int2  extent   = clip->data_window.size();

        modify_colors(ctx, "Paste",
                      [clip, channels, n, origin, extent](const float4 &dst, int2 p, int)
                      {
                          const int2 q = p - origin;
                          if (q.x < 0 || q.y < 0 || q.x >= extent.x || q.y >= extent.y)
                              return dst; // past the end of what was copied; leave what is there

                          // read as stored, not through raw_pixel(), which would divide a straight-alpha
                          // image's color back out
                          float4 src{0.f, 0.f, 0.f, 1.f};
                          for (int c = 0; c < n; ++c) src[c] = clip->channels[size_t(channels[c])](q);

                          // source-over on premultiplied values, the same expression for color and alpha
                          return src + dst * (1.f - src.w);
                      });
    }
};

class SeamlessPaste final : public EditCommand
{
public:
    SeamlessPaste() :
        EditCommand({{"Seamless paste...", "Poisson paste", "Gradient-domain paste"},
                     ICON_MY_SEAMLESS_PASTE,
                     ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V,
                     "Paste",
                     27.f})
    {
        m_info.has_dialog = true;
    }

    bool enabled(const EditContext &ctx) const override { return ctx.clipboard && *ctx.clipboard; }

    void draw(EditContext &ctx) override
    {
        ImGui::TextWrapped("Pastes what the clipboard varies like rather than what it is: the border keeps the "
                           "values already there, so no seam can appear, and the interior drifts to meet them "
                           "-- the patch takes on the surrounding illumination.");
        ImGui::Spacing();

        ImGui::SliderInt("Iterations", &m_iterations, 10, 2000);
        ImGui::Tooltip("How long the solver may run. It stops sooner once the answer stops changing, so this "
                       "is a bound rather than a cost.");

        ImGui::Checkbox("Match ratios", &m_match_ratios);
        ImGui::Tooltip("Solve on a compressed scale, so that what is matched across the border is a ratio "
                       "rather than a difference. Usually what an HDR image wants, where the two sides can be "
                       "many stops apart.");

        if (auto clip = ctx.clipboard ? *ctx.clipboard : nullptr)
            ImGui::TextDisabled("Clipboard: %d x %d", clip->size().x, clip->size().y);
    }

    void apply(const EditContext &ctx) override
    {
        auto clip = ctx.clipboard ? *ctx.clipboard : nullptr;
        auto img  = ctx.image;
        if (!clip || !img || clip->groups.empty())
            return;

        const auto &group  = clip->groups[size_t(clip->selected_group)];
        const int4  ch     = group.channels;
        const int   n      = group.num_channels;
        const int2  extent = clip->data_window.size();

        const int  iters        = m_iterations;
        const bool match_ratios = m_match_ratios;

        ctx.modify_channels_async(
            "Seamless paste",
            [clip, ch, n, extent, iters, match_ratios](const Array2Df &dst, const Box2i &region, int slot,
                                                       AtomicProgress p) -> Array2Df
            {
                const int2 size = region.size();

                Array2Df out{size};
                for (int y = 0; y < size.y; ++y)
                    for (int x = 0; x < size.x; ++x) out(x, y) = dst(region.min.x + x, region.min.y + y);

                // only the group's color channels have a counterpart to take gradients from; alpha is the
                // mask
                if (slot >= n || (n >= 4 && slot == 3))
                {
                    // nothing to do, but the bar was promised this channel's share
                    p.finish_share();
                    return out;
                }

                // the clipboard lands at the top-left of the rectangle this filter was handed, so the two
                // share an origin and the solve covers whichever is smaller
                const int2 solve{std::min(extent.x, size.x), std::min(extent.y, size.y)};
                if (solve.x < 3 || solve.y < 3)
                    return out; // no interior to solve for

                Array2Df background{solve}, source{solve}, mask{solve};
                for (int y = 0; y < solve.y; ++y)
                    for (int x = 0; x < solve.x; ++x)
                    {
                        background(x, y) = out(x, y);
                        source(x, y)     = clip->channels[size_t(ch[slot])](int2{x, y});

                        // the clipboard's own alpha is the mask, zeroed along the border, which is what
                        // pins the solve to the background
                        const bool border = x == 0 || y == 0 || x == solve.x - 1 || y == solve.y - 1;
                        mask(x, y)        = border ? 0.f : (n >= 4 ? clip->channels[size_t(ch[3])](int2{x, y}) : 1.f);
                    }

                // solve in asinh, so ratios are matched instead of differences where the two sides can be
                // many stops apart. asinh, not log: it is defined for every sample the image can hold,
                // negative ones included, so the transform does not depend on the content.
                if (match_ratios)
                    for (int i = 0; i < background.num_elements(); ++i)
                    {
                        background(i) = float(axis_scale_fwd(background(i), AxisScale_Asinh));
                        source(i)     = float(axis_scale_fwd(source(i), AxisScale_Asinh));
                    }

                Array2Df solved = poisson_blended(background, source, mask, iters, 1e-4f, p);
                if (p.canceled())
                    return out;

                if (match_ratios)
                    for (int i = 0; i < solved.num_elements(); ++i)
                        solved(i) = float(axis_scale_inv(solved(i), AxisScale_Asinh));

                // outside the mask the solve returns the background it was given
                for (int y = 0; y < solve.y; ++y)
                    for (int x = 0; x < solve.x; ++x) out(x, y) = solved(x, y);

                return out;
            });
    }

private:
    int  m_iterations   = 300;
    bool m_match_ratios = false;
};

} // namespace

void add_clipboard_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<Cut>());
    out.push_back(std::make_unique<Copy>());
    out.push_back(std::make_unique<Paste>());
    out.push_back(std::make_unique<SeamlessPaste>());
}
