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

constexpr float k_radians_per_degree = float(M_PI) / 180.f;

/// Continue the line, beyond the last item's label, at the dialog's right edge for something \p width wide.
void same_line_at_right(float width)
{
    ImGui::SameLine();
    if (const float slack = ImGui::GetContentRegionAvail().x - width; slack > 0.f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack);
}

class Remap final : public EditCommand
{
public:
    Remap() :
        EditCommand({{"Remap envmap...", "Change environment map format", "Spherical remapping"},
                     ICON_MY_ENVMAP,
                     ImGuiKey_None,
                     true,
                     "Remap",
                     27.f})
    {
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
    /// Gives back the view a preview took over, unless the remap was applied and its result is framed in it.
    void on_close(EditContext &ctx) override
    {
        if (framing() && !m_applied)
            ctx.set_view(m_view);
        m_framed  = Box2i{};
        m_applied = false;
        m_size    = int2{0};
    }

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
        ImGui::DragInt2("Size", &m_size.x, 1.f, 1, 65536, "%d px");
        m_size = la::max(m_size, int2{1});

        const auto &style = ImGui::GetStyle();
        same_line_at_right(ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize("Auto aspect").x);
        ImGui::Checkbox("Auto aspect", &m_auto_aspect);
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

        // The rotation as yaw, pitch and roll, which a drag across the preview turns too. The sliders keep their own
        // angles, since at a quarter-turn pitch the rotation cannot say how much of a turn is yaw and how much roll,
        // and the rotation is a quaternion, so that drags compose without drifting.
        const float3 before_angles = m_angles;
        ImGui::SliderFloat("Yaw", &m_angles.x, -180.f, 180.f, "%.1f deg");
        ImGui::Tooltip("Turn the sphere about its up axis, which slides a lat-long sideways.");
        ImGui::SliderFloat("Pitch", &m_angles.y, -90.f, 90.f, "%.1f deg");
        ImGui::Tooltip("Tip the sphere forward or back.");
        ImGui::SliderFloat("Roll", &m_angles.z, -180.f, 180.f, "%.1f deg");
        ImGui::Tooltip("Turn the sphere about the forward axis.");
        same_line_at_right(ImGui::CalcTextSize("Reset rotation").x + 2.f * style.FramePadding.x);
        if (ImGui::Button("Reset rotation"))
            m_angles = float3{0.f};
        ImGui::Tooltip("Drag across the preview to turn the sphere, keeping what is under the cursor under it; "
                       "Alt-drag pans the view instead.");
        if (m_angles != before_angles)
        {
            const float3 r = m_angles * k_radians_per_degree;
            m_rotation     = la::rotation_quat(envmap_rotation(r.x, r.y, r.z));
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
                           "this is what keeps a stretched footprint sharp, and what it costs, since each "
                           "step along that axis is another texel read.");
        }
        else
        {
            ImGui::SliderInt("Samples per axis", &m_supersample, 1, 8);
            ImGui::Tooltip("Averaged within each output pixel, so the cost is its square.");
        }

        // Previewed, the view frames the output as applying it will, fitting it when the preview starts and whenever
        // the output changes shape, so the result lands where its preview was; unpreviewed, the view is given back.
        if (ctx.previewing)
        {
            const Box2i window = output_window(*img);
            if (!framing())
                m_view = ctx.view();
            if (window != m_framed)
            {
                ctx.fit_view(window);
                m_framed = window;
            }
        }
        else if (framing())
        {
            ctx.set_view(m_view);
            m_framed = Box2i{};
        }
    }

    std::optional<EnvmapRemap> remap_preview(const EditContext &) const override
    {
        if (m_size.x <= 0 || m_size.y <= 0)
            return std::nullopt;
        return EnvmapRemap{m_src_mapping, m_dst_mapping, m_size, la::qmat(m_rotation)};
    }

    /// Turns the sphere so that what was seen at \p from is seen at \p to.
    bool drag_preview(float2 from, float2 to) override
    {
        // the rotation that takes the result's direction at to onto the one at from, applied before the turn so
        // far, so that the source seen at from comes to be seen at to
        const auto d  = EnvMapping(m_dst_mapping);
        const auto at = [d](float2 uv) { return envmap_uv_to_xyz(d, nearest_valid_envmap_uv(d, uv)); };
        m_rotation    = la::normalize(la::qmul(m_rotation, rotation_between(at(to), at(from))));
        // the sliders follow, keeping their roll where pitch has locked it to the yaw, and adding zero so that a
        // pitch of -0 shows as 0
        m_angles = envmap_euler(la::qmat(m_rotation), m_angles.z * k_radians_per_degree) / k_radians_per_degree + 0.f;
        return true;
    }

    void apply(const EditContext &ctx) override
    {
        const auto s = EnvMapping(m_src_mapping), d = EnvMapping(m_dst_mapping);
        const auto rotation = la::qmat(m_rotation);
        const int2 out_size = m_size;
        const int  ss       = m_supersample;
        const auto mode     = EnvMapSampling(m_sampling);

        if (out_size.x <= 0 || out_size.y <= 0)
            return;

        ctx.resample_image_async(
            "Remap envmap", out_size, [s, d, out_size, ss, mode, rotation](const Array2Df &src, AtomicProgress p)
            { return remapped_envmap(src, out_size, d, s, mode, ss, rotation, p); },
            framing() ? Extent_StructureInView : Extent_Structure);
        m_applied = true;
    }

private:
    /// The display window the remap leaves the image with, which keeps its data window's corner.
    Box2i output_window(const Image &img) const { return Box2i{img.data_window.min, img.data_window.min + m_size}; }
    /// Whether a preview has the view framing the output.
    bool framing() const { return m_framed.has_volume(); }

    float4    m_rotation{0.f, 0.f, 0.f, 1.f}; ///< As a unit quaternion (x, y, z, w), turning the result into the source
    float3    m_angles{0.f}; ///< The rotation's yaw, pitch and roll in degrees, as the sliders hold them
    ViewState m_view;   ///< What the view was before a preview took it over
    Box2i     m_framed; ///< The output window the view was last fit to, while a preview frames it
    bool      m_applied = false;

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
                     true,
                     "Convolve",
                     27.f})
    {
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

        ImGui::DragInt2("Size", &m_size.x, 1.f, 1, 8192, "%d px");
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

        ctx.resample_image_async(
            "Irradiance envmap", out_size, [m, out_size](const Array2Df &src, AtomicProgress p)
            { return irradiance_envmap(src, out_size, m, p); }, Extent_Structure);
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
