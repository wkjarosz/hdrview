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

/// What an edit command sees of the application: the image, the subject, and the ways to change it.
/**
    Each modify_* method names the edit for the history and takes the subject from the context; see the
    corresponding HDRViewApp methods for what they guarantee.
*/
struct EditContext
{
    virtual ~EditContext() = default;

    /// The image being edited; null when there is none.
    virtual ImagePtr image() const = 0;
    /// Which of that image's samples the edit covers.
    virtual const EditSubject &subject() const = 0;

    /// The channel groups an operation acts on: normally the selected ones.
    /**
        See HDRViewApp::target_groups() for how a right-clicked group overrides that. Empty when there are
        none.
    */
    virtual std::vector<int> target_groups() const = 0;

    virtual Box2i selection() const               = 0;
    virtual void  set_selection(const Box2i &box) = 0;
    /// The color the viewport draws behind the image, for the edits that composite against it.
    virtual float4 background_color() const = 0;

    /// The image last cut or copied, or null when nothing has been. Shared by every open image.
    virtual ConstImagePtr clipboard() const       = 0;
    virtual void          set_clipboard(ImagePtr) = 0;

    //
    // The ways an image can be changed. All but the async pair return whether anything was edited.
    //
    virtual bool modify_pixels(const std::string &name, const std::function<float(float, int2, int)> &op) = 0;
    virtual bool modify_colors(const std::string &name, const std::function<float4(const float4 &, int2)> &op,
                               const std::function<void(Image &)> &retag = {})                            = 0;
    virtual bool modify_neighborhood(const std::string                                                      &name,
                                     const std::function<float4(const std::function<float4(int2)> &, int2)> &op,
                                     int border_x, int border_y)                                          = 0;
    virtual bool modify_channels(const std::string                                              &name,
                                 const std::function<Array2Df(const Array2Df &, const Box2i &)> &filter)  = 0;
    virtual void modify_channels_async(
        const std::string                                                                   &name,
        const std::function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)> &f)          = 0;
    virtual void modify_image_async(const std::string &name, int2 size,
                                    const std::function<Array2Df(const Array2Df &, AtomicProgress)> &op) = 0;
    virtual bool modify_structure(const std::string &name, const std::function<void(Image &)> &op)       = 0;
    virtual bool modify_reversibly(const std::string &name, const std::function<void(Image &)> &forward,
                                   const std::function<void(Image &)> &backward)                         = 0;

    /// Put \p img into the image list beside the one being edited, and select it.
    /**
        \p partname is what the Images panel shows beside the file name, to tell several images made from
        one file apart.
    */
    virtual void add_image(ImagePtr img, const std::string &partname) = 0;

    /// Draw the shared "Apply to" controls.
    virtual void draw_subject_selector() = 0;
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
        ImGuiInputFlags          flags = ImGuiInputFlags_None;

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

    virtual ~EditCommand() = default;

    virtual Info info() const = 0;

    /// True when this is a dialog rather than an edit applied the moment it is chosen.
    bool has_dialog() const
    {
        // by value: info() returns a temporary
        const std::string n = info().names.front();
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
};

using EditCommandPtr = std::unique_ptr<EditCommand>;
