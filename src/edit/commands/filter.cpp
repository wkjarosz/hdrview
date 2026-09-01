//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file filter.cpp
    \author Wojciech Jarosz

    The edits that read the samples around the one they are writing; see edit/filters.h for the filters
    themselves, which know nothing about how they are asked for.
*/

#include "edit/commands.h"

#include "edit/filters.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <cmath>

namespace
{

//! The border-mode pair the resampling commands offer, which is the same control in both of them.
void border_fields(int *border_x, int *border_y, bool *linked, const char *tooltip)
{
    auto combo = [](const char *label, int *value)
    {
        if (ImGui::BeginCombo(label, border_mode_name(*value)))
        {
            for (int i = 0; i < BorderMode_COUNT; ++i)
                if (ImGui::Selectable(border_mode_name(i), *value == i))
                    *value = i;
            ImGui::EndCombo();
        }
    };

    combo("Border", border_x);
    ImGui::Tooltip("%s", tooltip);

    ImGui::Checkbox("Same in both directions", linked);
    if (!*linked)
        combo("Border (vertical)", border_y);
    else
        *border_y = *border_x;
}

class Blur final : public EditCommand
{
public:
    Info info() const override { return {{"Blur...", "Gaussian blur", "Box blur"}, ICON_MY_BLUR}; }

    void draw(EditContext &) override
    {
        // Addressed by the enum rather than by literal, which is how "Box" came to select the fast
        // Gaussian sitting between them.
        ImGui::RadioButton("Gaussian", &m_kind, Kind_Gaussian);
        ImGui::SameLine();
        ImGui::RadioButton("Fast Gaussian", &m_kind, Kind_FastGaussian);
        ImGui::SameLine();
        ImGui::RadioButton("Box", &m_kind, Kind_Box);

        if (m_kind == Kind_Box)
        {
            ImGui::SliderInt("Half width", &m_half_width, 0, 64);
            ImGui::Checkbox("Same in both directions", &m_link_axes);
            if (!m_link_axes)
                ImGui::SliderInt("Half width (vertical)", &m_half_width_y, 0, 64);

            // Repeating widens the result here, which is the point: this is the box blur as an effect, and
            // n passes of a stated width is the thing being asked for.
            ImGui::SliderInt("Passes", &m_iterations, 1, 16);
            ImGui::Tooltip("Each pass widens the blur. For a Gaussian of a given width, use Fast Gaussian.");
        }
        else
        {
            ImGui::SliderFloat("Sigma", &m_sigma, 0.f, 64.f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Same in both directions", &m_link_axes);
            if (!m_link_axes)
                ImGui::SliderFloat("Sigma (vertical)", &m_sigma_y, 0.f, 64.f, "%.2f", ImGuiSliderFlags_Logarithmic);

            if (m_kind == Kind_FastGaussian)
            {
                // Accuracy alone: the box width is solved for from sigma and the count, so the result stays
                // the width asked for however many passes it takes to get there.
                ImGui::SliderInt("Quality", &m_iterations, 1, 12);
                ImGui::Tooltip("Box blur passes. More is closer to a true Gaussian and costs proportionally "
                               "more; the amount of blur does not change. Three is already hard to tell "
                               "apart, and one is a plain box.");
            }
        }
    }

    void apply(EditContext &ctx) override
    {
        if (m_kind == Kind_Box)
        {
            const int hx = m_half_width, hy = m_link_axes ? m_half_width : m_half_width_y, n = m_iterations;
            ctx.modify_channels_async("Box blur", [hx, hy, n](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                      { return box_blurred(src, r, hx, hy, n, p); });
        }
        else if (m_kind == Kind_FastGaussian)
        {
            const float sx = m_sigma, sy = m_link_axes ? m_sigma : m_sigma_y;
            const int   n = m_iterations;
            ctx.modify_channels_async("Gaussian blur",
                                      [sx, sy, n](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                      { return fast_gaussian_blurred(src, r, sx, sy, n, p); });
        }
        else
        {
            const float sx = m_sigma, sy = m_link_axes ? m_sigma : m_sigma_y;
            ctx.modify_channels_async("Gaussian blur", [sx, sy](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                      { return gaussian_blurred(src, r, sx, sy, p); });
        }
    }

private:
    //! What kernel is wanted, which decides what the controls above mean.
    enum Kind : int
    {
        Kind_Gaussian = 0, //!< The real thing, at a cost that grows with sigma
        Kind_FastGaussian, //!< Repeated boxes converging on it, at a cost independent of sigma
        Kind_Box           //!< Boxes as an effect in their own right
    };

    int   m_kind         = Kind_Gaussian;
    float m_sigma        = 2.f;
    float m_sigma_y      = 2.f;
    int   m_half_width   = 2;
    int   m_half_width_y = 2;
    int   m_iterations   = 6;
    bool  m_link_axes    = true;
};

class UnsharpMask final : public EditCommand
{
public:
    Info info() const override { return {{"Unsharp mask...", "Sharpen"}, ICON_MY_SHARPEN}; }

    void draw(EditContext &) override
    {
        ImGui::SliderFloat("Sigma", &m_sigma, 0.f, 64.f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::Tooltip("The size of the detail affected: small values sharpen fine texture, large ones "
                       "raise local contrast.");
        ImGui::SliderFloat("Amount", &m_amount, 0.f, 8.f, "%.2f");
        ImGui::Tooltip("How much of what the blur removed is added back. Zero changes nothing.");
    }

    void apply(EditContext &ctx) override
    {
        const float s = m_sigma, a = m_amount;
        ctx.modify_channels_async("Unsharp mask", [s, a](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                  { return unsharp_masked(src, r, s, a, p); });
    }

private:
    float m_sigma = 2.f, m_amount = 1.f;
};

class Median final : public EditCommand
{
public:
    Info info() const override { return {{"Median filter...", "Remove fireflies and outliers"}, ICON_MY_MEDIAN}; }

    void draw(EditContext &) override
    {
        ImGui::SliderFloat("Radius", &m_radius, 0.f, 32.f, "%.1f");

        ImGui::Checkbox("Circular window", &m_disc);
        ImGui::Tooltip("A square window reaches root-two farther at its corners than along its axes, which "
                       "leaves a faint squareness in what it removes.");
        ImGui::Tooltip("Removes lone outliers -- fireflies in a render -- without the smearing a blur "
                       "would cause. Costs the area of the disc per sample, so a large radius is slow.");
    }

    void apply(EditContext &ctx) override
    {
        const float r = m_radius;
        const bool  d = m_disc;
        ctx.modify_channels_async("Median filter", [r, d](const Array2Df &src, const Box2i &region, AtomicProgress p)
                                  { return median_filtered(src, region, r, d, p); });
    }

private:
    float m_radius = 2.f;
    bool  m_disc   = true;
};

class Shift final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Shift...", "Offset", "Translate", "Wrap around"},
                ICON_MY_SHIFT,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Shift"};
    }

    void draw(EditContext &) override
    {
        ImGui::DragFloat2("X, Y offset", &m_offset.x, 0.25f, 0.f, 0.f, "%.2f px");
        ImGui::Tooltip("Positive moves the image right and down. Fractional offsets are allowed, and are "
                       "what the sampler below is for.");

        border_fields(&m_border_x, &m_border_y, &m_link_borders,
                      "What is read where the shift reaches past the edge. Repeat is the wrapping shift: "
                      "what leaves one side comes back in on the other, so a tiling texture stays tiling.");

        // Only asked for when it is consulted: a whole-sample offset reads samples exactly, whichever this
        // says.
        ImGui::BeginDisabled(m_offset.x == std::floor(m_offset.x) && m_offset.y == std::floor(m_offset.y));
        if (ImGui::BeginCombo("Sampler", sampler_name(m_sampler)))
        {
            for (int i = 0; i < Sampler_COUNT; ++i)
                if (ImGui::Selectable(sampler_name(i), m_sampler == i))
                    m_sampler = i;
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::Tooltip("How the samples between samples are reconstructed, which a whole-number offset never "
                       "has to ask.");
    }

    void apply(EditContext &ctx) override
    {
        const float2 d  = m_offset;
        const int    s  = m_sampler;
        const int    bx = m_border_x, by = m_link_borders ? m_border_x : m_border_y;
        ctx.modify_channels("Shift", [d, s, bx, by](const Array2Df &src, const Box2i &r)
                            { return shifted(src, r, d.x, d.y, s, bx, by); });
    }

private:
    float2 m_offset{0.f, 0.f};
    int    m_sampler      = Sampler_Bilinear;
    int    m_border_x     = BorderMode_Repeat;
    int    m_border_y     = BorderMode_Repeat;
    bool   m_link_borders = true;
};

class ZapGremlins final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Zap gremlins...", "Replace NaNs and infinities"},
                ICON_MY_ZAP_GREMLINS,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Zap"};
    }

    void draw(EditContext &ctx) override
    {
        ImGui::TextWrapped("Gremlins are NaNs or infinities that can corrupt various image operations. Here you can "
                           "replace them with a specified color, or with the median of their neighborhood.");
        ImGui::Spacing();

        if (auto img = ctx.image())
            if (auto *stats = img->channels[img->groups[img->selected_group].channels[0]].get_stats())
                if (stats->computed)
                    ImGui::TextFmt("{} NaN and {} infinite samples in this channel.", stats->summary.nan_pixels,
                                   stats->summary.inf_pixels);

        // The two 1.8 offered: take what the neighbors say, or write something chosen. The first is almost
        // always what is wanted; the second is there for when a run of them has no good neighbor to ask.
        ImGui::RadioButton("Median of neighbors", &m_mode, Mode_Median);
        ImGui::Tooltip("Puts back something the surrounding samples agree with, so a firefly in a smooth "
                       "region leaves no trace.");
        ImGui::SameLine();
        ImGui::RadioButton("Fill with", &m_mode, Mode_Value);

        ImGui::BeginDisabled(m_mode != Mode_Value);
        ImGui::ColorEdit4("Value", &m_value.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);
        ImGui::EndDisabled();
    }

    void apply(EditContext &ctx) override
    {
        if (m_mode == Mode_Median)
            ctx.modify_channels("Zap gremlins",
                                [](const Array2Df &src, const Box2i &r) { return zapped_gremlins(src, r); });
        else
        {
            // Per channel, so the chosen color reaches the component it belongs to.
            const float4 v = m_value;
            ctx.modify_pixels("Zap gremlins",
                              [v](float s, int2, int slot) { return std::isfinite(s) ? s : v[slot % 4]; });
        }
    }

private:
    enum Mode : int
    {
        Mode_Median = 0,
        Mode_Value
    };

    int    m_mode = Mode_Median;
    float4 m_value{0.f, 0.f, 0.f, 1.f};
};

class BumpToNormal final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Bump to normal map...", "Height to normal", "Normal map"},
                ICON_MY_NORMAL_MAP,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Convert",
                27.f};
    }

    void draw(EditContext &) override
    {
        ImGui::TextWrapped("Reads the image as a height field -- the average of its channels -- and writes "
                           "the surface normal that height field would have, encoded so that a component of "
                           "zero is 0.5.");
        ImGui::Spacing();

        ImGui::DragFloat("Scale", &m_scale, 0.01f, 0.f, 100.f, "%.2f");
        ImGui::Tooltip("How steep the slopes are taken to be. The height is measured across the whole image "
                       "rather than per sample, so a normal map keeps its look when the image is resized.");

        border_fields(&m_border_x, &m_border_y, &m_link_borders,
                      "What lies past the edge when the slope at the last row or column is measured. "
                      "Repeat is what a tiling texture wants, so its normals tile too.");

        ImGui::Checkbox("Flip green", &m_flip_y);
        ImGui::Tooltip("Whether the green channel points up or down the image. Which one a renderer wants "
                       "is a convention it chooses, and the two are commonly called OpenGL and DirectX.");
    }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        // Slopes in the image's own coordinates, which is why the size enters: 1.8 measured height per unit
        // of the whole image rather than per sample, so the same bump map gives the same normals whatever
        // resolution it is stored at.
        const float2 size{float(img->size().x), float(img->size().y)};
        const float  s      = m_scale;
        const float  y_sign = m_flip_y ? -1.f : 1.f;

        ctx.modify_neighborhood(
            "Bump to normal map",
            [s, size, y_sign](const std::function<float4(int2)> &read, int2 p)
            {
                auto height = [&read](int2 q)
                {
                    const float4 c = read(q);
                    return (c.x + c.y + c.z) / 3.f;
                };

                // Forward differences, as 1.8 took them: one sample along each axis is the finest slope the
                // samples can express.
                const float h00 = height(p);
                const float dx  = height(p + int2{1, 0}) - h00;
                const float dy  = height(p + int2{0, 1}) - h00;

                // The surface z = h(x,y) has normal (-dh/dx, -dh/dy, 1); the sign is folded into the scale
                // so that a slope rising to the right leans the normal to the right.
                float3 n = la::normalize(float3{s * dx * size.x, y_sign * s * dy * size.y, 1.f});

                // Into the [0,1] range a normal map is stored in, where flat is (0.5, 0.5, 1).
                n = n * 0.5f + 0.5f;
                return float4{n, 1.f};
            },
            m_border_x, m_link_borders ? m_border_x : m_border_y);
    }

private:
    float m_scale        = 1.f;
    int   m_border_x     = BorderMode_Edge;
    int   m_border_y     = BorderMode_Edge;
    bool  m_link_borders = true;
    bool  m_flip_y       = false;
};

} // namespace

void add_filter_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<Blur>());
    out.push_back(std::make_unique<UnsharpMask>());
    out.push_back(std::make_unique<Median>());
    out.push_back(std::make_unique<Shift>());
    out.push_back(std::make_unique<ZapGremlins>());
    out.push_back(std::make_unique<BumpToNormal>());
}
