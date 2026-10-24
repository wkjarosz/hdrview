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

class Remap final : public EditCommand
{
public:
    Remap() :
        EditCommand({{"Remap envmap...", "Change environment map format", "Spherical remapping"},
                     ICON_MY_ENVMAP,
                     ImGuiKey_None,
                     true,
                     "Remap"})
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
                m_size.x = aspect_width();
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
        if (ImGui::RowBracketButton(ICON_MY_SWAP, mappings, true, "Swap the source and target mappings."))
            std::swap(m_src_mapping, m_dst_mapping);

        const int2 before = m_size;
        ImGui::DragInt2("Size", &m_size.x, 1.f, 1, 65536, "%d px");
        m_size = la::max(m_size, int2{1});

        const auto &style = ImGui::GetStyle();
        ImGui::SameLine();
        {
            ImGui::RightAligned right(ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
                                      ImGui::CalcTextSize("Auto aspect").x);
            ImGui::Checkbox("Auto aspect", &m_auto_aspect);
            ImGui::Tooltip("Keep the width and height at the target mapping's aspect ratio (e.g. 2:1 for a lat-long, "
                           "3:4 for a cube cross).");
        }

        // follow whichever was just changed: a new target re-derives the width from its aspect, and
        // editing one field drives the other
        if (m_auto_aspect)
        {
            if (m_dst_mapping != before_dst || m_size.y != before.y)
                m_size.x = aspect_width();
            else if (m_size.x != before.x)
                m_size.y = std::max(1, int(std::lround(double(m_size.x) / double(envmapping_aspect(m_dst_mapping)))));
        }

        // The sliders keep their own angles, since at a quarter-turn pitch the rotation cannot tell yaw from roll. The
        // rotation itself is a quaternion, so the many small turns of a drag compose exactly, and the sliders follow
        // it.
        const float3 before_angles = m_angles;
        ImGui::SliderFloat("Yaw", &m_angles.x, -180.f, 180.f, "%.1f deg");
        ImGui::Tooltip("Rotate the environment around the vertical axis. In a lat-long this shifts the image "
                       "horizontally.");
        ImGui::SliderFloat("Pitch", &m_angles.y, -90.f, 90.f, "%.1f deg");
        ImGui::Tooltip("Tilt the environment up or down.");
        ImGui::SliderFloat("Roll", &m_angles.z, -180.f, 180.f, "%.1f deg");
        ImGui::Tooltip("Rotate the environment around the forward axis.");
        ImGui::SameLine();
        {
            ImGui::RightAligned right(ImGui::CalcTextSize("Reset rotation").x + 2.f * style.FramePadding.x);
            if (ImGui::Button("Reset rotation"))
                m_angles = float3{0.f};
            ImGui::Tooltip("Set yaw, pitch and roll back to zero.\n\nYou can also rotate the environment by "
                           "dragging the preview. Alt-drag to pan the view instead.");
        }
        if (m_angles != before_angles)
        {
            const float3 r = m_angles * k_radians_per_degree;
            m_rotation     = la::rotation_quat(envmap_rotation(r.x, r.y, r.z));
        }

        ImGui::RadioButton("EWA", &m_sampling, EnvMapSampling_EWA);
        ImGui::Tooltip("Filter each output pixel over the elliptical region it covers in the source, using "
                       "mipmaps. Avoids aliasing without overblurring, even where the mapping stretches the image "
                       "a lot (e.g. near the poles of a lat-long).");
        ImGui::SameLine();
        ImGui::RadioButton("Point", &m_sampling, EnvMapSampling_Point);
        ImGui::Tooltip("Average samples spread around each output pixel in a Gaussian. Can alias when shrinking "
                       "the image by more than the number of samples per axis.");

        if (m_sampling == EnvMapSampling_EWA)
        {
            ImGui::SliderInt("Max anisotropy", &m_max_aniso, 1, 32);
            ImGui::Tooltip("Set how elongated a pixel's footprint in the source can get before it is blurred to "
                           "fit. Higher values keep stretched regions sharper, but are slower.");
        }
        else
        {
            ImGui::SliderInt("Samples per axis", &m_samples, 1, 8);
            ImGui::Tooltip("Set the number of samples along each axis of an output pixel. The cost grows with the "
                           "square of this value.");
            ImGui::BeginDisabled(m_samples <= 1);
            ImGui::SliderFloat("Sigma", &m_sigma, 0.2f, 1.5f, "%.2f px");
            ImGui::EndDisabled();
            ImGui::Tooltip("Set the width of the Gaussian the samples are spread over, in output pixels. Larger "
                           "values blur more and alias less. A single sample reads the pixel's center, whatever its "
                           "width. Ctrl-click to type a value outside the slider's range.");
            // the range is for dragging; a value typed in past it is kept, as long as it is not negative
            m_sigma = std::max(m_sigma, 0.f);
        }

        // While previewing, fit the view to the output when the preview starts and whenever the output's size changes,
        // so the applied result lands where the preview was. When previewing stops, restore the view.
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
        return EnvmapRemap{m_src_mapping, m_dst_mapping, m_size, la::qmat(m_rotation), m_sampling, quality(), m_sigma};
    }

    /// Turns the sphere so that what was seen at \p from is seen at \p to.
    bool drag_preview(float2 from, float2 to, float2) override
    {
        // R' = R Q, with Q turning the output direction at to onto the one at from, so the output at to now shows what
        // it showed at from
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
        const auto r = remap_preview(ctx);
        if (!r)
            return;
        ctx.resample_image_async(
            "Remap envmap", r->size,
            [r = *r](const Array2Df &src, AtomicProgress p)
            {
                return remapped_envmap(src, r.size, EnvMapping(r.dst), EnvMapping(r.src), EnvMapSampling(r.sampling),
                                       r.quality, r.sigma, r.rotation, p);
            },
            framing() ? Extent_StructureInView : Extent_Structure);
        m_applied = true;
    }

private:
    /// The width the target mapping's aspect gives the current height.
    int aspect_width() const
    { return std::max(1, int(std::lround(double(m_size.y) * double(envmapping_aspect(m_dst_mapping))))); }
    /// The display window the remap leaves the image with, which keeps its data window's corner.
    Box2i output_window(const Image &img) const { return Box2i{img.data_window.min, img.data_window.min + m_size}; }
    /// What remapped_envmap() takes as its supersample count: samples per axis, or EWA's maximum anisotropy.
    int quality() const { return m_sampling == EnvMapSampling_EWA ? m_max_aniso : m_samples; }
    /// Whether a preview has the view framing the output.
    bool framing() const { return m_framed.has_volume(); }

    float4    m_rotation{0.f, 0.f, 0.f, 1.f}; ///< As a unit quaternion (x, y, z, w), turning the result into the source
    float3    m_angles{0.f}; ///< The rotation's yaw, pitch and roll in degrees, as the sliders hold them
    ViewState m_view;   ///< What the view was before a preview took it over
    Box2i     m_framed; ///< The output window the view was last fit to, while a preview frames it
    bool      m_applied = false;

    int   m_src_mapping = EnvMapping_LatLong;
    int   m_dst_mapping = EnvMapping_Angular;
    int2  m_size{0, 0};
    int   m_samples     = 8;    ///< Per axis, when supersampling
    float m_sigma       = 0.5f; ///< The supersampling Gaussian's standard deviation, in output pixels
    int   m_max_aniso   = 8;    ///< For EWA
    int   m_sampling    = EnvMapSampling_EWA;
    bool  m_auto_aspect = true;
};

class Irradiance final : public EditCommand
{
public:
    Irradiance() :
        EditCommand({{"Irradiance envmap...", "Diffuse convolution", "Cosine convolution"},
                     ICON_MY_IRRADIANCE,
                     ImGuiKey_None,
                     true,
                     "Convolve"})
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
        ImGui::Tooltip("Choose how the environment map is laid out. The result uses the same mapping.");

        ImGui::DragInt2("Size", &m_size.x, 1.f, 1, 8192, "%d px");
        m_size = la::max(m_size, int2{1});
        ImGui::Tooltip("Set the size of the result. Irradiance is very smooth, so a small size loses no detail.");
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
