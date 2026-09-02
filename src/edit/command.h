//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include "edit/progress.h"
#include "edit/subject.h"
#include "fwd.h"

#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

/// What an edit changed besides sample values, which says what has to be rebuilt afterwards.
enum EditExtent : int
{
    Extent_Samples = 0, ///< Samples within the channels the image already had
    Extent_Structure    ///< The channel list or the windows: the layer tree is rebuilt and the view refit
};

/// Filters one channel of an image.
/**
    Handed the whole channel, the rectangle of it to produce in channel-local coordinates, which of the
    covered channels it is, and a share of the progress bar.
*/
using ChannelFilter = std::function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)>;
/// Computes one channel of a whole new image from the channel it replaces.
using ImageFilter = std::function<Array2Df(const Array2Df &, AtomicProgress)>;

/// What an edit command sees of the application: the image, the subject, and the ways to change it.
/**
    Filled in by HDRViewApp::edit_context(), and by the tests directly. The ways of changing an image are
    free functions over this; see edit/edit_ops.h. The two here are the ones needing the application's
    worker thread and progress dialog.
*/
struct EditContext
{
    /// The image being edited; null when there is none.
    ImagePtr image;
    /// Which of that image's samples an edit covers.
    EditSubject subject;
    /// The rectangular selection, in image coordinates; empty when there is none.
    Box2i roi;
    /// The channel groups a group edit acts on: normally the selected ones.
    /**
        A group pointed at from outside the selection names itself alone; see HDRViewApp::target_groups().
    */
    std::vector<int> target_groups;
    /// The color the viewport draws behind the image, for the edits that composite against it.
    float4 background{0.f, 0.f, 0.f, 1.f};
    /// The image last cut or copied, shared by every open image; the pointee is null until one has been.
    ImagePtr *clipboard = nullptr;

    /// Put an image into the list beside the one being edited, and select it.
    /**
        The second argument is what the Images panel shows beside the file name, to tell several images made
        from one file apart.
    */
    std::function<void(ImagePtr, std::string)> add_image;
    /// Set the selection, which cropping clears.
    std::function<void(const Box2i &)> set_selection;
    /// Draw the shared "Apply to" controls.
    std::function<void()> draw_subject_selector;
    /// Called once an edit has landed, so the application can rebind its textures and refit the view.
    std::function<void(EditExtent)> edited;

    /// Filter the subject's channels on a worker, with a progress bar that can cancel it.
    /**
        The image is only touched once every channel is done, back on the main thread and through
        modify_image(), so the edit still lands as one undoable step. Returns having only started the work;
        a canceled filter discards its partial result and changes nothing.
    */
    std::function<void(const std::string &name, const ChannelFilter &filter)> modify_channels_async;
    /// Replace the image wholesale with something computed from it at a new size, off the main thread.
    /**
        For the environment-map operations. The results are swapped in together, as one undoable step.
    */
    std::function<void(const std::string &name, int2 size, const ImageFilter &op)> modify_image_async;
};

/// One undoable edit: what it is called, what it draws, and what it does.
/**
    Add one by writing a subclass and naming it in the table in commands.cpp.

    The dialog shell, the "Apply to" selector and the Cancel/Confirm footer are drawn around draw() by
    HDRViewApp::draw_edit_command_dialog(); a command that leaves draw() alone has no dialog and is applied
    the moment it is invoked. Parameters live as members, so they persist between openings.
*/
class EditCommand
{
public:
    /// Everything the action registry, the menu and the command palette need to address this command.
    struct Info
    {
        /// Shown in the menu; the first is also the key the action registry and the tests address it by.
        /**
            A name ending in "..." is a dialog, as everywhere else in the interface.
        */
        std::vector<std::string> names;
        std::string              icon;
        ImGuiKeyChord            chord = ImGuiKey_None;

        /// What the confirming button says. Ignored by a command with no dialog.
        std::string confirm = "Apply";
        /// Least width of the dialog, in em; content wider than this widens it further.
        /**
            In em because an Info is built before there is an ImGui context to ask for a size.
        */
        float width_em = 24.f;
        /// Whether the "Apply to" settings mean anything for this edit, and so whether its dialog shows them.
        /**
            False for the edits that replace or reshape the whole image.
        */
        bool draws_subject_selector = true;

        /// With several images selected, whether this runs on all of them or only the current one.
        /**
            Each gets its own invocation and undo entry.
        */
        bool fans_out = true;

        /// Whether the image has to accept edits for this to be offered.
        bool needs_editable = true;
    };

    explicit EditCommand(Info info) : m_info(std::move(info)) {}
    virtual ~EditCommand() = default;

    /// Built once, in the constructor: the menu, the palette and the action registry read it every frame.
    const Info &info() const { return m_info; }

    /// True when this is a dialog rather than an edit applied the moment it is chosen.
    bool has_dialog() const
    {
        const std::string &n = m_info.names.front();
        return n.size() >= 3 && n.compare(n.size() - 3, 3, "...") == 0;
    }

    /// Draw the command's own parameter controls; the chrome is drawn around this.
    virtual void draw(EditContext &) {}

    /// Perform the edit. Called on confirm, or immediately for a command with no dialog.
    virtual void apply(EditContext &) = 0;

    /// Whether the command can run at all, beyond the image accepting edits.
    virtual bool enabled(const EditContext &) const { return true; }

    /// Called as the dialog opens, for anything that has to be read from the image each time.
    virtual void on_open(EditContext &) {}
    /// Called as it closes, either way, for anything that must not be carried to the next image.
    virtual void on_close(EditContext &) {}

protected:
    /// A subclass constructor may adjust the fields it would rather set by name than pass positionally.
    Info m_info;
};

using EditCommandPtr = std::unique_ptr<EditCommand>;
