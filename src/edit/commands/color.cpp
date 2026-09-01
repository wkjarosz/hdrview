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
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <cmath>

namespace
{

class ConvertColorSpace final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Convert color space...", "Change primaries", "Change white point", "Gamut conversion"},
                ICON_MY_COLORSPACE,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Convert",
                27.f};
    }

    //! The file's own tag, so a conversion starts from what the pixels actually are.
    void on_open(EditContext &ctx) override
    {
        auto img = ctx.image();
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

    void apply(EditContext &ctx) override
    {
        float3x3 M;
        color_conversion_matrix(M, from(), to(), AdaptationMethod(m_method));

        const int  g = m_dst_gamut, w = m_dst_white, m = m_method;
        const auto tagged = to();

        ctx.modify_colors(
            "Convert color space",
            // Premultiplied alpha is not in the way here: the conversion is a matrix, and scaling every
            // component by alpha commutes with it.
            [M](const float4 &c, int2) { return float4{la::mul(M, c.xyz()), c.w}; },
            [g, w, m, tagged](Image &image)
            {
                image.chromaticities    = tagged;
                image.adopted_neutral   = std::nullopt;
                image.adaptation_method = AdaptationMethod(m);
                image.compute_color_transform();

                // compute_color_transform() names the space from the chromaticities, which is the
                // authority; these only matter when the destination is one it cannot recognize.
                image.color_space = ColorGamut_(g);
                image.white_point = WhitePoint_(w);

                // The samples are linear light in the new primaries, and the Colorspace panel reads this
                // as its "Profile name".
                image.metadata["color profile"] = color_profile_name(ColorGamut_(g), TransferFunction::Linear);
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
    Info info() const override
    {
        return {{"Channel mixer...", "Mix channels", "Monochrome"},
                ICON_MY_CHANNEL_MIXER,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Mix",
                27.f};
    }

    void draw(EditContext &) override
    {
        // Radio-height text, so the label sits on the same line as the buttons beside it rather than
        // riding above them.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Output channel");

        // Each named in its own color, and gray last: the output is one of
        // four rather than three plus a mode, so nothing has to say what happens when both are set.
        const char  *names[]  = {"Red", "Green", "Blue", "Monochrome"};
        const ImVec4 colors[] = {ImVec4(0.90f, 0.35f, 0.35f, 1.f), ImVec4(0.35f, 0.85f, 0.35f, 1.f),
                                 ImVec4(0.45f, 0.55f, 1.00f, 1.f), ImVec4(0.80f, 0.80f, 0.80f, 1.f)};

        for (int i = 0; i < Out_COUNT; ++i)
        {
            // Only between them: SameLine() before the first would put it back on the line above.
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

        // The checkbox first, so that the total -- which changes width as it changes -- has nothing after
        // it to push around.
        ImGui::Checkbox("Normalize", &m_normalize);
        ImGui::Tooltip("Divides each row by its own total, so the mix neither brightens nor darkens. Off, a "
                       "total above 100% lightens the result and one below darkens it.");
        ImGui::SameLine();
        ImGui::TextDisabled("Total: %.1f%%", w.x + w.y + w.z);
    }

    void apply(EditContext &ctx) override
    {
        auto row = [norm = m_normalize](const float3 &r)
        {
            const float3 v   = r / 100.f;
            const float  sum = v.x + v.y + v.z;
            // A row summing to zero is a legitimate difference of channels; leaving it alone is the only
            // thing to do rather than dividing by nothing.
            return norm && std::abs(sum) > 1e-6f ? v / sum : v;
        };

        if (m_output == Out_Monochrome)
        {
            const float3 g = row(m_rows[Out_Monochrome]);
            ctx.modify_colors("Channel mixer",
                              [g](const float4 &c, int2)
                              {
                                  const float y = la::dot(g, c.xyz());
                                  return float4{y, y, y, c.w};
                              });
        }
        else
        {
            const float3 r = row(m_rows[Out_Red]), g = row(m_rows[Out_Green]), b = row(m_rows[Out_Blue]);
            ctx.modify_colors("Channel mixer", [r, g, b](const float4 &c, int2)
                              { return float4{la::dot(r, c.xyz()), la::dot(g, c.xyz()), la::dot(b, c.xyz()), c.w}; });
        }
    }

private:
    //! Which channel the weights are written to, gray among them rather than beside them.
    enum Output : int
    {
        Out_Red = 0,
        Out_Green,
        Out_Blue,
        Out_Monochrome,

        Out_COUNT
    };

    // A row of source weights per output channel, plus the one that makes a single gray from all three.
    // Kept as percentages the way Photoshop presents them, since that is how the numbers are read:
    // "40% of the red channel", not "0.4".
    //
    // Thirds rather than 33.3 each: a monochrome default that announces a total of 99.9% reads as an error
    // in the dialog rather than as the rounding it is.
    float3 m_rows[Out_COUNT] = {
        {100.f, 0.f, 0.f}, {0.f, 100.f, 0.f}, {0.f, 0.f, 100.f}, {100.f / 3.f, 100.f / 3.f, 100.f / 3.f}};
    int  m_output    = Out_Red;
    bool m_normalize = false;
};

class HueSaturation final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Hue/saturation...", "Colorize", "Desaturate"},
                ICON_MY_HUE_SATURATION,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Apply",
                27.f};
    }

    void draw(EditContext &) override
    {
        // The ranges Photoshop uses: hue in degrees around the wheel, the other two as a percentage away
        // from where they are.
        ImGui::SliderFloat("Hue", &m_hue, -180.f, 180.f, "%+.0f deg");
        ImGui::SliderFloat("Saturation", &m_saturation, -100.f, 100.f, "%+.0f%%");
        ImGui::SliderFloat("Lightness", &m_lightness, -100.f, 100.f, "%+.0f%%");
        ImGui::Tooltip("Lightness mixes toward black or white rather than changing the lightness itself, "
                       "which would wash the color out on the way.");

        // The wheel as it is and as the settings would leave it, which is easier to judge than the numbers.
        auto strip = [](const char *id, float h, float s, float l)
        {
            const float  w    = ImGui::GetContentRegionAvail().x;
            const float  step = std::max(1.f, w / 64.f);
            const ImVec2 p    = ImGui::GetCursorScreenPos();
            auto        *dl   = ImGui::GetWindowDrawList();

            for (float x = 0.f; x < w; x += step)
            {
                const float3 rgb =
                    adjust_HSL(HSL_to_RGB(float3{x / w, 1.f, 0.5f}), h / 360.f, (s + 100.f) / 100.f, l / 100.f);
                dl->AddRectFilled(ImVec2(p.x + x, p.y), ImVec2(p.x + x + step, p.y + ImGui::GetFrameHeight()),
                                  ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.x, rgb.y, rgb.z, 1.f)));
            }
            ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
            ImGui::SetItemTooltip("%s", id);
        };
        strip("The hue wheel as it is", 0.f, 0.f, 0.f);
        strip("The hue wheel as these settings would leave it", m_hue, m_saturation, m_lightness);
    }

    void apply(EditContext &ctx) override
    {
        const float h = m_hue / 360.f, s = (m_saturation + 100.f) / 100.f, l = m_lightness / 100.f;
        ctx.modify_colors("Hue/saturation",
                          [h, s, l](const float4 &c, int2) { return float4{adjust_HSL(c.xyz(), h, s, l), c.w}; });
    }

private:
    float m_hue = 0.f, m_saturation = 0.f, m_lightness = 0.f;
};

class Flatten final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Flatten...", "Composite over a background", "Remove transparency"},
                ICON_MY_FLATTEN,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Flatten"};
    }

    void draw(EditContext &ctx) override
    {
        ImGui::TextWrapped("Composites the image over a background color, so what was transparent becomes "
                           "that color and the result is opaque.");
        ImGui::Spacing();

        ImGui::ColorEdit4("Background", &m_bg.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);

        // The color the viewport is already showing behind the image, which is usually the one being
        // matched.
        if (ImGui::Button("Use viewport background"))
            m_bg = ctx.background_color();
        ImGui::Tooltip("Takes the custom background color from the View menu.");
    }

    void apply(EditContext &ctx) override
    {
        const float4 b = m_bg;
        ctx.modify_colors("Flatten",
                          [b](const float4 &c, int2)
                          {
                              // Samples are held premultiplied, so "over" is an addition rather than the
                              // textbook's lerp; the background is given straight and is premultiplied here.
                              return float4{c.xyz() + b.xyz() * b.w * (1.f - c.w), c.w + b.w * (1.f - c.w)};
                          });
    }

private:
    float4 m_bg{0.f, 0.f, 0.f, 1.f};
};

} // namespace

void add_color_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<ConvertColorSpace>());
    out.push_back(std::make_unique<ChannelMixer>());
    out.push_back(std::make_unique<HueSaturation>());
    out.push_back(std::make_unique<Flatten>());
}
