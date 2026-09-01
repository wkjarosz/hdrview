//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file channels.cpp
    \author Wojciech Jarosz

    The edits that change which channels an image has, and how they are grouped.
*/

#include "edit/commands.h"

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <algorithm>
#include <vector>

namespace
{

//! The channels of the group the viewport is showing, or empty when there is no group to speak of.
std::vector<int> current_group_channels(const ImagePtr &img)
{
    std::vector<int> out;
    if (!img)
        return out;

    const int g = img->active_group_index(Target_Primary);
    if (!img->is_valid_group(g))
        return out;

    const auto &group = img->groups[size_t(g)];
    for (int i = 0; i < group.num_channels; ++i) out.push_back(group.channels[i]);
    return out;
}

/*!
    Show a group's channels one at a time rather than as a color.

    Groups are derived from channel names, so this cannot remove a group -- it marks the channels, and the
    rebuild that follows declines to put them back together. The inverse is Regroup channels.
*/
class ExplodeChannelGroup final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Explode channel group", "Ungroup channels", "Split channel group"},
                ICON_MY_NO_CHANNEL_GROUP,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Explode",
                24.f,
                /* has_subject */ false};
    }

    //! Only worth offering for a group that is more than one channel already.
    bool enabled(const EditContext &ctx) const override { return current_group_channels(ctx.image()).size() > 1; }

    void apply(EditContext &ctx) override
    {
        const std::vector<int> channels = current_group_channels(ctx.image());
        if (channels.size() < 2)
            return;

        ctx.modify_reversibly(
            "Explode channel group",
            [channels](Image &img)
            {
                for (int c : channels) img.channels[size_t(c)].ungrouped = true;
                img.rebuild_layers();
            },
            [channels](Image &img)
            {
                for (int c : channels) img.channels[size_t(c)].ungrouped = false;
                img.rebuild_layers();
            });
    }
};

//! Put every channel back in the group its name asks for, undoing any number of explosions at once.
class RegroupChannels final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Regroup channels", "Undo exploded channel groups"},
                ICON_MY_CHANNEL_GROUP,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Regroup",
                24.f,
                /* has_subject */ false};
    }

    bool enabled(const EditContext &ctx) const override
    {
        auto img = ctx.image();
        if (!img)
            return false;

        return std::any_of(img->channels.begin(), img->channels.end(), [](const Channel &c) { return c.ungrouped; });
    }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        // Only the ones actually marked, so undoing this puts back exactly what it cleared.
        std::vector<int> marked;
        for (size_t c = 0; c < img->channels.size(); ++c)
            if (img->channels[c].ungrouped)
                marked.push_back(int(c));

        if (marked.empty())
            return;

        ctx.modify_reversibly(
            "Regroup channels",
            [marked](Image &img2)
            {
                for (int c : marked) img2.channels[size_t(c)].ungrouped = false;
                img2.rebuild_layers();
            },
            [marked](Image &img2)
            {
                for (int c : marked) img2.channels[size_t(c)].ungrouped = true;
                img2.rebuild_layers();
            });
    }
};

/*!
    Remove a group's channels from the image outright.

    Unlike exploding, this is a real edit: the channels are gone, and saving writes the smaller image.
    Undo brings them back, since the whole channel list is what a structural entry records.
*/
class DeleteChannelGroup final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Delete channel group", "Remove channels"},
                ICON_MY_TRASH_CAN,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Delete",
                24.f,
                /* has_subject */ false};
    }

    //! Never the last group: an image with no channels is not an image.
    bool enabled(const EditContext &ctx) const override
    {
        auto img = ctx.image();
        if (!img || img->groups.size() < 2)
            return false;

        const std::vector<int> channels = current_group_channels(img);
        return !channels.empty() && channels.size() < img->channels.size();
    }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        std::vector<int> channels = current_group_channels(img);
        if (channels.empty() || channels.size() >= img->channels.size())
            return;

        // Erased from the back, so the indices still to be erased are not shifted out from under it.
        std::sort(channels.begin(), channels.end(), std::greater<int>());

        ctx.modify_structure("Delete channel group",
                             [channels](Image &image)
                             {
                                 for (int c : channels)
                                     if (c >= 0 && c < int(image.channels.size()))
                                         image.channels.erase(image.channels.begin() + c);

                                 // The groups and the layer tree are built from the channel names, so they
                                 // have to be derived again rather than patched.
                                 image.rebuild_layers();
                             });
    }
};

} // namespace

void add_channel_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<ExplodeChannelGroup>());
    out.push_back(std::make_unique<RegroupChannels>());
    out.push_back(std::make_unique<DeleteChannelGroup>());
}
