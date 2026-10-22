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
                     "Convert",
                     27.f})
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
            ImGui::TextWrapped("These describe the same color space, so this would leave the samples as they are.");
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
        EditCommand({{"Channel mixer...", "Mix channels", "Monochrome"},
                     ICON_MY_CHANNEL_MIXER,
                     ImGuiKey_None,
                     true,
                     "Mix",
                     27.f})
    {
    }

    void draw(EditContext &) override
    {
        // radio-height text, so the label sits on the same line as the buttons beside it
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Output channel");

        // each named in its own color, gray last
        const char  *names[]  = {"Red", "Green", "Blue", "Monochrome"};
        const ImVec4 colors[] = {ImVec4(0.90f, 0.35f, 0.35f, 1.f), ImVec4(0.35f, 0.85f, 0.35f, 1.f),
                                 ImVec4(0.45f, 0.55f, 1.00f, 1.f), ImVec4(0.80f, 0.80f, 0.80f, 1.f)};

        for (int i = 0; i < Out_COUNT; ++i)
        {
            // only between them: SameLine() before the first would put it back on the line above
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, colors[i]);
            ImGui::RadioButton(names[i], &m_output, i);
            ImGui::PopStyleColor(2);
        }
        ImGui::Tooltip("Which channel the weights below are written to. Monochrome writes one gray value "
                       "to all three.");

        float3 &w = m_rows[m_output];

        ImGui::DragFloat("Red##weight", &w.x, 0.5f, -200.f, 200.f, "%.1f%%");
        ImGui::DragFloat("Green##weight", &w.y, 0.5f, -200.f, 200.f, "%.1f%%");
        ImGui::DragFloat("Blue##weight", &w.z, 0.5f, -200.f, 200.f, "%.1f%%");

        // the checkbox first, so the total, whose width changes, has nothing after it to push around
        ImGui::Checkbox("Normalize", &m_normalize);
        ImGui::Tooltip("Divides each row by its own total, so the mix neither brightens nor darkens. Off, a "
                       "total above 100% lightens the result and one below darkens it.");
        ImGui::SameLine();
        ImGui::TextDisabled("Total: %.1f%%", w.x + w.y + w.z);
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
        const bool   mono = m_output == Out_Monochrome;
        const float3 r    = row(m_rows[mono ? Out_Monochrome : Out_Red]),
                     g    = row(m_rows[mono ? Out_Monochrome : Out_Green]),
                     b    = row(m_rows[mono ? Out_Monochrome : Out_Blue]);
        PointEdit    e{shader::EDIT_MATRIX};
        e.m0 = transpose(float3x3{r, g, b});
        return e;
    }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Channel mixer", *point_edit(ctx)); }

private:
    /// Which channel the weights are written to.
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
    int  m_output    = Out_Red;
    bool m_normalize = false;
};

class HueSaturation final : public EditCommand
{
public:
    HueSaturation() :
        EditCommand({{"Hue/saturation...", "Colorize", "Desaturate"},
                     ICON_MY_HUE_SATURATION,
                     ImGuiKey_None,
                     true,
                     "Apply",
                     27.f})
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
        ImGui::Tooltip("Mix toward black, or toward white, or toward a highlight's own level where it is brighter.");

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
        strip("The hue wheel as it is", PointEdit{shader::EDIT_HUE_SATURATION});
        const float w =
            strip("The hue wheel as these settings would leave it; drag it to turn the hue", *point_edit(ctx));

        // dragging the strip carries its colors along with the mouse, so the hue under it is the one grabbed
        if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0.f && w > 0.f)
        {
            const float h = m_hue - 360.f * ImGui::GetIO().MouseDelta.x / w;
            m_hue         = h - 360.f * std::floor((h + 180.f) / 360.f);
        }
    }

    std::optional<PointEdit> point_edit(const EditContext &ctx) const override
    {
        // toward the gray of the image's own primaries, and on straight color where the samples are premultiplied,
        // which finalize() makes them whenever alpha means transparency
        const float3 Yw            = ctx.image ? ctx.image->luminance_weights : sRGB_Yw();
        bool         premultiplied = false;
        if (auto img = ctx.image; img && img->is_valid_group(img->active_group_index(Target_Primary)))
            premultiplied = img->alpha_is_transparency() &&
                            group_has_alpha(img->groups[size_t(img->active_group_index(Target_Primary))].type);

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
                     "Apply",
                     27.f})
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

        // radio-height text, so the label sits on the same line as the buttons beside it
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Curve");
        struct Shape
        { const char *name, *tooltip; };
        static const Shape shapes[] = {
            {"S-curve", "An s-curve that approaches black and white without arriving, and clips beyond them."},
            {"Line", "A straight line through the midpoint, which runs past black and white, so an HDR sample "
                     "keeps its difference from its neighbors."},
            {"Rational", "Schlick's rational s-curve, whose ends flatten as its middle steepens, and which carries on "
                         "past black and white along its slopes there, so an HDR sample keeps a share of its "
                         "difference from its neighbors."}};
        static_assert(IM_ARRAYSIZE(shapes) == Shape_COUNT);
        for (int i = 0; i < Shape_COUNT; ++i)
        {
            ImGui::SameLine();
            ImGui::RadioButton(shapes[i].name, &m_shape, i);
            ImGui::Tooltip(shapes[i].tooltip);
        }

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
        e.p0 = float4{slope_of(m_contrast), (1.f - m_brightness) / 2.f, (m_brightness + 1.f) / 2.f, float(m_shape)};
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

    void apply(const EditContext &ctx) override
    {
        if (ctx.image)
            apply_point_edit(ctx, "Brightness/contrast", *point_edit(ctx));
    }

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
        Each half of Schlick's gain is his bias of a doubled input, which one division inverts for its parameter.
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
        return a > 0.f && a < 1.f ? (1.f - a) / a : -1.f;
    }

    /// The gain exponent that puts the s-curve through (\p x, \p y); negative when nothing can.
    /**
        gain_Perlin is a power on each half of its range, so this is one logarithm; the bias stays put.
    */
    static float curve_gain_through(float x, float y, float bias)
    {
        const float u = bias_Schlick(std::clamp(x, 0.f, 1.f), bias);
        if (u <= 1e-4f || u >= 1.f - 1e-4f || y <= 1e-4f || y >= 1.f - 1e-4f)
            return -1.f;

        // each half is pinned at its own end and at the middle, so a target on the far side of the
        // middle from the input is unreachable at any exponent
        if ((u > 0.5f) != (y > 0.5f))
            return -1.f;

        return u > 0.5f ? std::log(2.f * (1.f - y)) / std::log(2.f - 2.f * u) : std::log(2.f * y) / std::log(2.f * u);
    }

    /// The curves over [0,1], the one in force drawn solid and the others left faint behind it.
    void draw_curve()
    {
        const float slope    = slope_of(m_contrast);
        const float midpoint = (1.f - m_brightness) / 2.f;
        const float bias     = (m_brightness + 1.f) / 2.f;

        float linear[ImGui::ToneCurveSamples], curved[ImGui::ToneCurveSamples], rational[ImGui::ToneCurveSamples];
        for (int i = 0; i < ImGui::ToneCurveSamples; ++i)
        {
            const float x = ImGui::ToneCurveX(i);
            linear[i]     = brightness_contrast_linear(x, slope, midpoint);
            curved[i]     = brightness_contrast_nonlinear(x, slope, bias);
            rational[i]   = brightness_contrast_rational(x, slope, bias);
        }

        if (!ImGui::BeginToneCurvePlot("##Curve", true))
            return;

        const ImVec4 active{1.f, 1.f, 1.f, 0.85f};
        const ImVec4 faint{1.f, 1.f, 1.f, 0.18f};

        // all are drawn whichever is in force, the others faint
        ImGui::ToneCurve("s-curve", curved, m_shape == Shape_SCurve ? active : faint);
        ImGui::ToneCurve("line", linear, m_shape == Shape_Line ? active : faint);
        ImGui::ToneCurve("rational", rational, m_shape == Shape_Rational ? active : faint);

        // the pivot: the one input every curve sends to the middle, whatever the contrast. Drawn as a handle because
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
                const float wanted = m_shape == Shape_Line       ? line_slope_through(from.x, p.y, midpoint)
                                     : m_shape == Shape_Rational ? rational_slope_through(from.x, p.y, bias)
                                                                 : curve_gain_through(from.x, p.y, bias);
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
    /// The curve's shape.
    enum Shape : int
    {
        Shape_SCurve = 0,
        Shape_Line,
        Shape_Rational,

        Shape_COUNT
    };
    // the shader's names for them, which point_edit() hands it
    static_assert(Channel_RGB == shader::CURVE_RGB && Channel_Lightness == shader::CURVE_LIGHTNESS &&
                  Channel_Chromaticity == shader::CURVE_CHROMATICITY);
    static_assert(Shape_SCurve == shader::SHAPE_S_CURVE && Shape_Line == shader::SHAPE_LINE &&
                  Shape_Rational == shader::SHAPE_RATIONAL);

    float m_brightness = 0.f, m_contrast = 0.f;
    int   m_shape   = Shape_SCurve;
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
        ImGui::TextWrapped("Composites the image over a background color, so what was transparent becomes "
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
