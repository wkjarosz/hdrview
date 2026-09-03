//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "box.h"
#include "edit/command.h"
#include "edit/subject.h"
#include "edit/undo.h"
#include "fwd.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

/// Whether \p img accepts edits: false while a renderer is streaming into it, whose next tile would overwrite them.
bool can_edit(const ConstImagePtr &img);

/// The groups of \p img an edit acts on: the selected ones, or \p pointed_at alone when it is outside them.
/**
    An image the Images panel has never had a say over falls back to the group on screen.
*/
std::vector<int> target_groups(const ConstImagePtr &img, int pointed_at = -1);

/// The channels \p subject names, and the rectangle of them it covers, in image coordinates.
/**
    The rectangle is the data window, narrowed to \p roi when the subject asks for it. Empty means there is
    nothing to edit.
*/
std::pair<std::vector<int>, Box2i> resolve_subject(const ConstImagePtr &img, const EditSubject &subject,
                                                   const Box2i &roi);

/** \name Editing an image
    Each lands as one undoable edit: it cancels the statistics tasks reading the pixels, builds the undo
    entry from the image as it was, applies the change, pushes the entry onto the image's history and
    invalidates the caches keyed on the pixels. All return whether anything was edited -- false for an image
    that refuses edits, or a subject that names nothing. The async pair on EditContext ends up here too,
    once its worker is done.
*/
///@{

/// Apply \p op to the context's image and record how to reverse it; the only thing that writes pixels.
/**
    \param ctx       What is being edited
    \param name      Shown beside "Undo"/"Redo", e.g. "Rotate 90 CW"
    \param op        Makes the change
    \param make_undo Builds the entry that reverses it, called before \p op so it can capture what \p op
                     is about to overwrite
    \param extent    Whether \p op reshapes the image, so the layer tree is rebuilt and the view refit
*/
bool modify_image(const EditContext &ctx, const std::string &name, const std::function<void(Image &)> &op,
                  const UndoFactory &make_undo, EditExtent extent = Extent_Pixels);

/// Apply \p op to every pixel the subject covers, one at a time.
/**
    \p op is handed a value, its position in image coordinates, and which of the subject's channels it
    belongs to -- 0 for the first, so a group's R, G, B, A arrive as 0, 1, 2, 3 -- and returns what to
    replace it with. Both the GPU upload and the undo entry cover only the subject's rectangle.
*/
bool modify_pixels(const EditContext &ctx, const std::string &name, const std::function<float(float, int2, int)> &op);

/// Apply \p op to each covered group's channels together, so it can mix them into each other.
/**
    \p op is handed the group's values, the position in image coordinates, and the group's index into
    Image::groups, which is how it reads elsewhere in the group. Only color groups are covered, RGB and
    RGBA; a group without alpha gets 1 in that slot and whatever \p op returns there is dropped.

    \p retag, if given, retags the image's color metadata in the same history entry, so undoing takes back
    both what \p op wrote and what it means.
*/
bool modify_colors(const EditContext &ctx, const std::string &name,
                   const std::function<float4(const float4 &, int2, int)> &op,
                   const std::function<void(Image &)>                     &retag = {});

///@}
