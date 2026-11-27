//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file clipboard.cpp
    \author Wojciech Jarosz

    Cut, copy and paste, of the visible channel group, over a clipboard that holds one. A pasted region keeps its full
    precision, and maps into the visible group by what its channels mean. The application also puts each copy on the
    system clipboard, in encodings that keep HDR, and fills this clipboard from the system's when another application
    has put an image there since.
*/

#include "edit/commands.h"

#include "colorspace.h"
#include "edit/edit_ops.h"
#include "edit/poisson.h"

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <array>
#include <cmath>
#include <memory>
#include <optional>

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

Chromaticities chromaticities_of(const Image &img) { return img.chromaticities.value_or(Chromaticities{}); }

/// \p img's primaries and white point, by name where they have one.
std::string color_space_name(const Image &img)
{
    const auto chr = chromaticities_of(img);
    return fmt::format("{} primaries, {} white point", color_gamut_name(named_color_gamut(chr)),
                       white_point_name(named_white_point(chr.white)));
}

/// Whether a group of \p type holds colors, grays included, as opposed to depth, motion vectors and the like.
bool is_color(ChannelGroup::Type type) { return group_is_rgb(type) || group_is_gray(type); }

/// The two channel groups a paste maps between: the clipboard's visible one and the image's.
struct Pairing
{
    ConstImagePtr       clip;
    ConstImagePtr       image;
    const ChannelGroup *from = nullptr;
    const ChannelGroup *to   = nullptr;

    /// Whether both hold colors, grays included, which map by what their channels mean.
    bool colors() const { return is_color(from->type) && is_color(to->type); }
    /// Whether both are the same kind of group, depth into depth say, which map channel for channel.
    bool alike() const { return from->type == to->type && from->num_channels == to->num_channels; }
    /// Whether the clipboard holds RGB colors in another color space than the image's; a gray is neutral in any.
    bool spaces_differ() const
    {
        float3x3 M;
        return colors() && group_is_rgb(from->type) &&
               color_conversion_matrix(M, chromaticities_of(*clip), chromaticities_of(*image));
    }
    /// Whether RGB colors go into a gray, which a paste has to reduce to one value.
    bool color_into_gray() const { return group_is_rgb(from->type) && group_is_gray(to->type); }
};

std::optional<Pairing> pairing(const EditContext &ctx)
{
    ConstImagePtr clip = ctx.clipboard ? *ctx.clipboard : nullptr;
    if (!clip || !ctx.image || !clip->is_valid_group(clip->selected_group) ||
        !ctx.image->is_valid_group(ctx.image->selected_group))
        return std::nullopt;
    return Pairing{clip, ctx.image, &clip->groups[size_t(clip->selected_group)],
                   &ctx.image->groups[size_t(ctx.image->selected_group)]};
}

/// How a paste lays the clipboard's visible channel group into the image's, asking where it has to choose.
/**
    Colors map by meaning: a gray spreads to red, green and blue, and alpha goes to alpha. Groups of the same kind map
    channel for channel. A paste asks only what that leaves open: whether to convert colors from another color space,
    as Photoshop's Paste Profile Mismatch dialog does, how to reduce colors to a gray, and, where either group is not a
    color and the two differ, which channel each of the image's takes.
*/
class PasteMapping
{
public:
    /// The clipboard laid out as the image's visible group: a plane per channel, and the alpha it composites by.
    struct Planes
    {
        std::vector<Array2Df> slots;
        Array2Df              alpha; ///< 1 where either group is not a color, so the paste replaces what is there
    };

    /// Whether the paste has anything to ask: a color space to convert from, colors to reduce, or channels to choose.
    static bool asks(const EditContext &ctx)
    {
        const auto p = pairing(ctx);
        if (!p)
            return false;
        return p->colors() ? p->spaces_differ() || p->color_into_gray() : !p->alike();
    }

    void on_open(const EditContext &ctx)
    {
        m_convert = true;
        if (ctx.image)
            m_method = ctx.image->adaptation_method;
        // the defaults: in order, or a lone channel into each, with 0 for the rest
        if (const auto p = pairing(ctx))
            for (int k = 0; k < 4; ++k)
                m_sources[size_t(k)] = p->from->num_channels == 1 ? 0 : (k < p->from->num_channels ? k : k_zero);
    }

    void draw(const EditContext &ctx)
    {
        const auto p = pairing(ctx);
        if (!p || !asks(ctx))
            return;

        if (!p->colors())
        {
            ImGui::SeparatorText("Channels");
            ImGui::TextWrappedToWidth("The clipboard's channels don't match this image's. Choose which clipboard "
                                      "channel goes into each of the image's channels below.");
            for (int k = 0; k < p->to->num_channels; ++k)
            {
                const std::string &name   = p->image->channels[size_t(p->to->channels[k])].name;
                const int          source = m_sources[size_t(k)];
                auto source_name = [&](int s)
                {
                    return s == k_zero ? std::string{"0"}
                           : s == k_one ? std::string{"1"}
                                        : p->clip->channels[size_t(p->from->channels[s])].name;
                };
                if (ImGui::BeginCombo(name.c_str(), source_name(source).c_str()))
                {
                    for (int s = 0; s < p->from->num_channels; ++s)
                        if (ImGui::Selectable(source_name(s).c_str(), source == s))
                            m_sources[size_t(k)] = s;
                    for (int s : {k_zero, k_one})
                        if (ImGui::Selectable(source_name(s).c_str(), source == s))
                            m_sources[size_t(k)] = s;
                    ImGui::EndCombo();
                }
            }
            return;
        }

        const bool differ = p->spaces_differ();
        if (differ)
        {
            ImGui::SeparatorText("Color space mismatch");
            ImGui::TextWrappedToWidth("The clipboard's colors are in a different color space than this image.");
            ImGui::TextDisabled("Clipboard: %s", color_space_name(*p->clip).c_str());
            ImGui::TextDisabled("Image: %s", color_space_name(*p->image).c_str());
        }
        else
        {
            ImGui::SeparatorText("Color to gray");
            ImGui::TextWrappedToWidth("The clipboard holds colors, and this channel group is gray.");
        }

        if (ImGui::RadioButton("Convert", m_convert))
            m_convert = true;
        ImGui::Tooltip(p->color_into_gray()
                           ? "Paste the luminance of the clipboard's colors, computed from its primaries."
                           : "Convert the clipboard's colors into this image's color space.");
        ImGui::SameLine();
        if (ImGui::RadioButton("Don't convert", !m_convert))
            m_convert = false;
        ImGui::Tooltip(p->color_into_gray()
                           ? "Paste the average of red, green and blue."
                           : "Paste the clipboard's values as they are, which this image then reads in its own "
                             "color space.");

        if (differ)
        {
            ImGui::BeginDisabled(!m_convert);
            if (ImGui::BeginCombo("Adaptation", adaptation_method_name(m_method)))
            {
                for (AdaptationMethod n = 0; n < AdaptationMethod_COUNT; ++n)
                    if (ImGui::Selectable(adaptation_method_name(n), m_method == n))
                        m_method = n;
                ImGui::EndCombo();
            }
            ImGui::Tooltip("How a change of white point is accounted for.");
            ImGui::EndDisabled();
        }
    }

    /// The clipboard, mapped as chosen into the layout of the image's visible group, or nothing to paste.
    std::shared_ptr<Planes> planes(const EditContext &ctx) const
    {
        const auto p = pairing(ctx);
        if (!p)
            return nullptr;

        const int2 size   = p->clip->size();
        const int  n_from = p->from->num_channels;
        const int  n_to   = p->to->num_channels;
        auto       out    = std::make_shared<Planes>();
        out->slots.assign(size_t(n_to), Array2Df{size});
        out->alpha = Array2Df{size};

        float3x3     M{la::identity};
        const bool   convert =
            m_convert && p->spaces_differ() &&
            color_conversion_matrix(M, chromaticities_of(*p->clip), chromaticities_of(*p->image), m_method);
        const float3 Yw      = p->image->luminance_weights;

        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                // read as stored, not through raw_pixel(), which would divide a straight-alpha image's color back out
                float4 raw{0.f};
                for (int c = 0; c < n_from; ++c) raw[c] = p->clip->channels[size_t(p->from->channels[c])](x, y);

                if (!p->colors())
                {
                    for (int k = 0; k < n_to; ++k)
                    {
                        const int s                    = m_sources[size_t(k)];
                        out->slots[size_t(k)](x, y) = s == k_one ? 1.f : (s >= 0 && s < n_from ? raw[s] : 0.f);
                    }
                    out->alpha(x, y) = 1.f;
                    continue;
                }

                float3      rgb = group_is_gray(p->from->type) ? float3{raw[0]} : raw.xyz();
                const float a   = group_has_alpha(p->from->type) ? raw[n_from - 1] : 1.f;
                if (convert)
                    rgb = mul(M, rgb);

                if (group_is_gray(p->to->type))
                    out->slots[0](x, y) = !p->color_into_gray() ? rgb.x
                                          : m_convert            ? dot(Yw, rgb)
                                                                 : (rgb.x + rgb.y + rgb.z) / 3.f;
                else
                    for (int c = 0; c < 3; ++c) out->slots[size_t(c)](x, y) = rgb[c];
                if (group_has_alpha(p->to->type))
                    out->slots[size_t(n_to - 1)](x, y) = a;
                out->alpha(x, y) = a;
            }
        return out;
    }

private:
    static constexpr int k_zero = -1, k_one = -2; ///< A constant in place of one of the clipboard's channels

    bool               m_convert = true;
    AdaptationMethod   m_method  = AdaptationMethod_Bradford;
    /// Unless both groups are colors, the clipboard channel each of the image's takes.
    std::array<int, 4> m_sources{0, 1, 2, 3};
};

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
            *ctx.clipboard = ctx.image->duplicate(target_region(ctx), true);
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
        *ctx.clipboard = img->duplicate(target_region(ctx), true);

        // zero throughout: these samples are held premultiplied, where a color with no alpha is additive
        // and not invisible
        modify_group(ctx, "Cut", [](const float4 &, int2) { return float4{0.f}; });
    }
};

class Paste final : public EditCommand
{
public:
    Paste() : EditCommand({{"Paste"}, ICON_MY_PASTE, ImGuiMod_Ctrl | ImGuiKey_V})
    {
        m_info.reads_clipboard        = true;
        m_info.may_ask                = true; // how to map the clipboard's channels, where that is a choice
        m_info.confirm                = "Paste";
        m_info.draws_subject_selector = false; // into the visible group, whatever Apply to says
    }

    bool enabled(const EditContext &ctx) const override { return ctx.clipboard && *ctx.clipboard; }
    bool asks(const EditContext &ctx) const override { return PasteMapping::asks(ctx); }
    void on_open(EditContext &ctx) override { m_mapping.on_open(ctx); }
    void draw(EditContext &ctx) override { m_mapping.draw(ctx); }

    void apply(const EditContext &ctx) override
    {
        // the defaults, when it did not ask
        if (!PasteMapping::asks(ctx))
            m_mapping.on_open(ctx);
        const auto planes = m_mapping.planes(ctx);
        if (!planes)
            return;

        // landed at the top-left of what is being pasted into, and clipped to it
        const int2 origin = target_region(ctx).min;
        const int2 extent = planes->alpha.size();
        modify_group(ctx, "Paste",
                     [planes, origin, extent](const float4 &dst, int2 p)
                     {
                         const int2 q = p - origin;
                         if (q.x < 0 || q.y < 0 || q.x >= extent.x || q.y >= extent.y)
                             return dst; // past the end of what was copied; leave what is there

                         // source-over on premultiplied values, the same expression for color and alpha
                         const float a   = planes->alpha(q);
                         float4      out = dst;
                         for (size_t k = 0; k < planes->slots.size(); ++k)
                             out[int(k)] = planes->slots[k](q) + dst[int(k)] * (1.f - a);
                         return out;
                     });
    }

private:
    PasteMapping m_mapping;
};

class SeamlessPaste final : public EditCommand
{
public:
    SeamlessPaste() :
        EditCommand({{"Seamless paste...", "Poisson paste", "Gradient-domain paste"},
                     ICON_MY_SEAMLESS_PASTE,
                     ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V,
                     true,
                     "Paste"})
    {
        m_info.reads_clipboard        = true;
        m_info.draws_subject_selector = false; // into the visible group, as Paste
    }

    bool enabled(const EditContext &ctx) const override { return ctx.clipboard && *ctx.clipboard; }

    void draw(EditContext &ctx) override
    {
        ImGui::TextWrappedToWidth("Pastes what the clipboard varies like rather than what it is: the border keeps the "
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

        m_mapping.draw(ctx);
    }

    void on_open(EditContext &ctx) override { m_mapping.on_open(ctx); }

    void apply(const EditContext &ctx) override
    {
        auto       img    = ctx.image;
        const auto planes = m_mapping.planes(ctx);
        if (!img || !planes)
            return;

        // The worker is handed each channel the subject covers; only the visible group's are pasted into, and of
        // those not its alpha, which is the mask.
        const auto          covered = subject_channels(*img, ctx.subject);
        const ChannelGroup &group   = img->groups[size_t(img->selected_group)];
        std::vector<int>    slot_of(covered.size(), -1); ///< Each covered channel's place in the visible group
        for (size_t i = 0; i < covered.size(); ++i)
            for (int k = 0; k < group.num_channels; ++k)
                if (covered[i] == group.channels[k] && !(group_has_alpha(group.type) && k == group.num_channels - 1))
                    slot_of[i] = k;

        const int2 extent       = planes->alpha.size();
        const int  iters        = m_iterations;
        const bool match_ratios = m_match_ratios;

        ctx.modify_channels_async(
            "Seamless paste",
            [planes, slot_of, extent, iters, match_ratios](const Array2Df &dst, const Box2i &region, int slot,
                                                           AtomicProgress p) -> Array2Df
            {
                const int2 size = region.size();

                Array2Df out{size};
                for (int y = 0; y < size.y; ++y)
                    for (int x = 0; x < size.x; ++x) out(x, y) = dst(region.min.x + x, region.min.y + y);

                const int k = slot < int(slot_of.size()) ? slot_of[size_t(slot)] : -1;
                if (k < 0)
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
                        source(x, y)     = planes->slots[size_t(k)](x, y);

                        // the clipboard's own alpha is the mask, zeroed along the border, which is what
                        // pins the solve to the background
                        const bool border = x == 0 || y == 0 || x == solve.x - 1 || y == solve.y - 1;
                        mask(x, y)        = border ? 0.f : planes->alpha(x, y);
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
    int          m_iterations   = 300;
    bool         m_match_ratios = false;
    PasteMapping m_mapping;
};

} // namespace

void add_clipboard_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<Cut>());
    out.push_back(std::make_unique<Copy>());
    out.push_back(std::make_unique<Paste>());
    out.push_back(std::make_unique<SeamlessPaste>());
}
