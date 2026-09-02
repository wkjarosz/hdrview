//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include <utility>
#include <vector>

/*!
    Which of an image's samples an edit applies to.

    Pre-2.0 never had to ask: an image was one interleaved RGBA plane, so an edit applied to all of it.
    An image is now a set of named channels grouped into layers, and "invert" over a thirty-layer EXR is
    not one operation -- so every edit carries this, and says so where the user can see it.

    Three scopes and deliberately no fourth: "every visible group" would sound like one, but group
    visibility is recomputed from the Images panel's channel-filter text on every keystroke, which would
    let typing in a search box quietly redefine what a destructive edit covers.
*/
struct EditSubject
{
    enum Scope : int
    {
        Scope_CurrentGroup = 0, //!< Only the channels the viewport is currently showing
        Scope_SelectedGroups,   //!< Every group selected in the Images panel, which always includes the current one
        Scope_AllChannels,      //!< Every channel in the image, whatever layer it belongs to

        Scope_COUNT
    };

    Scope scope = Scope_CurrentGroup;

    //! Restrict the edit to the rectangular selection, when there is one.
    /*!
        On by default: having just drawn a selection, an edit that ignored it would be a surprise, and the
        way back is one undo either way.

        Ignored while the selection is empty rather than meaning "edit nothing", so this staying on does
        not make edits appear to stop working once the selection is cleared -- with no selection every edit
        covers the whole image.
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

/*!
    What a scope covers, resolved against an image.

    Free functions rather than members of HDRViewApp, because this depends on the subject and the image
    alone -- which is what lets an edit's coverage be checked without constructing a window; see
    tests/test_edit_commands.cpp.
*/
//! The channels of \p img that \p subject's scope names, in channel order.
std::vector<int> subject_channels(const Image &img, const EditSubject &subject);

//! The color groups of \p img that \p subject's scope names, and every channel of them.
/*!
    Only RGB and RGBA: everything else in an image -- depth, motion vectors, an ID -- is not color, and a
    color operation has no meaning for it, so it is left alone rather than run through one.
*/
std::pair<std::vector<int>, std::vector<int>> subject_color_groups(const Image &img, const EditSubject &subject);
