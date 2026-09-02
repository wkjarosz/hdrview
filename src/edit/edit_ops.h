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

/// Whether \p img accepts edits at all.
/**
    False while a renderer is streaming into it, since the next tile would overwrite the edit.
*/
bool can_edit(const ConstImagePtr &img);

/// The groups of \p img an edit acts on: normally the selected ones.
/**
    \p pointed_at, when it names a group outside the selection, stands for itself. Falls back to the group
    on screen for an image the Images panel has never had a say over.
*/
std::vector<int> target_groups(const ConstImagePtr &img, int pointed_at = -1);

/// The channels \p subject names, and the rectangle of them it covers, in image coordinates.
/**
    The rectangle is the data window, narrowed to \p roi when the subject asks for it. Empty means there is
    nothing to edit.
*/
std::pair<std::vector<int>, Box2i> resolve_subject(const ConstImagePtr &img, const EditSubject &subject,
                                                   const Box2i &roi);

//
// The ways an image can be changed, each landing as one undoable edit. Every one of them cancels the
// statistics tasks reading the samples, builds the undo entry from the image as it was, applies the
// change, pushes the entry onto the image's history and invalidates the caches keyed on its samples.
// All return whether anything was edited; false for an image that refuses edits or a subject that names
// nothing. The async pair on EditContext ends up here too, once its worker is done.
//

/**
    Apply \p op to the context's image and record how to reverse it. The only thing that writes image
    pixels; the two below go through it.

    \param ctx       What is being edited
    \param name      Shown beside "Undo"/"Redo", e.g. "Rotate 90 CW"
    \param op        Makes the change
    \param make_undo Builds the entry that reverses it, called before \p op so it can capture whatever
                     \p op is about to overwrite
    \param extent    Whether \p op reshapes the image, in which case the layer tree is rebuilt and the view
                     refit afterwards
*/
bool modify_image(const EditContext &ctx, const std::string &name, const std::function<void(Image &)> &op,
                  const UndoFactory &make_undo, EditExtent extent = Extent_Samples);

/**
    Apply \p op to every sample the subject covers.

    \p op is handed a sample, its position in image coordinates, and which of the subject's channels it
    belongs to -- 0 for the first, so a group's R, G, B, A arrive as 0, 1, 2, 3 -- and returns what to
    replace it with. Both the GPU upload and the undo entry cover only the subject's rectangle.
*/
bool modify_pixels(const EditContext &ctx, const std::string &name, const std::function<float(float, int2, int)> &op);

/**
    Apply \p op to each covered group's channels together: their values, the position in image coordinates,
    and the group's index into Image::groups, which is how an op reads the group's other samples. Unlike
    modify_pixels(), which sees one sample at a time, this can mix channels into each other, as a
    color-space conversion or a channel mixer does.

    Only color groups are covered, RGB and RGBA; the subject's other channels are left alone. A group
    without alpha gets 1 in that slot and whatever \p op returns there is dropped.

    \p retag, if given, updates the image's color metadata to describe what \p op produced, recorded in the
    same history entry as the pixels so undoing takes back both.
*/
bool modify_colors(const EditContext &ctx, const std::string &name,
                   const std::function<float4(const float4 &, int2, int)> &op,
                   const std::function<void(Image &)>                     &retag = {});
