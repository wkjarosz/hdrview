//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/subject.h"

#include "image.h"

#include <algorithm>
#include <numeric>

namespace
{

/// The groups \p subject's scope names, before any filtering by what they contain.
std::vector<int> subject_groups(const Image &img, const EditSubject &subject)
{
    std::vector<int> groups;
    if (subject.scope == EditSubject::Scope_AllChannels)
    {
        for (int g = 0; g < int(img.groups.size()); ++g) groups.push_back(g);
    }
    else if (subject.scope == EditSubject::Scope_SelectedGroups)
        groups = img.selected_groups();
    else if (int g = img.active_group_index(Target_Primary); img.is_valid_group(g))
        groups.push_back(g);

    return groups;
}

} // namespace

std::vector<int> subject_channels(const Image &img, const EditSubject &subject)
{
    // every channel, not the union of every group's, so one belonging to no group is still covered
    if (subject.scope == EditSubject::Scope_AllChannels)
    {
        std::vector<int> channels(img.channels.size());
        std::iota(channels.begin(), channels.end(), 0);
        return channels;
    }

    std::vector<int> channels;
    for (int g : subject_groups(img, subject))
    {
        const auto &group = img.groups[size_t(g)];
        for (int c = 0; c < group.num_channels; ++c) channels.push_back(group.channels[c]);
    }
    return channels;
}

std::pair<std::vector<int>, std::vector<int>> subject_color_groups(const Image &img, const EditSubject &subject)
{
    std::vector<int> groups = subject_groups(img, subject);

    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [&img](int g)
                                {
                                    const auto t = img.groups[size_t(g)].type;
                                    return t != ChannelGroup::RGB_Channels && t != ChannelGroup::RGBA_Channels;
                                }),
                 groups.end());

    // every channel of every covered group, which is the set an undo entry has to hold
    std::vector<int> channels;
    for (int g : groups)
    {
        const auto &group = img.groups[size_t(g)];
        for (int c = 0; c < group.num_channels; ++c) channels.push_back(group.channels[c]);
    }

    return {groups, channels};
}
