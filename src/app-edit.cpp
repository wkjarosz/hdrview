//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "image.h"
#include "imgui_ext.h"

#include <spdlog/spdlog.h>

using std::function;
using std::string;

bool HDRViewApp::can_edit(const ConstImagePtr &img) { return img && !img->is_live; }

void HDRViewApp::after_modify(const ImagePtr &img)
{
    // Statistics and histograms are cached against this; see Image::content_version.
    ++img->content_version;

    // Deliberately not finalize(): it does far more than rebuild the layer tree. It would premultiply a
    // straight-alpha image a second time, silently darkening it, and re-derive metadata that has not
    // changed. An edit that adds or removes channels needs the tree rebuilt and will have to ask for that
    // specifically; none of the edits so far touches the channel set.

    // The group the viewport is sampling may now be a different set of channels, or the same channels at
    // a different size.
    set_image_textures();
}

bool HDRViewApp::modify_image(const ImagePtr &img, const string &name, const function<void(Image &)> &op,
                              const function<UndoPtr(const Image &)> &make_undo)
{
    if (!can_edit(img))
    {
        spdlog::warn("Cannot edit '{}': its pixels come from a running process.", img ? img->filename : "");
        return false;
    }

    spdlog::debug("Editing '{}': {}", img->filename, name);

    // A statistics task reads these samples from a worker thread, so it has to be off them before the
    // write -- and before the undo entry reads them to record what the edit is about to displace.
    for (auto &c : img->channels) c.cancel_stats();

    // Built first: an entry that stores pixels has to see them as they were.
    auto entry = make_undo(*img);

    op(*img);

    if (entry)
        img->history.add(std::move(entry));

    after_modify(img);
    return true;
}

bool HDRViewApp::modify_image_reversibly(const ImagePtr &img, const string &name,
                                         const function<void(Image &)> &forward,
                                         const function<void(Image &)> &backward)
{
    return modify_image(img, name, forward,
                        [&name, forward, backward](const Image &) -> UndoPtr
                        {
                            // Undoing runs the opposite operation and redoing runs the original, so
                            // nothing has to be remembered but the two functions.
                            return std::make_unique<LambdaUndo>(name, backward, forward);
                        });
}

bool HDRViewApp::undo()
{
    auto img = current_image();
    if (!can_edit(img) || !img->history.has_undo())
        return false;

    for (auto &c : img->channels) c.cancel_stats();

    if (!img->history.undo(*img))
        return false;

    after_modify(img);
    return true;
}

bool HDRViewApp::redo()
{
    auto img = current_image();
    if (!can_edit(img) || !img->history.has_redo())
        return false;

    for (auto &c : img->channels) c.cancel_stats();

    if (!img->history.redo(*img))
        return false;

    after_modify(img);
    return true;
}

void HDRViewApp::close_image(int index)
{
    if (!is_valid(index))
        index = current_image_index();

    auto img = image(index);
    if (!img || !img->history.is_modified())
    {
        close_image_immediately(index);
        return;
    }

    m_pending_discard                       = PendingDiscard::CloseImage;
    m_pending_close_index                   = index;
    dialog("Discard unsaved changes?").open = true;
}

void HDRViewApp::close_all_images()
{
    if (!any_image_modified())
    {
        close_all_images_immediately();
        return;
    }

    m_pending_discard                       = PendingDiscard::CloseAll;
    dialog("Discard unsaved changes?").open = true;
}

bool HDRViewApp::any_image_modified() const
{
    for (const auto &img : m_images)
        if (img && img->history.is_modified())
            return true;
    return false;
}

void HDRViewApp::apply_pending_discard()
{
    const auto what   = m_pending_discard;
    m_pending_discard = PendingDiscard::None;

    switch (what)
    {
    case PendingDiscard::CloseImage: close_image_immediately(m_pending_close_index); break;
    case PendingDiscard::CloseAll: close_all_images_immediately(); break;
    case PendingDiscard::Quit: m_params.appShallExit = true; break;
    case PendingDiscard::None: break;
    }

    m_pending_close_index = -1;
}

void HDRViewApp::draw_confirm_discard_dialog(bool &open)
{
    // One prompt for all three, since what is at stake is the same in each case: edits that exist only in
    // memory. Which of them is being asked about is in m_pending_discard.
    const char *message = m_pending_discard == PendingDiscard::CloseImage
                              ? "This image has edits that have not been saved. Closing it will discard them."
                              : "Some open images have edits that have not been saved. Continuing will discard them.";

    auto result = ImGui::ConfirmDialog("Discard unsaved changes?", open, message, "Discard");
    if (result == ImGui::DialogResult::Confirm)
        apply_pending_discard();
    else if (result == ImGui::DialogResult::Cancel)
    {
        m_pending_discard     = PendingDiscard::None;
        m_pending_close_index = -1;
    }
}
