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

/// How far an edit reached, which says what has to be rebuilt afterwards.
enum EditExtent : int
{
    Extent_Pixels = 0, ///< Pixels within the channels the image already had
    Extent_Structure   ///< The channel list or the windows: the layer tree is rebuilt and the view refit
};

/// Produces a rectangle of one channel, in channel-local coordinates.
/**
    Given the whole channel to read from, its index among those covered, and a share of the progress bar.
*/
using ChannelFilter = std::function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)>;
/// Produces a whole channel at the image's new size, from the channel it replaces.
using ChannelResampler = std::function<Array2Df(const Array2Df &, AtomicProgress)>;

/// What an edit command sees of the application: the image, the subject, and the ways to change it.
/**
    Filled in by HDRViewApp::edit_context(), and by the tests directly. Most ways of changing an image are
    free functions over this (see edit/edit_ops.h); the two here need the worker thread and progress dialog.
*/
struct EditContext
{
    ImagePtr         image;                          ///< The image being edited; null when there is none.
    EditSubject      subject;                        ///< Which of the image's pixels an edit covers.
    Box2i            roi;                            ///< The selection, in image coordinates; may be empty.
    std::vector<int> target_groups;                  ///< The groups a group edit acts on; see target_groups().
    float4           background{0.f, 0.f, 0.f, 1.f}; ///< The color the viewport draws behind the image.
    ImagePtr        *clipboard = nullptr;            ///< The image last cut or copied, shared by every image.

    /// Put an image into the list beside the one being edited and select it; the string is its partname.
    std::function<void(ImagePtr, std::string)> add_image;

    std::function<void(const Box2i &)> set_selection;         ///< Set the selection, which cropping clears.
    std::function<void()>              draw_subject_selector; ///< Draw the shared "Apply to" controls.

    /// Called once an edit has landed, so the application can rebind its textures and refit the view.
    std::function<void(EditExtent)> edited;

    /// Filter the subject's channels on a worker, behind a cancellable progress bar; one undoable step.
    std::function<void(const std::string &name, const ChannelFilter &filter)> modify_channels_async;
    /// The same, resampling every channel into a new size, which replaces the image.
    std::function<void(const std::string &name, int2 size, const ChannelResampler &op)> resample_image_async;
};

/// One undoable edit: what it is called, what it draws, and what it does.
/**
    Add one by writing a subclass and naming it in the table in commands.cpp. Parameters live as members, so
    they persist between openings; HDRViewApp::draw_edit_command_dialog() draws the dialog shell, the
    "Apply to" selector and the Cancel/Confirm footer around draw().
*/
class EditCommand
{
public:
    /// Everything the action registry, the menu and the command palette need to address this command.
    struct Info
    {
        /// Shown in the menu; the first is also the key the action registry and the tests address it by.
        std::vector<std::string> names;
        std::string              icon;
        ImGuiKeyChord            chord = ImGuiKey_None;

        std::string confirm  = "Apply"; ///< What the confirming button says. Ignored without a dialog.
        float       width_em = 24.f;    ///< Least dialog width in em; an Info predates the ImGui context.

        bool draws_subject_selector = true;  ///< Whether "Apply to" means anything here, and so is shown.
        bool has_dialog             = false; ///< Whether invoking this opens a dialog rather than editing at once.
        bool fans_out               = true;  ///< Whether a multi-selection runs this once per image.
        bool needs_editable         = true;  ///< Whether the image has to accept edits for this to be offered.
    };

    explicit EditCommand(Info info) : m_info(std::move(info)) {}
    virtual ~EditCommand() = default;

    /// Built once, in the constructor: the menu, the palette and the action registry read it every frame.
    const Info &info() const { return m_info; }

    /// Draw the command's own parameter controls; the chrome is drawn around this.
    virtual void draw(EditContext &) {}

    /// Perform the edit. Called on confirm, or immediately for a command with no dialog.
    virtual void apply(const EditContext &) = 0;

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
