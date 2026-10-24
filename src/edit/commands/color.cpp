//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file color.cpp
    \author Wojciech Jarosz

    The edits that need to operate on a group's channels together, instead of independently.
*/

#include "edit/commands.h"

#include "colorspace.h"
#include "edit/edit_ops.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"
#include "shared/edits.h"

#include <algorithm>
#include <cmath>
#include <hello_imgui/hello_imgui.h>
#include <optional>
#include <vector>

namespace
{

class ConvertColorSpace final : public EditCommand
{
public:
    ConvertColorSpace() :
        EditCommand({{"Convert color space...", "Change primaries", "Change white point", "Gamut conversion"},
                     ICON_MY_COLORSPACE,
                     ImGuiKey_None,
                     true,
                     "Convert"})
    {
    }

    /// The file's own tag, so a conversion starts from what the pixels are.
    void on_open(EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        m_src_gamut = img->color_space;
        m_src_white = img->white_point;
        if (m_src_gamut == ColorGamut_Unspecified || m_src_gamut == ColorGamut_Custom)
            m_src_gamut = ColorGamut_sRGB_BT709;
        if (m_src_white == WhitePoint_Unspecified)
            m_src_white = WhitePoint_D65;
        m_method = img->adaptation_method;
    }

    void draw(EditContext &) override
    {
        auto gamut_combo = [](const char *label, int *value)
        {
            if (ImGui::BeginCombo(label, color_gamut_name(ColorGamut_(*value)), ImGuiComboFlags_HeightLargest))
            {
                for (int n = ColorGamut_FirstNamed; n <= ColorGamut_LastNamed; ++n)
                    if (ImGui::Selectable(color_gamut_name(ColorGamut_(n)), *value == n))
                        *value = n;
                ImGui::EndCombo();
            }
        };
        auto white_combo = [](const char *label, int *value)
        {
            if (ImGui::BeginCombo(label, white_point_name(WhitePoint_(*value)), ImGuiComboFlags_HeightLargest))
            {
                for (int n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                    if (ImGui::Selectable(white_point_name(WhitePoint_(n)), *value == n))
                        *value = n;
                ImGui::EndCombo();
            }
        };

        ImGui::SeparatorText("From");
        gamut_combo("Primaries##from", &m_src_gamut);
        white_combo("White point##from", &m_src_white);
        ImGui::Tooltip("What the samples already are, taken from the image's own tag. Correcting it here "
                       "changes how they are read, not what they are.");

        ImGui::SeparatorText("To");
        gamut_combo("Primaries##to", &m_dst_gamut);
        white_combo("White point##to", &m_dst_white);

        if (ImGui::BeginCombo("Adaptation", adaptation_method_name(AdaptationMethod(m_method))))
        {
            for (int n = 0; n < AdaptationMethod_Count; ++n)
                if (ImGui::Selectable(adaptation_method_name(AdaptationMethod(n)), m_method == n))
                    m_method = n;
            ImGui::EndCombo();
        }
        ImGui::Tooltip("How a change of white point is accounted for. Bradford is the usual choice; the "
                       "identity transform leaves the primaries to do it alone, which shifts neutrals.");

        float3x3 M;
        if (!color_conversion_matrix(M, from(), to(), AdaptationMethod(m_method)))
            ImGui::TextWrappedToWidth(
                "These describe the same color space, so this would leave the samples as they are.");
    }

    std::optional<PointEdit> point_edit(const EditContext &) const override
    {
        PointEdit e{shader::EDIT_MATRIX};
        color_conversion_matrix(e.m0, from(), to(), AdaptationMethod(m_method));
        return e;
    }

    // as compute_color_transform() works it out for the tag apply() leaves, which adopts no neutral of its own
    std::optional<float3x3> M_to_sRGB_after(const EditContext &) const override
    {
        float3x3 M;
        color_conversion_matrix(M, to(), Chromaticities{}, AdaptationMethod(m_method));
        return M;
    }

    void apply(const EditContext &ctx) override
    {
        const int  g = m_dst_gamut, w = m_dst_white, m = m_method;
        const auto tagged = to();

        apply_point_edit(ctx, "Convert color space", *point_edit(ctx),
                         [g, w, m, tagged](Image &image)
                         {
                             image.chromaticities    = tagged;
                             image.adopted_neutral   = std::nullopt;
                             image.adaptation_method = AdaptationMethod(m);
                             image.compute_color_transform();

                             // compute_color_transform() names the space from the chromaticities; these only matter
                             // when the destination is one it cannot recognize
                             image.color_space = ColorGamut_(g);
                             image.white_point = WhitePoint_(w);

                             // the samples are linear light in the new primaries; the Colorspace panel reads this as
                             // its "Profile name"
                             image.metadata["color profile"] =
                                 color_profile_name(ColorGamut_(g), TransferFunction::Linear);
                         });
    }

private:
    Chromaticities from() const
    {
        Chromaticities c = gamut_chromaticities(ColorGamut_(m_src_gamut));
        c.white          = white_point(WhitePoint_(m_src_white));
        return c;
    }
    Chromaticities to() const
    {
        Chromaticities c = gamut_chromaticities(ColorGamut_(m_dst_gamut));
        c.white          = white_point(WhitePoint_(m_dst_white));
        return c;
    }

    int m_src_gamut = ColorGamut_sRGB_BT709, m_dst_gamut = ColorGamut_sRGB_BT709;
    int m_src_white = WhitePoint_D65, m_dst_white = WhitePoint_D65;
    int m_method = AdaptationMethod_Bradford;
};

class ChannelMixer final : public EditCommand
{
public:
    ChannelMixer() :
        EditCommand(
            {{"Channel mixer...", "Mix channels", "Monochrome"}, ICON_MY_CHANNEL_MIXER, ImGuiKey_None, true, "Mix"})
    {
    }

    void draw(EditContext &) override
    {
        ImGui::Checkbox("Monochrome", &m_monochrome);
        ImGui::Tooltip("Mix one gray value from the three channels, and write it to all of them.");

        // The whole mix at once, a row of source weights per output channel, each channel named in its own color and
        // the gray row in gray. Fit to its columns, so the table gives the dialog its width rather than taking it.
        const char  *names[]  = {"Red", "Green", "Blue", "Gray"};
        const ImVec4 colors[] = {ImVec4(0.90f, 0.35f, 0.35f, 1.f), ImVec4(0.35f, 0.85f, 0.35f, 1.f),
                                 ImVec4(0.45f, 0.55f, 1.00f, 1.f), ImVec4(0.80f, 0.80f, 0.80f, 1.f)};
        auto         name     = [&](int i)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
            ImGui::TextUnformatted(names[i]);
            ImGui::PopStyleColor();
        };

        if (ImGui::BeginTable("##mix", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
        {
            const float cell = HelloImGui::EmSize(5.f);
            ImGui::TableSetupColumn("##output");
            for (int c = 0; c < 3; ++c) ImGui::TableSetupColumn(names[c], ImGuiTableColumnFlags_WidthFixed, cell);
            ImGui::TableSetupColumn("Total");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            for (int c = 0; c < 3; ++c)
            {
                ImGui::TableNextColumn();
                name(c);
                ImGui::Tooltip("How much of each output is taken from this channel.");
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Total");

            for (int r = m_monochrome ? int(Out_Monochrome) : int(Out_Red);
                 r < (m_monochrome ? int(Out_COUNT) : int(Out_Monochrome)); ++r)
            {
                float3 &w = m_rows[r];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                name(r);
                ImGui::Tooltip(r == Out_Monochrome ? "The weights the gray is mixed with."
                                                   : "The weights this output channel is mixed with.");
                for (int c = 0; c < 3; ++c)
                {
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat(fmt::format("##weight{}{}", r, c).c_str(), &w[c], 0.5f, -200.f, 200.f, "%.1f%%");
                }
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%.1f%%", w.x + w.y + w.z);
            }
            ImGui::EndTable();
        }

        ImGui::Checkbox("Normalize", &m_normalize);
        ImGui::Tooltip("Divides each row by its own total, so the mix neither brightens nor darkens.");
    }

    std::optional<PointEdit> point_edit(const EditContext &) const override
    {
        auto row = [norm = m_normalize](const float3 &r)
        {
            const float3 v   = r / 100.f;
            const float  sum = v.x + v.y + v.z;
            // a row summing to zero is a legitimate difference of channels; leave it alone
            return norm && std::abs(sum) > 1e-6f ? v / sum : v;
        };

        // the rows of the matrix, as source weights per output channel; Monochrome writes one gray to all three
        const bool   mono = m_monochrome;
        const float3 r    = row(m_rows[mono ? Out_Monochrome : Out_Red]),
                     g    = row(m_rows[mono ? Out_Monochrome : Out_Green]),
                     b    = row(m_rows[mono ? Out_Monochrome : Out_Blue]);
        PointEdit    e{shader::EDIT_MATRIX};
        e.m0 = transpose(float3x3{r, g, b});
        return e;
    }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Channel mixer", *point_edit(ctx)); }

private:
    /// The rows of weights: one per output channel, and the one that mixes a single gray.
    enum Output : int
    {
        Out_Red = 0,
        Out_Green,
        Out_Blue,
        Out_Monochrome,

        Out_COUNT
    };

    // a row of source weights per output channel, plus the one that makes a single gray from all three,
    // as percentages the way Photoshop presents them. Thirds, so the monochrome default totals 100%.
    float3 m_rows[Out_COUNT] = {
        {100.f, 0.f, 0.f}, {0.f, 100.f, 0.f}, {0.f, 0.f, 100.f}, {100.f / 3.f, 100.f / 3.f, 100.f / 3.f}};
    bool m_monochrome = false;
    bool m_normalize  = false;
};

class HueSaturation final : public EditCommand
{
public:
    HueSaturation() :
        EditCommand(
            {{"Hue/saturation...", "Colorize", "Desaturate"}, ICON_MY_HUE_SATURATION, ImGuiKey_None, true, "Apply"})
    {
    }

    void draw(EditContext &ctx) override
    {
        // the ranges Photoshop uses: hue in degrees around the wheel, the other two as a percentage
        ImGui::SliderFloat("Hue", &m_hue, -180.f, 180.f, "%+.0f deg");
        ImGui::Tooltip("Turn every color around the hue wheel. Dragging the lower strip turns it too.");
        ImGui::SliderFloat("Saturation", &m_saturation, -100.f, 100.f, "%+.0f%%");
        ImGui::Tooltip("Move every color toward or away from the gray of its own luminance.");
        ImGui::SliderFloat("Lightness", &m_lightness, -100.f, 100.f, "%+.0f%%");
        ImGui::Tooltip("Darken toward black, or lighten toward white (toward the color's own peak, for colors brighter "
                       "than white).");

        // The wheel as it is and as the settings would leave it.
        /**
            Drawn as shaded quads between the points where the sweep bends, which is not an approximation: the
            hue hexcone is piecewise linear in hue, with its corners slid along by the turn, the saturation is a
            matrix, and the lightness is affine but for the level it mixes toward, which bends where a component
            crosses white.

            The other bends are where clamping to the display's range bends the ramp, which is what raising the
            saturation of an already-saturated hue looks like: flat, a steeper ramp, then flat.
        */
        auto strip = [&](const char *id, const PointEdit &edit)
        {
            const float  w  = ImGui::GetContentRegionAvail().x;
            const ImVec2 p  = ImGui::GetCursorScreenPos();
            auto        *dl = ImGui::GetWindowDrawList();

            // before the display's range is imposed, where the bends are still straight lines
            auto wheel = [](float t) { return float4{HSL_to_RGB(float3{t, 1.f, 0.5f}), 1.f}; };
            auto raw   = [&](float t)
            { return shader::edit_color(edit.op, wheel(t), edit.p0, edit.p1, edit.m0, edit.m1).xyz(); };
            auto shown = [&](float t)
            {
                const float3 c = raw(t);
                return ImGui::ColorConvertFloat4ToU32(
                    ImVec4(std::clamp(c.x, 0.f, 1.f), std::clamp(c.y, 0.f, 1.f), std::clamp(c.z, 0.f, 1.f), 1.f));
            };

            // Where a component of f crosses 0 or 1 between each pair of knots, between which it is linear, so each
            // crossing is one division.
            auto crossings = [](const std::vector<float> &knots, auto f)
            {
                std::vector<float> out = knots;
                for (size_t i = 0; i + 1 < knots.size(); ++i)
                {
                    const float  t0 = knots[i], t1 = knots[i + 1];
                    const float3 a = f(t0), b = f(t1);
                    for (int c = 0; c < 3; ++c)
                        for (float level : {0.f, 1.f})
                            if ((a[c] < level) != (b[c] < level) && a[c] != b[c])
                                out.push_back(t0 + (level - a[c]) / (b[c] - a[c]) * (t1 - t0));
                }
                std::sort(out.begin(), out.end());
                return out;
            };

            // the hexcone's corners, moved by the turn and wrapped back into the strip, then where the saturated
            // color crosses white, where the lightness bends, then where the result crosses the display's range
            std::vector<float> knots{0.f, 1.f};
            for (int k = 0; k < 6; ++k)
            {
                const float t = float(k) / 6.f - edit.p0.x;
                knots.push_back(t - std::floor(t));
            }
            std::sort(knots.begin(), knots.end());
            knots = crossings(knots, [&](float t) { return mul(edit.m0, rotate_hue(wheel(t).xyz(), edit.p0.x)); });
            const std::vector<float> bends = crossings(knots, raw);

            const float height = ImGui::GetFrameHeight();
            for (size_t i = 0; i + 1 < bends.size(); ++i)
            {
                const float t0 = bends[i], t1 = bends[i + 1];
                if (t1 <= t0)
                    continue; // two bends landing together leave nothing between them

                const ImU32 left = shown(t0), right = shown(t1);
                dl->AddRectFilledMultiColor(ImVec2(p.x + t0 * w, p.y), ImVec2(p.x + t1 * w, p.y + height), left, right,
                                            right, left);
            }

            ImGui::InvisibleButton(id, ImVec2(w, height));
            ImGui::SetItemTooltip("%s", id);
            return w;
        };
        // as wide as the controls above them, rather than what makes the dialog wide
        float w = 0.f;
        {
            ImGui::FillsWidth fill;
            strip("The hue wheel as it is", PointEdit{shader::EDIT_HUE_SATURATION});
            w = strip("The hue wheel as these settings would leave it; drag it to turn the hue", *point_edit(ctx));
        }

        // dragging the strip carries its colors along with the mouse, so the hue under it is the one grabbed
        if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0.f && w > 0.f)
        {
            const float h = m_hue - 360.f * ImGui::GetIO().MouseDelta.x / w;
            m_hue         = h - 360.f * std::floor((h + 180.f) / 360.f);
        }
    }

    std::optional<PointEdit> point_edit(const EditContext &ctx) const override
    {
        // Toward the gray of the image's own primaries, on straight color wherever alpha means transparency, since
        // finalize() then stores the samples premultiplied. A group without alpha reads as opaque either way.
        const auto  &img           = ctx.image;
        const bool   premultiplied = img && img->alpha_is_transparency();
        const float3 Yw            = img ? img->luminance_weights : sRGB_Yw();

        PointEdit e{shader::EDIT_HUE_SATURATION};
        e.m0 = saturation_matrix((m_saturation + 100.f) / 100.f, Yw);
        e.p0 = float4{m_hue / 360.f, 0.f, m_lightness / 100.f, 0.f};
        e.p1 = float4{premultiplied ? 1.f : 0.f, 0.f, 0.f, 0.f};
        return e;
    }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Hue/saturation", *point_edit(ctx)); }

private:
    float m_hue = 0.f, m_saturation = 0.f, m_lightness = 0.f;
};

class BrightnessContrast final : public EditCommand
{
public:
    BrightnessContrast() :
        EditCommand({{"Brightness/contrast...", "Levels", "Tone curve"},
                     ICON_MY_BRIGHTNESS_CONTRAST,
                     ImGuiKey_None,
                     true,
                     "Apply"})
    {
    }

    void draw(EditContext &) override
    {
        // the curve first, since it is what the two sliders are for
        draw_curve();

        ImGui::SliderFloat("Brightness", &m_brightness, -1.f, 1.f, "%+.3f");
        ImGui::Tooltip("Shift the 50% gray midpoint.\n\nAbove zero this lifts a previously darker value to "
                       "50%; below zero it dims a previously brighter one to 50%.");

        ImGui::SliderFloat("Contrast", &m_contrast, -1.f, 1.f, "%+.3f");
        ImGui::Tooltip("Change the slope at the new 50% midpoint. At -1 everything collapses to one level; "
                       "at +1 the curve is vertical and only black and white are left.");

        ImGui::Checkbox("Linear", &m_linear);
        ImGui::Tooltip("Use a straight line through the midpoint instead of an s-curve. Both continue past black and "
                       "white, so HDR values keep contrast there: the line all of it, the s-curve a share.");
        ImGui::SameLine();
        ImGui::Checkbox("Clip to SDR", &m_clip);
        ImGui::Tooltip("Clip what the curve gives to [0, 1], as an SDR image holds it.");

        // radio-height text, so the label sits on the same line as the buttons beside it
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Adjust");
        const char *names[] = {"RGB", "Lightness", "Chromaticity"};
        for (int i = 0; i < Channel_COUNT; ++i)
        {
            ImGui::SameLine();
            ImGui::RadioButton(names[i], &m_channel, i);
        }
        ImGui::Tooltip("RGB moves the three channels alike, which shifts saturation along with everything "
                       "else. The other two work in L*a*b*, where lightness and color are separate: one "
                       "changes how light the image is and leaves its colors, the other does the reverse.");
    }

    std::optional<PointEdit> point_edit(const EditContext &ctx) const override
    {
        PointEdit e{shader::EDIT_BRIGHTNESS_CONTRAST};
        const int shape = (m_linear ? shader::SHAPE_LINE : shader::SHAPE_RATIONAL) + (m_clip ? shader::SHAPE_CLIP : 0);
        e.p0 = float4{slope_of(m_contrast), (1.f - m_brightness) / 2.f, (m_brightness + 1.f) / 2.f, float(shape)};
        e.p1 = float4{float(m_channel), 0.f, 0.f, 0.f};
        // through the image's own primaries: L*a*b* is defined from XYZ, and what the samples mean in XYZ is what
        // the image says they do
        if (m_channel != Channel_RGB && ctx.image)
        {
            const auto  &img   = *ctx.image;
            const float3 white = img.chromaticities ? XYZ_from_xy(img.chromaticities->white) : Lab_reference_white();
            e.p1               = float4{float(m_channel), white.x, white.y, white.z};
            e.m0               = img.M_RGB_to_XYZ;
            e.m1               = img.M_XYZ_to_RGB;
        }
        return e;
    }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Brightness/contrast", *point_edit(ctx)); }

private:
    /// The slope the contrast slider asks for, as the tangent of an angle.
    /**
        The slider's ends are then the two extremes: -1 horizontal and flat, 0 the 45-degree diagonal, +1
        vertical and black-or-white.
    */
    static float slope_of(float contrast) { return float(std::tan(lerp(0.0, M_PI_2, contrast / 2.0 + 0.5))); }

    /// The inverse of slope_of(): what the slider must read for the curve to have this slope.
    static float contrast_of(float slope)
    {
        return std::clamp(float(4.0 * std::atan(std::max(0.f, slope)) / M_PI) - 1.f, -1.f, 1.f);
    }

    /// The slope that puts the straight line through (\p x, \p y); negative when nothing can.
    /**
        The line is fixed through its pivot, so one more point determines it, unless that point is the pivot
        itself.
    */
    static float line_slope_through(float x, float y, float midpoint)
    {
        if (std::fabs(x - midpoint) < 1e-4f)
            return -1.f;
        return (y - 0.5f) / (x - midpoint);
    }

    /// The midpoint slope that puts the rational s-curve through (\p x, \p y); negative when nothing can.
    /**
        The curve is Schlick's gain after his bias, and each half of the gain is a bias of a doubled input, so solving
        for its parameter takes one division. The curve's midpoint slope is the gain's times the bias's,
        1 / (4 bias (1 - bias)).
    */
    static float rational_slope_through(float x, float y, float bias)
    {
        const float u = bias_Schlick(std::clamp(x, 0.f, 1.f), bias);
        if (u <= 1e-4f || u >= 1.f - 1e-4f || y <= 1e-4f || y >= 1.f - 1e-4f || (u > 0.5f) != (y > 0.5f))
            return -1.f;
        // bias(t, a) = z for a, on the half the input falls in
        const float t = u < 0.5f ? 2.f * u : 2.f - 2.f * u, z = u < 0.5f ? 2.f * y : 2.f - 2.f * y;
        if (std::fabs(1.f - t) < 1e-4f)
            return -1.f;
        const float a = 1.f / ((t / z - 1.f) / (1.f - t) + 2.f);
        return a > 0.f && a < 1.f ? (1.f - a) / a / (4.f * bias * (1.f - bias)) : -1.f;
    }

    /// Both curves over [0,1], the one in force drawn solid and the other left faint behind it.
    void draw_curve()
    {
        const float slope    = slope_of(m_contrast);
        const float midpoint = (1.f - m_brightness) / 2.f;
        const float bias     = (m_brightness + 1.f) / 2.f;

        float linear[ImGui::ToneCurveSamples], rational[ImGui::ToneCurveSamples];
        for (int i = 0; i < ImGui::ToneCurveSamples; ++i)
        {
            // clipped as the edit clips them, so the plot shows what Clip to SDR does
            const float x = ImGui::ToneCurveX(i);
            auto        f = [this](float y) { return m_clip ? std::clamp(y, 0.f, 1.f) : y; };
            linear[i]     = f(brightness_contrast_linear(x, slope, midpoint));
            rational[i]   = f(brightness_contrast_rational(x, slope, bias));
        }

        if (!ImGui::BeginToneCurvePlot("##Curve", true))
            return;

        const ImVec4 active{1.f, 1.f, 1.f, 0.85f};
        const ImVec4 faint{1.f, 1.f, 1.f, 0.18f};

        ImGui::ToneCurve("rational", rational, m_linear ? faint : active);
        ImGui::ToneCurve("line", linear, m_linear ? active : faint);

        // the pivot: the one input both curves send to the middle, whatever the contrast. Drawn as a handle because
        // it is also what the drag below grabs.
        ImGui::ToneCurveMarkerX("pivot", midpoint, ImVec4(1.f, 1.f, 1.f, 0.35f));
        ImGui::ToneCurveHandle(float2{midpoint, 0.5f}, ImVec4(1.f, 1.f, 1.f, 0.55f));

        /*
            Dragging does one of two things, decided by what was under the cursor when it went down. Near
            the pivot it drags the pivot, which is brightness alone; anywhere else it bends the curve, the
            grabbed input keeping its place on the horizontal axis, which is contrast alone.

            The two cannot be combined: the pivot's output is one half at every contrast, so both requests
            agree only when the cursor is at one half.
        */
        if (float2 p, from; ImGui::ToneCurveDrag(p, &from))
        {
            if (m_dragging_pivot.value_or(std::fabs(from.x - midpoint) < 0.06f))
            {
                m_dragging_pivot = true;
                m_brightness     = std::clamp(lerp(1.f, -1.f, p.x), -1.f, 1.f);
            }
            else
            {
                m_dragging_pivot = false;

                // left as it is where the cursor asks for something no setting produces, so the curve
                // stops following instead of jumping to an end of the slider
                const float wanted =
                    m_linear ? line_slope_through(from.x, p.y, midpoint) : rational_slope_through(from.x, p.y, bias);
                if (wanted >= 0.f)
                    m_contrast = contrast_of(wanted);
            }
        }
        else
            m_dragging_pivot.reset();

        ImGui::EndToneCurvePlot();
    }

    /// Which of the image's qualities the curve is applied to.
    enum Channel : int
    {
        Channel_RGB = 0,      ///< The three channels alike, saturation moving with everything else
        Channel_Lightness,    ///< L* alone, so the colors stay where they are
        Channel_Chromaticity, ///< a* and b*, so how light the image is does not change

        Channel_COUNT
    };
    // the shader's names for them, which point_edit() hands it
    static_assert(Channel_RGB == shader::CURVE_RGB && Channel_Lightness == shader::CURVE_LIGHTNESS &&
                  Channel_Chromaticity == shader::CURVE_CHROMATICITY);

    float m_brightness = 0.f, m_contrast = 0.f;
    bool  m_linear = false, m_clip = false;
    int   m_channel = Channel_RGB;
    /// Which of the two things the drag in progress is doing; unset between drags.
    std::optional<bool> m_dragging_pivot;
};

class Flatten final : public EditCommand
{
public:
    Flatten() :
        EditCommand({{"Flatten...", "Composite over a background", "Remove transparency"},
                     ICON_MY_FLATTEN,
                     ImGuiKey_None,
                     true,
                     "Flatten"})
    {
    }

    void draw(EditContext &ctx) override
    {
        ImGui::TextWrappedToWidth("Composites the image over a background color, so what was transparent becomes "
                                  "that color and the result is opaque.");
        ImGui::Spacing();

        ImGui::ColorEdit4("Background", &m_bg.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);

        // the color the viewport is already showing behind the image
        if (ImGui::Button("Use viewport background"))
            m_bg = ctx.background;
        ImGui::Tooltip("Takes the custom background color from the View menu.");
    }

    std::optional<PointEdit> point_edit(const EditContext &) const override
    {
        PointEdit e{shader::EDIT_FLATTEN};
        e.p0 = m_bg;
        return e;
    }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Flatten", *point_edit(ctx)); }

private:
    float4 m_bg{0.f, 0.f, 0.f, 1.f};
};

} // namespace

void add_color_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<BrightnessContrast>());
    out.push_back(std::make_unique<ConvertColorSpace>());
    out.push_back(std::make_unique<ChannelMixer>());
    out.push_back(std::make_unique<HueSaturation>());
    out.push_back(std::make_unique<Flatten>());
}
