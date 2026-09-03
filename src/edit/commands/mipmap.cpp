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

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <algorithm>
#include <cmath>

namespace
{
/// How many levels an image of \p size has, counting the image itself, down to a single sample.
int level_count(int2 size) { return 1 + int(std::floor(std::log2(float(std::max(1, std::max(size.x, size.y)))))); }

/// Halve the image repeatedly, adding each level as a separate Image.
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
                     "Generate",
                     26.f})
    {
        // rewrites the whole image into a pyramid, so there is no subject to narrow
        m_info.draws_subject_selector = false;
    }

    bool enabled(const EditContext &ctx) const override
    {
        auto img = ctx.image;
        return img && level_count(img->size()) > 1;
    }

    void on_open(EditContext &ctx) override
    {
        // the whole chain by default, clamped to what this image has
        if (auto img = ctx.image)
            m_levels = std::min(m_levels, level_count(img->size()) - 1);
    }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        const int most = level_count(img->size()) - 1;

        ImGui::SliderInt("Levels", &m_levels, 1, most);
        ImGui::Tooltip("How many halvings to make. Each is a separate image, so they can be compared, "
                       "inspected and saved like any other.");

        m_levels = std::clamp(m_levels, 1, most);

        // the count alone does not say how small it gets
        int2 size = img->size();
        for (int i = 0; i < m_levels && i < 4; ++i)
        {
            size = int2{std::max(1, size.x / 2), std::max(1, size.y / 2)};
            ImGui::TextDisabled("mip %d: %d x %d", i + 1, size.x, size.y);
        }
        if (m_levels > 4)
            ImGui::TextDisabled("... down to mip %d", m_levels);
    }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        const int most = level_count(img->size()) - 1;
        const int want = std::clamp(m_levels, 1, most);

        // each level is halved from the one before, as a mip chain is, and not resampled from the original
        ImagePtr previous = img;
        for (int level = 1; level <= want; ++level)
        {
            const int2 size{std::max(1, previous->size().x / 2), std::max(1, previous->size().y / 2)};

            auto next = previous->duplicate();
            if (!next)
                return;

            // stb's resampler, as Image size uses: it averages over the samples each destination covers
            next->resample(size);
            next->rebuild_layers();

            ctx.add_image(next, fmt::format("mip {}", level));
            previous = next;
        }
    }

private:
    int m_levels = 32; ///< Clamped to what the image has when the dialog opens
};

} // namespace

void add_mipmap_commands(std::vector<EditCommandPtr> &out) { out.push_back(std::make_unique<GenerateMipmaps>()); }
