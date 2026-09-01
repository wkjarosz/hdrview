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

//! The layer the group on screen belongs to, which is the prefix its channel names share.
/*!
    What scopes regrouping. A group can be selected but a set of them cannot, so an operation that puts
    channels back together has to work out for itself which ones are meant -- and once a group has been
    ungrouped, the only thing its channels still have in common is the layer they came from.
*/
std::string target_layer(const EditContext &ctx);

//! Every channel of \p img in the layer \p layer that ungrouping marked.
/*!
    Marked, rather than merely standing alone: a layer can hold channels whose names never grouped -- a
    depth channel beside a color -- and those are not what regrouping is about. Clearing a flag they never
    had would do nothing, but the entry that reverses it would then set one they never had either.
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

//! The channels of \p group, or empty when that is not a group of \p img.
std::vector<int> group_channels(const ImagePtr &img, int group)
{
    std::vector<int> out;
    if (!img || !img->is_valid_group(group))
        return out;

    const auto &g = img->groups[size_t(group)];
    for (int i = 0; i < g.num_channels; ++i) out.push_back(g.channels[i]);
    return out;
}

//! The channels of the group an edit is about to act on, which is not always the one on screen.
std::vector<int> target_channels(const EditContext &ctx) { return group_channels(ctx.image(), ctx.target_group()); }

//! Leave the viewport on whichever group now holds \p channel.
/*!
    A rebuild renumbers the groups, so the index that was selected can afterwards mean something else
    entirely -- and it is the selection that says what the next command will act on. Following the channel
    across the rebuild is what lets exploding and regrouping be used one after the other.
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

std::string target_layer(const EditContext &ctx)
{
    const std::vector<int> channels = target_channels(ctx);
    return channels.empty() ? std::string{} : Channel::head(ctx.image()->channels[size_t(channels.front())].name);
}

/*!
    Show a group's channels one at a time rather than as a color.

    Groups are derived from channel names, so this cannot remove a group -- it marks the channels, and the
    rebuild that follows declines to put them back together. The inverse is Regroup channels.
*/
class UngroupChannels final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Ungroup channels", "Explode channel group", "Split channel group"},
                ICON_MY_NO_CHANNEL_GROUP,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Ungroup",
                24.f,
                /* has_subject */ false};
    }

    //! Only worth offering for a group that is more than one channel already.
    bool enabled(const EditContext &ctx) const override { return target_channels(ctx).size() > 1; }

    void apply(EditContext &ctx) override
    {
        const std::vector<int> channels = target_channels(ctx);
        if (channels.size() < 2)
            return;

        // Only follow the channels across the rebuild when it was the group on screen that was ungrouped;
        // one pointed at from the panel must leave the viewport where it was.
        const bool follow = ctx.target_group() == ctx.image()->selected_group;

        ctx.modify_reversibly(
            "Ungroup channels",
            [channels, follow](Image &img)
            {
                for (int c : channels) img.channels[size_t(c)].ungrouped = true;
                img.rebuild_layers();
                if (follow)
                    select_channels_group(img, channels.front());
            },
            [channels, follow](Image &img)
            {
                for (int c : channels) img.channels[size_t(c)].ungrouped = false;
                img.rebuild_layers();
                if (follow)
                    select_channels_group(img, channels.front());
            });
    }
};

/*!
    Put the channels of a layer back into the groups their names ask for.

    The counterpart to ungrouping, and scoped to a layer because that is all there is to go on.
    Ungrouping leaves a group's channels standing on their own, and only one group can be selected at a
    time -- so selecting any one of them and asking for its layer back is the way to say which of them is
    meant.

    A layer holding two ungrouped groups is restored in one go, since nothing distinguishes them once they
    are apart.
*/
class RegroupChannels final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Regroup channels", "Rejoin exploded channels"},
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
        return img && !ungrouped_in_layer(img, target_layer(ctx)).empty();
    }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        const std::vector<int> marked = ungrouped_in_layer(img, target_layer(ctx));
        if (marked.empty())
            return;

        const bool follow = ctx.target_group() == img->selected_group;

        ctx.modify_reversibly(
            "Regroup channels",
            [marked, follow](Image &img2)
            {
                for (int c : marked) img2.channels[size_t(c)].ungrouped = false;
                img2.rebuild_layers();
                if (follow)
                    select_channels_group(img2, marked.front());
            },
            [marked, follow](Image &img2)
            {
                for (int c : marked) img2.channels[size_t(c)].ungrouped = true;
                img2.rebuild_layers();
                if (follow)
                    select_channels_group(img2, marked.front());
            });
    }
};

/*!
    Remove a group's channels from the image outright.

    Unlike ungrouping, this is a real edit: the channels are gone, and saving writes the smaller image.
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

        const std::vector<int> channels = target_channels(ctx);
        return !channels.empty() && channels.size() < img->channels.size();
    }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        std::vector<int> channels = target_channels(ctx);
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

std::string delete_channels_label(const ImagePtr &img, int group)
{
    // The action's own name never changes -- it is the key the registry, the palette and the tests address
    // it by -- so only what the menu shows is chosen here.
    return group_channels(img, group).size() == 1 ? "Delete channel" : "Delete channel group";
}

void add_channel_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<UngroupChannels>());
    out.push_back(std::make_unique<RegroupChannels>());
    out.push_back(std::make_unique<DeleteChannelGroup>());
}
