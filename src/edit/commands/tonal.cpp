//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file tonal.cpp
    \author Wojciech Jarosz

    The edits that map a sample to another sample, without regard to where it sits or what is beside it.
*/

#include "edit/commands.h"

#include "colorspace.h"
#include "common.h"
#include "edit/edit_ops.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"
#include "shared/edits.h"

#include <hello_imgui/hello_imgui.h>

#include <cmath>

namespace
{

class Invert final : public EditCommand
{
public:
    Invert() : EditCommand({{"Invert", "Negative"}, ICON_MY_INVERT, ImGuiMod_Ctrl | ImGuiKey_I}) {}

    std::optional<PointEdit> point_edit(const EditContext &) const override { return PointEdit{shader::EDIT_INVERT}; }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Invert", *point_edit(ctx)); }
};

class Clamp final : public EditCommand
{
public:
    Clamp() : EditCommand({{"Clamp to [0,1]", "Clip to LDR range"}, ICON_MY_CLAMP}) {}

    std::optional<PointEdit> point_edit(const EditContext &) const override { return PointEdit{shader::EDIT_CLAMP}; }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Clamp to [0,1]", *point_edit(ctx)); }
};

class ExposureGamma final : public EditCommand
{
public:
    ExposureGamma() : EditCommand({{"Exposure/gamma..."}, ICON_MY_EXPOSURE, ImGuiKey_None, true}) {}

    void draw(EditContext &) override
    {
        draw_curve();

        ImGui::SliderFloat("Exposure", &m_exposure, -10.f, 10.f, "%.2f");
        ImGui::Tooltip("Scale every sample by two to this power, which is what a stop of exposure is.");

        ImGui::SliderFloat("Offset", &m_offset, -1.f, 1.f, "%.3f");
        ImGui::Tooltip("Added after the exposure and before the gamma, so it lifts the black point.");

        ImGui::SliderFloat("Gamma", &m_gamma, MIN_GAMMA, 10.f, "%.3f");
        ImGui::Tooltip("The power the result is raised to, mirrored through the origin so that a negative "
                       "sample keeps its sign.");
    }

    /// The curve the three sliders make, over [0,1] and a little beyond.
    /**
        Only the part landing in the plot is drawn, a little past [0,1]; the operation itself is unbounded.
    */
    void draw_curve()
    {
        const float scale = std::pow(2.f, m_exposure);
        const float inv_g = 1.f / std::max(MIN_GAMMA, m_gamma);

        float ys[ImGui::ToneCurveSamples];
        for (int i = 0; i < ImGui::ToneCurveSamples; ++i) ys[i] = spow(scale * ImGui::ToneCurveX(i) + m_offset, inv_g);

        if (!ImGui::BeginToneCurvePlot("##Curve"))
            return;

        ImGui::ToneCurve("exposure/gamma", ys, ImVec4(1.f, 1.f, 1.f, 0.85f));

        // where a mid-gray input lands
        ImGui::ToneCurveMarkerX("mid", 0.5f, ImVec4(1.f, 1.f, 1.f, 0.25f));

        ImGui::EndToneCurvePlot();
    }

    std::optional<PointEdit> point_edit(const EditContext &) const override
    {
        PointEdit e{shader::EDIT_EXPOSURE_GAMMA};
        e.p0 = float4{std::pow(2.f, m_exposure), m_offset, 1.f / std::max(MIN_GAMMA, m_gamma), 0.f};
        return e;
    }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Exposure/gamma", *point_edit(ctx)); }

private:
    float m_exposure = 0.f, m_offset = 0.f, m_gamma = 1.f;
};

class Fill final : public EditCommand
{
public:
    Fill() : EditCommand({{"Fill..."}, ICON_MY_FILL, ImGuiKey_None, true, "Fill"}) {}

    void draw(EditContext &ctx) override
    {
        ImGui::ColorEdit4("Color", &m_color.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);

        ImGui::RadioButton("Blend over", &m_mode, Mode_Blend);
        ImGui::Tooltip("The color's alpha is how much of it to lay over what is already there. At 1 it "
                       "covers completely; at 0 it changes nothing. Works on any image, with or without an "
                       "alpha channel of its own.");
        ImGui::SameLine();
        ImGui::RadioButton("Replace", &m_mode, Mode_Replace);
        ImGui::Tooltip("Write the color as given, including its alpha, so the filled region takes on that "
                       "transparency. Needs the image to have an alpha channel for the alpha to land "
                       "anywhere.");

        if (m_mode == Mode_Replace)
            if (auto img = ctx.image; img && img->is_valid_group(img->active_group_index(Target_Primary)))
                if (!group_has_alpha(img->groups[size_t(img->active_group_index(Target_Primary))].type))
                    ImGui::TextUnformatted("This channel group has no alpha, so the color's alpha has\n"
                                           "nowhere to go. Blend over instead to use it as coverage.");
    }

    std::optional<PointEdit> point_edit(const EditContext &ctx) const override
    {
        const float4 c = m_color;

        // whether the samples in memory are premultiplied, which finalize() makes them whenever alpha
        // means transparency. Both modes have to match that or the result is out by a factor of alpha.
        bool premultiplied = false;
        int  alpha_slot    = -1;
        if (auto img = ctx.image; img && img->is_valid_group(img->active_group_index(Target_Primary)))
        {
            const auto &group = img->groups[size_t(img->active_group_index(Target_Primary))];
            if (img->transparency != TransparencyType_None && group_has_alpha(group.type))
            {
                premultiplied = true;
                alpha_slot    = group.num_channels - 1;
            }
        }

        // Replace: premultiplied storage wants the color scaled by its own alpha, and the alpha channel itself is
        // stored as given. Blend over: source-over, which premultiplied storage makes the same expression for color
        // and alpha alike.
        if (m_mode == Mode_Replace)
        {
            PointEdit e{shader::EDIT_FILL_REPLACE};
            e.p0 = premultiplied ? float4{c.x * c.w, c.y * c.w, c.z * c.w, c.w} : c;
            return e;
        }
        PointEdit e{shader::EDIT_FILL_BLEND};
        e.p0 = c;
        e.p1 = float4{float(alpha_slot), 0.f, 0.f, 0.f};
        return e;
    }

    void apply(const EditContext &ctx) override { apply_point_edit(ctx, "Fill", *point_edit(ctx)); }

private:
    /// What the color's alpha is taken to mean.
    enum Mode : int
    {
        Mode_Blend = 0, ///< Coverage: lay the color over what is there
        Mode_Replace    ///< Write it outright, alpha channel included
    };

    float4 m_color{0.f, 0.f, 0.f, 1.f};
    int    m_mode = Mode_Blend;
};

} // namespace

void add_tonal_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<Invert>());
    out.push_back(std::make_unique<Clamp>());
    out.push_back(std::make_unique<ExposureGamma>());
    out.push_back(std::make_unique<Fill>());
}
