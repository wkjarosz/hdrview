//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file mipmap.cpp
    \author Wojciech Jarosz

    Building a mip pyramid from an image.
*/

#include "edit/commands.h"

#include "edit/filters.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
/// Shrink the image level by level, adding each level as a separate Image.
/**
    Separate Images because all the channels of one Image share a data window. Unrelated to OpenEXR's own
    mip levels, which are not read.
*/
class GenerateMipmaps final : public EditCommand
{
public:
    GenerateMipmaps() :
        EditCommand({{"Generate mipmaps...", "Build a mip pyramid", "Halve repeatedly"},
                     ICON_MY_CHANNEL_GROUP,
                     ImGuiKey_None,
                     true,
                     "Generate"})
    {
        // rewrites the whole image into a pyramid, so there is no subject to narrow
        m_info.draws_subject_selector = false;
    }

    bool enabled(const EditContext &ctx) const override
    {
        auto img = ctx.image;
        return img && la::maxelem(img->size()) > 1;
    }

    void on_open(EditContext &ctx) override
    {
        // the whole chain by default, clamped to what this image has
        if (auto img = ctx.image)
            m_levels = std::min(m_levels, int(mip_level_sizes(img->size(), m_per_octave).size()));
    }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        if (ImGui::BeginCombo("Filter", mip_filter_name(m_filter)))
        {
            for (int i = 0; i < MipFilter_COUNT; ++i)
                if (ImGui::Selectable(mip_filter_name(i), m_filter == i))
                    m_filter = i;
            ImGui::EndCombo();
        }
        ImGui::Tooltip("Set how each level is filtered from the one before. Box averages what each sample covers, "
                       "Mitchell is a sharper cubic, and Gaussian blurs each level by the width set below, as if "
                       "it were filtered from the image itself.");

        if (m_filter == MipFilter_Gaussian)
        {
            ImGui::SliderFloat("Sigma", &m_sigma, 0.2f, 1.5f, "%.2f px");
            ImGui::Tooltip("Set the width of the Gaussian, in each level's pixels. Larger values blur more and alias "
                           "less.");
            // the range is for dragging; a value typed in past it is kept, as long as it is not negative
            m_sigma = std::max(m_sigma, 0.f);
        }

        const int per_octave = m_per_octave;
        ImGui::SliderInt("Levels per octave", &m_per_octave, 1, 4);
        ImGui::Tooltip("Set how many levels to make for each halving of the size. One halves the image each level; "
                       "two shrinks it by the square root of two, so every other level is half the resolution.");
        m_per_octave = std::clamp(m_per_octave, 1, 4);

        const auto sizes = mip_level_sizes(img->size(), m_per_octave);
        const int  most  = int(sizes.size());
        // the same depth, in octaves, as the count of levels goes up or down with them
        if (m_per_octave != per_octave)
            m_levels = (m_levels * m_per_octave + per_octave - 1) / per_octave;

        ImGui::SliderInt("Levels", &m_levels, 1, most);
        ImGui::Tooltip("How many levels to make. Each is a separate image, so they can be compared, "
                       "inspected and saved like any other.");
        m_levels = std::clamp(m_levels, 1, most);

        // the count alone does not say how small it gets
        for (int i = 0; i < m_levels && i < 4; ++i)
            ImGui::TextDisabled("mip %d: %d x %d", i + 1, sizes[i].x, sizes[i].y);
        if (m_levels > 4)
            ImGui::TextDisabled("... down to mip %d: %d x %d", m_levels, sizes[m_levels - 1].x, sizes[m_levels - 1].y);
    }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        const auto sizes = mip_level_sizes(img->size(), m_per_octave);
        const int  want  = std::clamp(m_levels, 1, int(sizes.size()));

        // each level is shrunk from the one before, as a mip chain is, and not resampled from the original; a
        // Gaussian's blur is counted along the chain, the image itself having none
        ImagePtr previous = img;
        float    blurred  = 0.f;
        for (int level = 1; level <= want; ++level)
        {
            const int2 size = sizes[size_t(level - 1)];
            auto       next = previous->duplicate();
            if (!next)
                return;

            for (auto &channel : next->channels)
            {
                const Array2Df out = mip_level(channel, size, m_filter, m_sigma, blurred);
                channel.resize(size);
                std::copy(out.data(), out.data() + out.num_elements(), channel.data());
                channel.texture_is_dirty = true;
            }
            next->data_window    = Box2i{next->data_window.min, next->data_window.min + size};
            next->display_window = next->data_window;
            next->rebuild_layers();

            ctx.add_image(next, fmt::format("mip {}", level));
            previous = next;
            blurred  = m_sigma;
        }
    }

private:
    int   m_levels     = 32; ///< Clamped to what the image has when the dialog opens
    int   m_per_octave = 1;
    int   m_filter     = MipFilter_Mitchell;
    float m_sigma      = 0.5f; ///< The Gaussian's standard deviation, in each level's own samples
};

} // namespace

void add_mipmap_commands(std::vector<EditCommandPtr> &out) { out.push_back(std::make_unique<GenerateMipmaps>()); }
