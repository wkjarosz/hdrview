//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file envmap_commands.cpp
    \author Wojciech Jarosz

    The edits that read an image as a parameterization of the sphere.
*/

#include "edit/commands.h"

#include "edit/envmap.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <utility>

namespace
{

void mapping_combo(const char *label, int *value)
{
    if (ImGui::BeginCombo(label, envmapping_name(*value)))
    {
        for (int i = 0; i < EnvMapping_COUNT; ++i)
            if (ImGui::Selectable(envmapping_name(i), *value == i))
                *value = i;
        ImGui::EndCombo();
    }
}

class Remap final : public EditCommand
{
public:
    Remap() :
        EditCommand({{"Remap envmap...", "Change environment map format", "Spherical remapping"},
                     ICON_MY_ENVMAP,
                     ImGuiKey_None,
                     "Remap",
                     27.f})
    {
        m_info.has_dialog = true;

        // reparameterizes the whole image, so there is no subject to narrow
        m_info.draws_subject_selector = false;
    }

    /// Opens at the current image's size, then follows the target mapping's aspect.
    void on_open(EditContext &ctx) override
    {
        if (auto img = ctx.image)
        {
            m_size = img->size();
            if (m_auto_aspect)
                m_size.x = std::max(1, int(std::lround(double(m_size.y) * double(envmapping_aspect(m_dst_mapping)))));
        }
    }
    void on_close(EditContext &) override { m_size = int2{0}; }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        if (m_size.x <= 0 || m_size.y <= 0)
            m_size = img->size();

        ImGui::RowSpan mappings;

        mapping_combo("Source", &m_src_mapping);
        mappings.take();

        const int before_dst = m_dst_mapping;
        mapping_combo("Target", &m_dst_mapping);
        mappings.take();

        // between the two it exchanges, bracketed like the chain that ties a width to a height
        if (ImGui::RowBracketButton(ICON_MY_SWAP, mappings, true, "Exchange source and target."))
            std::swap(m_src_mapping, m_dst_mapping);

        const int2 before = m_size;
        ImGui::DragInt2("Width, height", &m_size.x, 1.f, 1, 65536, "%d px");
        m_size = la::max(m_size, int2{1});

        ImGui::Checkbox("Auto aspect ratio", &m_auto_aspect);
        ImGui::Tooltip("Keep the width and height in the proportion the target parameterization wants: 2:1 "
                       "for a lat-long, square for a disc, 3:4 for a cube cross.");

        // follow whichever was just changed: a new target re-derives the width from its aspect, and
        // editing one field drives the other
        if (m_auto_aspect)
        {
            const float aspect = envmapping_aspect(m_dst_mapping);
            if (m_dst_mapping != before_dst || m_size.y != before.y)
                m_size.x = std::max(1, int(std::lround(double(m_size.y) * double(aspect))));
            else if (m_size.x != before.x)
                m_size.y = std::max(1, int(std::lround(double(m_size.x) / double(aspect))));
        }

        ImGui::RadioButton("EWA", &m_sampling, EnvMapSampling_EWA);
        ImGui::Tooltip("Reads a mip pyramid through an ellipse shaped by the area each output pixel covers "
                       "in the source. That area is far wider than it is tall near a lat-long's poles, "
                       "which is the case a mip level on its own cannot represent. Costs the same whatever "
                       "the scale.");
        ImGui::SameLine();
        ImGui::RadioButton("Point", &m_sampling, EnvMapSampling_Point);
        ImGui::Tooltip("Averages a grid of samples inside each output pixel. Exact when enlarging, but a "
                       "reduction of more than the sample count still aliases.");

        if (m_sampling == EnvMapSampling_EWA)
        {
            ImGui::SliderInt("Max anisotropy", &m_supersample, 1, 32);
            ImGui::Tooltip("How much longer than it is wide the footprint may be before it is widened to "
                           "fit. The mip level covers the short axis and the filter walks the long one, so "
                           "this is what keeps a stretched footprint sharp -- and what it costs, since each "
                           "step along that axis is another texel read.");
        }
        else
        {
            ImGui::SliderInt("Samples per axis", &m_supersample, 1, 8);
            ImGui::Tooltip("Averaged within each output pixel, so the cost is its square.");
        }
    }

    void apply(const EditContext &ctx) override
    {
        const auto s = EnvMapping(m_src_mapping), d = EnvMapping(m_dst_mapping);
        const int2 out_size = m_size;
        const int  ss       = m_supersample;
        const auto mode     = EnvMapSampling(m_sampling);

        if (out_size.x <= 0 || out_size.y <= 0)
            return;

        // no bias: the level the footprint asks for is the right one
        ctx.resample_image_async("Remap envmap", out_size,
                                 [s, d, out_size, ss, mode](const Array2Df &src, AtomicProgress p)
                                 { return remapped_envmap(src, out_size, d, s, mode, ss, 0.f, p); });
    }

private:
    int  m_src_mapping = EnvMapping_LatLong;
    int  m_dst_mapping = EnvMapping_Angular;
    int2 m_size{0, 0};
    int  m_supersample = 8;
    int  m_sampling    = EnvMapSampling_EWA;
    bool m_auto_aspect = true;
};

class Irradiance final : public EditCommand
{
public:
    Irradiance() :
        EditCommand({{"Irradiance envmap...", "Diffuse convolution", "Cosine convolution"},
                     ICON_MY_IRRADIANCE,
                     ImGuiKey_None,
                     "Convolve",
                     27.f})
    {
        m_info.has_dialog = true;

        m_info.draws_subject_selector = false;
    }

    /// The current image's size, since the result is usually looked at beside it.
    void on_open(EditContext &ctx) override
    {
        if (auto img = ctx.image)
            m_size = img->size();
    }
    void on_close(EditContext &) override { m_size = int2{0}; }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image;
        if (img && (m_size.x <= 0 || m_size.y <= 0))
            m_size = img->size();

        mapping_combo("Mapping", &m_mapping);

        ImGui::DragInt2("Width, height", &m_size.x, 1.f, 1, 8192, "%d px");
        m_size = la::max(m_size, int2{1});
        ImGui::Tooltip("The result is smooth enough that a small output loses nothing: it is described by "
                       "nine numbers however large it is written out.");
    }

    void apply(const EditContext &ctx) override
    {
        const auto m        = EnvMapping(m_mapping);
        const int2 out_size = m_size;

        if (out_size.x <= 0 || out_size.y <= 0)
            return;

        ctx.resample_image_async("Irradiance envmap", out_size, [m, out_size](const Array2Df &src, AtomicProgress p)
                                 { return irradiance_envmap(src, out_size, m, p); });
    }

private:
    int  m_mapping = EnvMapping_LatLong;
    int2 m_size{0, 0};
};

} // namespace

void add_envmap_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<Remap>());
    out.push_back(std::make_unique<Irradiance>());
}
