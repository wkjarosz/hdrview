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

#include "edit/poisson.h"

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <cmath>

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

        // Zero throughout, rather than keeping the color and clearing only the alpha: these samples are
        // held premultiplied, where a color with no alpha is not invisible but additive, and what is left
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

        // Landed at the top-left of what is being pasted into, and clipped to that rectangle, so pasting
        // into a selection stays inside it.
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

class SeamlessPaste final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Seamless paste...", "Poisson paste", "Gradient-domain paste"},
                ICON_MY_SEAMLESS_PASTE,
                ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V,
                ImGuiInputFlags_None,
                "Paste",
                27.f};
    }

    bool enabled(const EditContext &ctx) const override { return ctx.clipboard() != nullptr; }

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

        if (auto clip = ctx.clipboard())
            ImGui::TextDisabled("Clipboard: %d x %d", clip->size().x, clip->size().y);
    }

    void apply(EditContext &ctx) override
    {
        auto clip = ctx.clipboard();
        auto img  = ctx.image();
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

                // Only the group's color channels have a counterpart to take gradients from. Alpha is the
                // mask rather than something to solve for, and anything else the subject covers is not
                // this clipboard's business.
                if (slot >= n || (n >= 4 && slot == 3))
                    return out;

                // The clipboard lands at the top-left of the rectangle being pasted into, which is the
                // rectangle this filter was handed -- so the two share an origin and the solve is over
                // whichever of them is smaller.
                const int2 solve{std::min(extent.x, size.x), std::min(extent.y, size.y)};
                if (solve.x < 3 || solve.y < 3)
                    return out; // no interior to solve for

                Array2Df background{solve}, source{solve}, mask{solve};
                for (int y = 0; y < solve.y; ++y)
                    for (int x = 0; x < solve.x; ++x)
                    {
                        background(x, y) = out(x, y);
                        source(x, y)     = clip->channels[size_t(ch[slot])](int2{x, y});

                        // The clipboard's own alpha is the mask, which is what gives a copied selection
                        // its shape -- and it is zeroed along the border, since that is what pins the
                        // solve to the background. Without a boundary condition nothing holds it in place.
                        const bool border = x == 0 || y == 0 || x == solve.x - 1 || y == solve.y - 1;
                        mask(x, y)        = border ? 0.f : (n >= 4 ? clip->channels[size_t(ch[3])](int2{x, y}) : 1.f);
                    }

                // Matching ratios rather than differences, which is what an HDR image wants where the two
                // sides can be many stops apart. Through asinh rather than a logarithm: it is defined for
                // every sample the image can hold, negative ones included, and is logarithmic only above
                // its knee. A logarithm would have to be shifted up past the darkest sample first, which
                // makes the transform depend on the content -- so the same patch blended into two
                // backgrounds would come out differently.
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

                // Everywhere the solve covered: outside the mask it returns the background it was given,
                // so there is nothing to write around.
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
