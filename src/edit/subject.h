//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

/*!
    Which of an image's samples an edit applies to.

    Pre-2.0 never had to ask: an image was one interleaved RGBA plane, so an edit applied to all of it.
    An image is now a set of named channels grouped into layers, and "invert" over a thirty-layer EXR is
    not one operation -- so every edit carries this, and says so where the user can see it.

    Deliberately only two scopes. "Every visible group" would sound like a third, but group visibility is
    recomputed from the Images panel's channel-filter text on every keystroke, which would let typing in a
    search box quietly redefine what a destructive edit covers.
*/
struct EditSubject
{
    enum Scope : int
    {
        Scope_CurrentGroup = 0, //!< Only the channels the viewport is currently showing
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
    case EditSubject::Scope_AllChannels: return "All channels";
    default: return "";
    }
}
