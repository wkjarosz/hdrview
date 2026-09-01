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

/*!
    What an edit command is allowed to do to the application.

    Deliberately narrow. Commands live under edit/ and are written against this rather than against
    HDRViewApp, so that adding one never means reaching further into the application than the edit
    itself needs -- and so a command can be exercised without constructing a window.

    Everything an edit is applied through is here, one method per way of changing an image. Each names
    the edit for the history and takes the subject from the context, so a command says what it does and
    not which image or which channels it does it to; see the corresponding HDRViewApp methods for what
    each one guarantees.
*/
struct EditContext
{
    virtual ~EditContext() = default;

    //! The image being edited; null when there is none, which every command must tolerate.
    virtual ImagePtr image() const = 0;
    //! Which of that image's samples the edit covers.
    virtual const EditSubject &subject() const = 0;

    virtual Box2i selection() const               = 0;
    virtual void  set_selection(const Box2i &box) = 0;
    //! The color the viewport draws behind the image, for the edits that composite against it.
    virtual float4 background_color() const = 0;

    //! The image last cut or copied, or null when nothing has been. Shared by every open image.
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

    //! Draw the shared "Apply to" controls, which every dialog that edits samples carries.
    virtual void draw_subject_selector() = 0;
};

/*!
    One undoable edit: what it is called, what it draws, and what it does.

    A command owns all three, so adding an edit is writing one of these and naming it in the table in
    commands.cpp, rather than spreading a declaration, a dialog registration, an action registration and a
    menu entry across four files.

    The dialog chrome is not a command's business: the shell, the "Apply to" selector, and the
    Cancel/Confirm footer are drawn around draw() by whoever is dispatching, so fifteen dialogs cannot
    drift apart in how they open, confirm and close. A command that needs none of that -- a flip, a
    quarter turn -- simply leaves draw() alone, and is then applied the moment it is invoked.

    Parameters live as members, which is what makes them persist between openings the way a dialog's
    settings should.
*/
class EditCommand
{
public:
    //! Everything the action registry, the menu and the command palette need to address this command.
    struct Info
    {
        //! Shown in the menu; the first is also the key the action registry and the tests address it by.
        //! A name ending in "..." is a dialog, as everywhere else in the interface.
        std::vector<std::string> names;
        std::string              icon;
        ImGuiKeyChord            chord = ImGuiKey_None;
        ImGuiInputFlags          flags = ImGuiInputFlags_None;

        //! What the confirming button says. Ignored by a command with no dialog.
        std::string confirm = "Apply";
        //! Least width of the dialog, in em -- multiples of the font size, as the rest of the interface
        //! is measured. Content wider than this widens it further.
        /*!
            In em rather than pixels because an Info is built before there is an ImGui context to ask for a
            size, and a minimum rather than a size because these dialogs size themselves from their
            contents; see HDRViewApp::draw_edit_command_dialog().
        */
        float width_em = 24.f;
        //! Whether the dialog carries the "Apply to" controls. False for the edits that cover the whole
        //! image whatever the subject says -- a crop, a resize.
        bool has_subject = true;

        //! Whether the image has to accept edits for this to be offered. False for the one command that
        //! only reads: copying is not editing, and is worth having on an image a renderer owns.
        bool needs_editable = true;
    };

    virtual ~EditCommand() = default;

    virtual Info info() const = 0;

    //! True when this is a dialog rather than an edit applied the moment it is chosen.
    bool has_dialog() const
    {
        // By value: info() returns a temporary, and a reference into it would outlive the thing it names.
        const std::string n = info().names.front();
        return n.size() >= 3 && n.compare(n.size() - 3, 3, "...") == 0;
    }

    //! Draw the command's own controls. Nothing but the parameters: the chrome is drawn around this.
    virtual void draw(EditContext &) {}

    //! Perform the edit. Called on confirm, or immediately for a command with no dialog.
    virtual void apply(EditContext &) = 0;

    //! Whether the command can run at all, beyond the image accepting edits, which is checked for it.
    virtual bool enabled(const EditContext &) const { return true; }

    //! Called as the dialog opens, for anything that has to be read from the image each time.
    virtual void on_open(EditContext &) {}
    //! Called as it closes, either way, for anything that must not be carried to the next image.
    virtual void on_close(EditContext &) {}
};

using EditCommandPtr = std::unique_ptr<EditCommand>;
