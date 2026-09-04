//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/edit_ops.h"

#include "image.h"

#include <numeric>
#include <smallthreadpool.h>
#include <spdlog/spdlog.h>

using std::function;
using std::string;

namespace
{

/// Compute a rectangle of one or more channels in parallel, then upload it.
/**
    \p op fills the staging arrays for the rows [y0, y1) of the rectangle: one array per entry of
    \p channels, each sized to the rectangle and indexed from its corner. Nothing reaches the image until
    every row is staged, so an op may read the pixels it is replacing.
*/
void write_rect(Image &image, const std::vector<int> &channels, const Box2i &bounds,
                const function<void(int y0, int y1, std::vector<Array2Df> &staging)> &op)
{
    const int2 offset = bounds.min - image.data_window.min;
    const int2 extent = bounds.size();

    std::vector<Array2Df> staging(channels.size());
    for (auto &s : staging) s = Array2Df{extent};

    const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
    stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                      [&](int y0, int y1, int, int) { op(y0, y1, staging); });

    // upload_tile() writes the pixels and pushes just this rectangle to the GPU
    for (size_t i = 0; i < channels.size(); ++i)
        image.channels[size_t(channels[i])].upload_tile(Box2i{offset, offset + extent}, staging[i].data());
}

} // namespace

bool can_edit(const ConstImagePtr &img) { return img && !img->is_live; }

std::vector<int> target_groups(const ConstImagePtr &img, int pointed_at)
{
    if (!img)
        return {};

    // a group pointed at from the Images panel names itself alone, unless it is one of the selected
    // ones, in which case the right-click covers the selection, the same way a click inside one does
    if (pointed_at >= 0 && !img->is_group_selected(pointed_at))
        return {pointed_at};

    // the fallback is for an image the panel has never had a say over: one a command just produced, or
    // a test driving a command directly
    std::vector<int> groups = img->selected_groups();
    if (groups.empty() && img->is_valid_group(img->active_group_index(Target_Primary)))
        groups.push_back(img->active_group_index(Target_Primary));

    return groups;
}

std::pair<std::vector<int>, Box2i> resolve_subject(const ConstImagePtr &img, const EditSubject &subject,
                                                   const Box2i &roi)
{
    if (!img)
        return {std::vector<int>{}, Box2i{}};

    std::vector<int> channels = subject_channels(*img, subject);

    Box2i bounds = img->data_window;
    // an empty selection means "no selection", not "select nothing"
    if (subject.selection_only && roi.has_volume())
        bounds.intersect(roi);

    return {channels, bounds};
}

bool modify_image(const EditContext &ctx, const string &name, const function<void(Image &)> &op,
                  const UndoFactory &make_undo, EditExtent extent)
{
    const ImagePtr &img = ctx.image;
    if (!can_edit(img))
    {
        spdlog::warn("Cannot edit '{}': its pixels come from a running process.", img ? img->filename : "");
        return false;
    }

    spdlog::debug("Editing '{}': {}", img->filename, name);

    // a statistics task reads these pixels from a worker thread, so it has to be off them before the
    // write, and before the undo entry reads them
    for (auto &c : img->channels) c.cancel_stats();

    // built first: an entry that stores pixels has to see them as they were
    auto entry = make_undo(*img, name);

    op(*img);

    if (entry)
        img->history.add(std::move(entry));

    // the channel list changed wholesale, so the layers and groups built from its names are rebuilt
    if (extent == Extent_Structure)
        img->rebuild_layers();

    ++img->content_version; // invalidates the cached statistics and histograms

    if (ctx.edited)
        ctx.edited(extent);

    return true;
}

bool modify_pixels(const EditContext &ctx, const string &name, const function<float(float, int2, int)> &op)
{
    // Named apart rather than as a structured binding, which C++17 cannot capture in a lambda.
    auto  resolved = resolve_subject(ctx.image, ctx.subject, ctx.roi);
    auto &channels = resolved.first;
    auto &bounds   = resolved.second;
    if (channels.empty() || !bounds.has_volume())
        return false;

    return modify_image(
        ctx, name,
        [&channels, &bounds, &op](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();

            for (size_t slot = 0; slot < channels.size(); ++slot)
            {
                const Channel &channel = image.channels[size_t(channels[slot])];

                write_rect(image, {channels[slot]}, bounds,
                           [&](int y0, int y1, std::vector<Array2Df> &staging)
                           {
                               for (int y = y0; y < y1; ++y)
                                   for (int x = 0; x < extent.x; ++x)
                                       staging[0](x, y) = op(channel(offset.x + x, offset.y + y),
                                                             int2{bounds.min.x + x, bounds.min.y + y}, int(slot));
                           });
            }
        },
        [&channels, &bounds](const Image &image, const string &n) -> UndoPtr
        { return std::make_unique<ChannelRectUndo>(image, channels, bounds, n); });
}

bool modify_colors(const EditContext &ctx, const string &name, const function<float4(const float4 &, int2, int)> &op,
                   const function<void(Image &)> &retag)
{
    if (!ctx.image)
        return false;

    // Named apart rather than as a structured binding, which C++17 cannot capture in a lambda.
    auto  color    = subject_color_groups(*ctx.image, ctx.subject);
    auto &groups   = color.first;
    auto &channels = color.second;
    if (groups.empty())
    {
        // e.g. an ungrouped image or a depth pass: nothing here is color
        spdlog::warn("'{}' covers no color channel group of '{}'.", name, ctx.image->file_and_partname());
        return false;
    }

    Box2i bounds = ctx.image->data_window;
    if (ctx.subject.selection_only && ctx.roi.has_volume())
        bounds.intersect(ctx.roi);
    if (!bounds.has_volume())
        return false;

    return modify_image(
        ctx, name,
        [&groups, &bounds, &op, &retag](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();

            for (int g : groups)
            {
                const int        n = image.groups[size_t(g)].num_channels;
                std::vector<int> group_channels;
                for (int k = 0; k < n; ++k) group_channels.push_back(image.groups[size_t(g)].channels[k]);

                write_rect(image, group_channels, bounds,
                           [&](int y0, int y1, std::vector<Array2Df> &staging)
                           {
                               for (int y = y0; y < y1; ++y)
                                   for (int x = 0; x < extent.x; ++x)
                                   {
                                       // opaque where the group has no alpha, so an op may read the fourth
                                       // component whatever kind of group it was handed
                                       float4 c{0.f, 0.f, 0.f, 1.f};
                                       for (int k = 0; k < n; ++k)
                                           c[k] = image.channels[size_t(group_channels[size_t(k)])](offset.x + x,
                                                                                                    offset.y + y);

                                       const float4 out = op(c, int2{bounds.min.x + x, bounds.min.y + y}, g);

                                       for (int k = 0; k < n; ++k) staging[size_t(k)](x, y) = out[k];
                                   }
                           });
            }

            if (retag)
                retag(image);
        },
        [&channels, &bounds, &retag](const Image &image, const string &n) -> UndoPtr
        {
            // the pixels and what they mean changed together, so they are taken back together
            if (retag)
                return std::make_unique<ColorMetadataUndo>(image, channels, bounds, n);
            return std::make_unique<ChannelRectUndo>(image, channels, bounds, n);
        });
}
