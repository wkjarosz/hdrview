//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file channels.cpp
    \author Wojciech Jarosz

    The edits that change which channels an image has, and how they are grouped.

    None of them carries a subject: the channels they cover are named by the multi-selection, or by a
    single group right-clicked from outside it, rather than by a scope over one image's channels. They do
    still fan out, since a selection can hold groups of more than one image.
*/

#include "edit/commands.h"

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <algorithm>
#include <vector>

namespace
{

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

//! Every channel of every group an edit is about to act on, which need not include the one on screen.
std::vector<int> target_channels(const EditContext &ctx)
{
    std::vector<int> out;
    for (int g : ctx.target_groups())
        for (int c : group_channels(ctx.image(), g)) out.push_back(c);
    return out;
}

//! The channel to leave the viewport on after a rebuild, or -1 to leave it where it is.
/*!
    Only a group being changed is worth following: one merely pointed at from the panel must leave the
    viewport where it was. See select_channels_group() for why following it is what it takes.
*/
int followed_channel(const ImagePtr &img, const std::vector<int> &changed)
{
    const std::vector<int> showing = group_channels(img, img->selected_group);
    for (int c : changed)
        if (std::find(showing.begin(), showing.end(), c) != showing.end())
            return c;
    return -1;
}

//! The layer \p group belongs to, which is the prefix its channel names share.
std::string layer_of(const ImagePtr &img, int group)
{
    const std::vector<int> channels = group_channels(img, group);
    return channels.empty() ? std::string{} : Channel::head(img->channels[size_t(channels.front())].name);
}

//! The channels regrouping would put back together.
/*!
    Every marked channel of every group the command was invoked on -- except that a single group cannot
    say which channels it should rejoin, since ungrouping left each of them standing alone. One group
    therefore restores its whole layer, which is all its channels still have in common, and that is also
    what the Images panel's context menu asks for when it names one group. Two or more say exactly which.
*/
std::vector<int> regroup_channels(const EditContext &ctx)
{
    auto img = ctx.image();
    if (!img)
        return {};

    const std::vector<int> groups = ctx.target_groups();
    if (groups.size() == 1)
        return ungrouped_in_layer(img, layer_of(img, groups.front()));

    std::vector<int> out;
    for (int g : groups)
        for (int c : group_channels(img, g))
            if (img->channels[size_t(c)].ungrouped)
                out.push_back(c);

    return out;
}

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
                /* draws_subject_selector */ false};
    }

    //! Only worth offering while one of the groups it would take apart is more than one channel.
    bool enabled(const EditContext &ctx) const override { return !ungroupable_channels(ctx).empty(); }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        const std::vector<int> channels = ungroupable_channels(ctx);
        if (channels.empty())
            return;

        const int follow = followed_channel(img, channels);

        ctx.modify_reversibly(
            "Ungroup channels",
            [channels, follow](Image &img2)
            {
                for (int c : channels) img2.channels[size_t(c)].ungrouped = true;
                img2.rebuild_layers();
                if (follow >= 0)
                    select_channels_group(img2, follow);
            },
            [channels, follow](Image &img2)
            {
                for (int c : channels) img2.channels[size_t(c)].ungrouped = false;
                img2.rebuild_layers();
                if (follow >= 0)
                    select_channels_group(img2, follow);
            });
    }

private:
    //! The channels of the target groups that hold more than one, which are all this can take apart.
    /*!
        A group already standing alone is skipped rather than marked: marking it would change nothing but
        would still record an entry, and undoing that entry would clear a flag the channel never had.
    */
    static std::vector<int> ungroupable_channels(const EditContext &ctx)
    {
        std::vector<int> out;
        for (int g : ctx.target_groups())
        {
            const std::vector<int> channels = group_channels(ctx.image(), g);
            if (channels.size() > 1)
                out.insert(out.end(), channels.begin(), channels.end());
        }
        return out;
    }
};

/*!
    Put ungrouped channels back into the groups their names ask for.

    The counterpart to ungrouping, and scoped by the selection: selecting the channels that should end up
    together and asking for them back is how an RGBA group becomes an RGB group beside a lone alpha. The
    groups are still derived from the names, so this only decides which channels are available to match --
    with A held out, the R,G,B,A pattern comes up short and R,G,B matches instead.

    A single selected group falls back to its whole layer; see regroup_channels().
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
                /* draws_subject_selector */ false};
    }

    bool enabled(const EditContext &ctx) const override { return !regroup_channels(ctx).empty(); }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        const std::vector<int> marked = regroup_channels(ctx);
        if (marked.empty())
            return;

        const int follow = followed_channel(img, marked);

        ctx.modify_reversibly(
            "Regroup channels",
            [marked, follow](Image &img2)
            {
                for (int c : marked) img2.channels[size_t(c)].ungrouped = false;
                img2.rebuild_layers();
                if (follow >= 0)
                    select_channels_group(img2, follow);
            },
            [marked, follow](Image &img2)
            {
                for (int c : marked) img2.channels[size_t(c)].ungrouped = true;
                img2.rebuild_layers();
                if (follow >= 0)
                    select_channels_group(img2, follow);
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
                /* draws_subject_selector */ false};
    }

    //! Never every group: an image with no channels is not an image.
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

std::string delete_channels_label(const ImagePtr &img, const std::vector<int> &groups)
{
    // The action's own name never changes -- it is the key the registry, the palette and the tests address
    // it by -- so only what the menu shows is chosen here.
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
