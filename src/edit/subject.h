//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include <utility>
#include <vector>

/// Which channels and which rectangle an edit applies to (Edit > Apply to).
struct EditSubject
{
    enum Scope : int
    {
        Scope_CurrentGroup = 0, ///< Only the channels the viewport is currently showing
        Scope_SelectedGroups,   ///< Every group selected in the Images panel, which always includes the current one
        Scope_AllChannels,      ///< Every channel in the image, whatever layer it belongs to

        Scope_COUNT
    };

    Scope scope = Scope_CurrentGroup;

    /// Restrict the edit to the rectangular selection.
    /**
        Ignored while the selection is empty, so an edit then covers the whole image.
    */
    bool selection_only = true;
};

inline const char *edit_scope_name(int scope)
{
    switch (scope)
    {
    case EditSubject::Scope_CurrentGroup: return "Current channel group";
    case EditSubject::Scope_SelectedGroups: return "Selected channel groups";
    case EditSubject::Scope_AllChannels: return "All channels";
    default: return "";
    }
}

/// The channels of \p img that \p subject's scope names, in channel order.
std::vector<int> subject_channels(const Image &img, const EditSubject &subject);

/// The color groups of \p img that \p subject's scope names, and every channel of them.
/**
    Only RGB and RGBA groups: depth, motion vectors and the like are left alone.
*/
std::pair<std::vector<int>, std::vector<int>> subject_color_groups(const Image &img, const EditSubject &subject);
