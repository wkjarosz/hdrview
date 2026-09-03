//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file channels.cpp
    \author Wojciech Jarosz

    The edits that change which channels an image has, and how they are grouped. None carries a subject:
    the channels they cover come from HDRViewApp::target_groups().
*/

#include "edit/commands.h"

#include "edit/edit_ops.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <algorithm>
#include <vector>

namespace
{

/// Every channel of \p img in the layer \p layer that ungrouping marked.
/**
    Marked, not merely standing alone: a channel whose name never grouped has no flag to clear, and undo
    would then set one.
*/
std::vector<int> ungrouped_in_layer(const ImagePtr &img, const std::string &layer)
{
    std::vector<int> out;
    if (!img)
        return out;

    for (size_t c = 0; c < img->channels.size(); ++c)
        if (img->channels[c].ungrouped && Channel::head(img->channels[c].name) == layer)
            out.push_back(int(c));

    return out;
}

/// The channels of \p group, or empty when that is not a group of \p img.
std::vector<int> group_channels(const ImagePtr &img, int group)
{
    std::vector<int> out;
    if (!img || !img->is_valid_group(group))
        return out;

    const auto &g = img->groups[size_t(group)];
    for (int i = 0; i < g.num_channels; ++i) out.push_back(g.channels[i]);
    return out;
}

/// Every channel of every group an edit is about to act on.
std::vector<int> target_channels(const EditContext &ctx)
{
    std::vector<int> out;
    for (int g : ctx.target_groups)
        for (int c : group_channels(ctx.image, g)) out.push_back(c);
    return out;
}

/// The channel to leave the viewport on after a rebuild, or -1 to leave it where it is.
/**
    Only a group being changed is followed; see select_channels_group().
*/
int followed_channel(const ImagePtr &img, const std::vector<int> &changed)
{
    const std::vector<int> showing = group_channels(img, img->selected_group);
    for (int c : changed)
        if (std::find(showing.begin(), showing.end(), c) != showing.end())
            return c;
    return -1;
}

/// The layer \p group belongs to, which is the prefix its channel names share.
std::string layer_of(const ImagePtr &img, int group)
{
    const std::vector<int> channels = group_channels(img, group);
    return channels.empty() ? std::string{} : Channel::head(img->channels[size_t(channels.front())].name);
}

/// The channels regrouping would put back together: every marked channel of every group invoked on.
/**
    One group alone cannot say which channels to rejoin, ungrouping having left each standing alone, so it
    restores its whole layer instead.
*/
std::vector<int> regroup_channels(const EditContext &ctx)
{
    auto img = ctx.image;
    if (!img)
        return {};

    const std::vector<int> groups = ctx.target_groups;
    if (groups.size() == 1)
        return ungrouped_in_layer(img, layer_of(img, groups.front()));

    std::vector<int> out;
    for (int g : groups)
        for (int c : group_channels(img, g))
            if (img->channels[size_t(c)].ungrouped)
                out.push_back(c);

    return out;
}

/// Leave the viewport on whichever group now holds \p channel.
/**
    A rebuild renumbers the groups, so the index that was selected can afterwards mean something else.
*/
void select_channels_group(Image &img, int channel)
{
    for (size_t g = 0; g < img.groups.size(); ++g)
        for (int i = 0; i < img.groups[g].num_channels; ++i)
            if (img.groups[g].channels[i] == channel)
            {
                img.selected_group = int(g);
                return;
            }
}

/// Mark \p channels as standing alone or not and rebuild the groups derived from their names.
/**
    Leaves the viewport on whichever group now holds \p follow. Ungrouping and regrouping are this in both
    directions.
*/
std::function<void(Image &)> mark_ungrouped(std::vector<int> channels, int follow, bool ungrouped)
{
    return [channels = std::move(channels), follow, ungrouped](Image &img)
    {
        for (int c : channels) img.channels[size_t(c)].ungrouped = ungrouped;
        img.rebuild_layers();
        if (follow >= 0)
            select_channels_group(img, follow);
    };
}

/// Show a group's channels one at a time rather than as a color.
/**
    Groups are derived from channel names, so this marks the channels and the rebuild that follows declines
    to put them back together.
*/
class UngroupChannels final : public EditCommand
{
public:
    UngroupChannels() :
        EditCommand({{"Ungroup channels", "Explode channel group", "Split channel group"},
                     ICON_MY_NO_CHANNEL_GROUP,
                     ImGuiKey_None,
                     /* has_dialog */ false,
                     "Ungroup",
                     24.f,
                     /* draws_subject_selector */ false})
    {
    }

    /// Only worth offering while one of the target groups holds more than one channel.
    bool enabled(const EditContext &ctx) const override { return !ungroupable_channels(ctx).empty(); }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        const std::vector<int> channels = ungroupable_channels(ctx);
        if (channels.empty())
            return;

        const int follow = followed_channel(img, channels);

        auto forward  = mark_ungrouped(channels, follow, true);
        auto backward = mark_ungrouped(channels, follow, false);
        modify_image(ctx, "Ungroup channels", forward, reversible(forward, backward));
    }

private:
    /// The channels of the target groups that hold more than one.
    /**
        A group already standing alone is skipped, since marking it would change nothing but would still
        record an entry.
    */
    static std::vector<int> ungroupable_channels(const EditContext &ctx)
    {
        std::vector<int> out;
        for (int g : ctx.target_groups)
        {
            const std::vector<int> channels = group_channels(ctx.image, g);
            if (channels.size() > 1)
                out.insert(out.end(), channels.begin(), channels.end());
        }
        return out;
    }
};

/**
    Put ungrouped channels back into the groups their names ask for; the counterpart to ungrouping.

    The groups are still derived from the names, so this only decides which channels are available to
    match: with A held out of the selection, R,G,B,A comes up short and R,G,B matches instead. A single
    selected group falls back to its whole layer; see regroup_channels().
*/
class RegroupChannels final : public EditCommand
{
public:
    RegroupChannels() :
        EditCommand({{"Regroup channels", "Rejoin exploded channels"},
                     ICON_MY_CHANNEL_GROUP,
                     ImGuiKey_None,
                     /* has_dialog */ false,
                     "Regroup",
                     24.f,
                     /* draws_subject_selector */ false})
    {
    }

    bool enabled(const EditContext &ctx) const override { return !regroup_channels(ctx).empty(); }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        const std::vector<int> marked = regroup_channels(ctx);
        if (marked.empty())
            return;

        const int follow = followed_channel(img, marked);

        auto forward  = mark_ungrouped(marked, follow, false);
        auto backward = mark_ungrouped(marked, follow, true);
        modify_image(ctx, "Regroup channels", forward, reversible(forward, backward));
    }
};

/// Remove a group's channels from the image outright, so saving writes the smaller image.
/**
    Undo brings them back, the whole channel list being what a structural entry records.
*/
class DeleteChannelGroup final : public EditCommand
{
public:
    DeleteChannelGroup() :
        EditCommand({{"Delete channel group", "Remove channels"},
                     ICON_MY_TRASH_CAN,
                     ImGuiKey_None,
                     /* has_dialog */ false,
                     "Delete",
                     24.f,
                     /* draws_subject_selector */ false})
    {
    }

    /// Never every group: an image with no channels is not an image.
    bool enabled(const EditContext &ctx) const override
    {
        auto img = ctx.image;
        if (!img || img->groups.size() < 2)
            return false;

        const std::vector<int> channels = target_channels(ctx);
        return !channels.empty() && channels.size() < img->channels.size();
    }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        std::vector<int> channels = target_channels(ctx);
        if (channels.empty() || channels.size() >= img->channels.size())
            return;

        // erase from the back, so the indices still to be erased are not shifted
        std::sort(channels.begin(), channels.end(), std::greater<int>());

        modify_image(
            ctx, "Delete channel group",
            [channels](Image &image)
            {
                for (int c : channels)
                    if (c >= 0 && c < int(image.channels.size()))
                        image.channels.erase(image.channels.begin() + c);
            },
            structure_undo, Extent_Structure);
    }
};

} // namespace

std::string delete_channels_label(const ImagePtr &img, const std::vector<int> &groups)
{
    // only the menu label varies; the action's own name is the key everything addresses it by
    if (groups.size() > 1)
        return "Delete channel groups";

    return groups.size() == 1 && group_channels(img, groups.front()).size() == 1 ? "Delete channel"
                                                                                 : "Delete channel group";
}

void add_channel_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<UngroupChannels>());
    out.push_back(std::make_unique<RegroupChannels>());
    out.push_back(std::make_unique<DeleteChannelGroup>());
}
