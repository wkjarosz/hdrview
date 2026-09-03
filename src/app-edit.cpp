//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "edit/envmap.h"
#include "edit/filters.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <hello_imgui/hello_imgui.h>

#include <numeric>
#include <smallthreadpool.h>
#include <spdlog/spdlog.h>
#include <thread>

using std::function;
using std::string;

ImagePtr HDRViewApp::target_image() { return m_target_image_override ? m_target_image_override : current_image(); }

std::vector<int> HDRViewApp::target_groups(const ConstImagePtr &img) const
{
    // the override names a group of one image, and every image numbers its own groups, so it says
    // nothing about the others a fan-out reaches
    return ::target_groups(img, img == m_target_image_override ? m_target_group_override : -1);
}

void HDRViewApp::with_target_group(int image_index, int group, const std::function<void()> &body)
{
    // restored however `body` leaves things: pointing at a group must not move the selection, and an
    // action that removes the group would leave the override naming one that is gone
    auto      previous_image = m_target_image_override;
    const int previous_group = m_target_group_override;

    m_target_image_override = image(image_index);
    m_target_group_override = m_target_image_override ? group : -1;
    body();
    m_target_image_override = previous_image;
    m_target_group_override = previous_group;
}

void HDRViewApp::invoke_action_on_group(const string &action_name, int image_index, int group)
{
    with_target_group(image_index, group, [this, &action_name] { action(action_name).callback(); });
}

std::vector<ImagePtr> HDRViewApp::edit_command_images(const EditCommand &cmd)
{
    // a pointed-at group names one image, which need not be the current one; see target_groups()
    if (m_target_image_override)
    {
        if (!cmd.info().fans_out || !m_target_image_override->is_group_selected(m_target_group_override))
            return {m_target_image_override};
        return selected_images();
    }

    auto current = current_image();
    if (!current)
        return {};

    if (!cmd.info().fans_out)
        return {current};

    return selected_images();
}

EditContext HDRViewApp::edit_context(ImagePtr img)
{
    if (!img)
        img = target_image();

    EditContext ctx;
    ctx.target_groups = target_groups(img);
    ctx.image         = std::move(img);
    ctx.subject       = m_edit_subject;
    ctx.roi           = m_roi;
    ctx.background    = m_bg_color;
    ctx.clipboard     = &m_clipboard;

    ctx.add_image             = [this](ImagePtr i, string partname) { add_image_beside_current(i, partname); };
    ctx.set_selection         = [this](const Box2i &box) { set_selection(box); };
    ctx.draw_subject_selector = [this] { draw_edit_subject_selector(); };
    ctx.edited                = [this](EditExtent extent)
    {
        // recomputes each group's visibility and the layer tree's counts, then rebinds the textures
        update_visibility();

        // the view was framing an image of a different size
        if (extent == Extent_Structure)
            fit_display_window();
    };

    const ImagePtr    subject_image = ctx.image;
    const EditSubject subject       = ctx.subject;
    ctx.modify_channels_async       = [this, subject_image, subject](const string &name, const ChannelFilter &filter)
    { modify_channels_async(subject_image, name, subject, filter); };
    ctx.resample_image_async = [this, subject_image](const string &name, int2 size, const ChannelResampler &op)
    { resample_image_async(subject_image, name, size, op); };

    return ctx;
}

void HDRViewApp::apply_edit_command(EditCommand &cmd)
{
    // one context and one apply() per image, so each lands as its own undo entry. Each starts from the
    // same selection, since cropping clears it and the images after the first would find nothing to crop.
    const Box2i roi = m_roi;
    for (const auto &img : edit_command_images(cmd))
    {
        m_roi = roi;
        cmd.apply(edit_context(img));
    }
}

void HDRViewApp::invoke_edit_command(EditCommand &cmd)
{
    if (cmd.info().has_dialog)
    {
        dialog(cmd.info().names.front()).open = true;
        return;
    }

    apply_edit_command(cmd);
}

bool HDRViewApp::edit_command_enabled(const EditCommand &cmd)
{
    if (cmd.info().needs_editable && !can_edit(target_image()))
        return false;
    return cmd.enabled(edit_context());
}

void HDRViewApp::draw_edit_command_dialog(EditCommand &cmd, bool &open)
{
    const auto &info = cmd.info();
    auto        ctx  = edit_context();

    // before BeginModalDialog(), which consumes `open`: this is the frame the dialog was asked for
    if (open)
        cmd.on_open(ctx);

    if (ImGui::BeginModalDialog(info.names.front().c_str(), open, ImGui::DialogPosition::Center))
    {
        // a minimum width, set by an item of that width: these dialogs are AlwaysAutoResize, which sizes
        // the window from its contents and ignores a size set from outside
        ImGui::Dummy(ImVec2(info.width_em * HelloImGui::EmSize(), 0.f));

        cmd.draw(ctx);

        if (info.draws_subject_selector)
            draw_edit_subject_selector();

        // applied on confirm, not as the controls move: an edit per frame of a drag would fill the
        // history and rewrite every pixel it covers
        const auto result = ImGui::DialogButtons(info.confirm.c_str());
        if (result == ImGui::DialogResult::Confirm)
        {
            // the dialog is drawn against the current image, but confirming applies across the selection
            apply_edit_command(cmd);
            cmd.on_close(ctx);
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
        {
            cmd.on_close(ctx);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::after_modify(const ImagePtr &img)
{
    ++img->content_version; // invalidates the cached statistics and histograms

    // not finalize(): that would premultiply a straight-alpha image a second time. A structural entry has
    // rebuilt the layer tree itself by the time this runs.
    update_visibility();
}

bool HDRViewApp::step_selected_histories(bool forward)
{
    // one image's own history, stepped one entry; false when it had nothing to step
    auto step = [this, forward](const ImagePtr &img)
    {
        if (!can_edit(img) || (forward ? !img->history.has_redo() : !img->history.has_undo()))
            return false;

        for (auto &c : img->channels) c.cancel_stats();

        if (!(forward ? img->history.redo(*img) : img->history.undo(*img)))
            return false;

        after_modify(img);
        return true;
    };

    // every selected image steps, but the answer is the current image's
    auto current = current_image();
    bool stepped = false;
    for (const auto &img : selected_images())
    {
        const bool ok = step(img);
        if (img == current)
            stepped = ok;
    }
    return stepped;
}

bool HDRViewApp::undo() { return step_selected_histories(false); }

bool HDRViewApp::redo() { return step_selected_histories(true); }

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

bool HDRViewApp::scope_matters(const ConstImagePtr &img)
{
    // with one group every scope names the same channels, so there is nothing to decide
    return img && img->groups.size() > 1;
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
    // one prompt for all three cases; which is being asked about is in m_pending_discard
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

void HDRViewApp::draw_edit_subject_selector()
{
    // the same setting the Edit menu shows, so the two cannot disagree about what the next edit covers
    auto       img     = current_image();
    const bool matters = scope_matters(img);

    ImGui::SeparatorText("Apply to");

    // always settable, since the setting is remembered across images
    for (int i = 0; i < EditSubject::Scope_COUNT; ++i)
        if (ImGui::RadioButton(edit_scope_name(i), m_edit_subject.scope == i))
            m_edit_subject.scope = EditSubject::Scope(i);
    if (!matters && img)
        ImGui::Tooltip("This image has a single channel group, so all three choices cover the same channels.");

    ImGui::Checkbox("Selection only", &m_edit_subject.selection_only);
    if (!m_roi.has_volume())
        ImGui::Tooltip("There is no selection, so edits cover the whole image.");
}

void HDRViewApp::modify_channels_async(const ImagePtr &img, const string &name, const EditSubject &subject,
                                       const ChannelFilter &filter)
{
    if (!can_edit(img))
        return;

    if (m_running_filter)
    {
        m_filter_queue.push_back([this, img, name, subject, filter]
                                 { modify_channels_async(img, name, subject, filter); });
        return;
    }

    auto [channels, bounds] = resolve_subject(img, subject, m_roi);
    if (channels.empty() || !bounds.has_volume())
        return;

    auto running      = std::make_unique<RunningFilter>();
    running->image    = img;
    running->name     = name;
    running->channels = channels;
    running->bounds   = bounds;

    // the filter sees the whole channel but produces only this rectangle, so a selection costs the
    // selection and not the image
    const int2  offset = bounds.min - img->data_window.min;
    const Box2i local{offset, offset + bounds.size()};
    running->filter = [filter, local](const Array2Df &channel, int slot, AtomicProgress p)
    { return filter(channel, local, slot, p); };

    start_filter(std::move(running));
}

void HDRViewApp::resample_image_async(const ImagePtr &img, const string &name, int2 size, const ChannelResampler &op)
{
    if (!can_edit(img) || size.x <= 0 || size.y <= 0)
        return;

    if (m_running_filter)
    {
        m_filter_queue.push_back([this, img, name, size, op] { resample_image_async(img, name, size, op); });
        return;
    }

    auto running      = std::make_unique<RunningFilter>();
    running->image    = img;
    running->name     = name;
    running->bounds   = img->data_window;
    running->new_size = size;
    running->channels.resize(img->channels.size());
    for (size_t i = 0; i < img->channels.size(); ++i) running->channels[i] = int(i);
    running->filter = [op](const Array2Df &channel, int, AtomicProgress p) { return op(channel, p); };

    start_filter(std::move(running));
}

void HDRViewApp::start_filter(std::unique_ptr<RunningFilter> running)
{
    // the statistics tasks read the very pixels the filter is about to
    for (auto &c : running->image->channels) c.cancel_stats();

    running->results.resize(running->channels.size());

    RunningFilter *raw = running.get();
    m_running_filter   = std::move(running);

    // filters every channel into raw->results; runs on a worker where there is one, see below
    auto do_the_work = [raw]
    {
        const float share = 1.f / float(raw->channels.size());
        for (size_t i = 0; i < raw->channels.size() && !raw->progress.canceled(); ++i)
        {
            // a share of the same total, not a copy, so Cancel reaches the filter partway through a
            // channel and not only between channels
            raw->results[i] = raw->filter(raw->image->channels[size_t(raw->channels[i])], int(i),
                                          AtomicProgress{raw->progress, share});
        }

        if (!raw->progress.canceled())
            raw->progress.set_done();

        raw->done.store(true);
    };

#if defined(__EMSCRIPTEN__)
    // the web build has no pthreads, so this runs inline: the page stops responding for the duration and
    // there is nothing to cancel
    do_the_work();
    drain_running_filter();
#else
    dialog("Applying filter...").open = true;

    // reading the channels is safe for as long as this runs: a second edit is refused while a filter is
    // in flight, and the image cannot be closed without canceling it. Kept rather than detached, so
    // closing the window can stop it and wait; see ~RunningFilter().
    raw->worker = std::thread(
        [this, do_the_work]
        {
            do_the_work();
            // the frame loop may be idle waiting on window events rather than spinning
            wake_event_loop();
        });
#endif
}

void HDRViewApp::drain_running_filter()
{
    if (!m_running_filter || !m_running_filter->done.load())
        return;

    auto running     = std::move(m_running_filter);
    m_running_filter = nullptr;

    // a partial result is a wrong one, so an abandoned run changes nothing
    if (running->progress.canceled())
    {
        spdlog::debug("Filter '{}' was canceled.", running->name);
        // cancel means the whole run, not just the image it had reached
        m_filter_queue.clear();
        return;
    }

    const auto &channels = running->channels;
    const Box2i bounds   = running->bounds;
    auto       &results  = running->results;

    auto ctx = edit_context(running->image);

    if (const int2 size = running->new_size; size.x > 0 && size.y > 0)
    {
        // the results are a different size than what they were computed from, so the channels are
        // replaced outright and the windows moved to match
        modify_image(
            ctx, running->name,
            [size, &results](Image &image)
            {
                for (size_t i = 0; i < image.channels.size(); ++i)
                {
                    image.channels[i].resize(size);
                    std::copy(results[i].data(), results[i].data() + results[i].num_elements(),
                              image.channels[i].data());
                    image.channels[i].texture_is_dirty = true;
                }
                image.data_window    = Box2i{image.data_window.min, image.data_window.min + size};
                image.display_window = image.data_window;
            },
            structure_undo, Extent_Structure);
        start_next_filter();
        return;
    }

    modify_image(
        ctx, running->name,
        [&channels, &bounds, &results](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();
            for (size_t i = 0; i < channels.size(); ++i)
                image.channels[size_t(channels[i])].upload_tile(Box2i{offset, offset + extent}, results[i].data());
        },
        [&channels, &bounds](const Image &image, const string &n) -> UndoPtr
        { return std::make_unique<ChannelRectUndo>(image, channels, bounds, n); });

    start_next_filter();
}

void HDRViewApp::start_next_filter()
{
    if (m_filter_queue.empty())
        return;

    auto next = m_filter_queue.front();
    m_filter_queue.erase(m_filter_queue.begin());
    next();
}

void HDRViewApp::draw_filter_progress_dialog(bool &open)
{
    if (!m_running_filter)
    {
        open = false;
        return;
    }

    if (ImGui::BeginModalDialog("Applying filter...", open, ImGui::DialogPosition::Center))
    {
        if (!m_running_filter)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        // as above: the window is sized from its contents, so a bar filling the available width has none
        // to fill and the dialog shrinks to the Cancel button
        ImGui::Dummy(ImVec2(24.f * HelloImGui::EmSize(), 0.f));

        ImGui::TextUnformatted(m_running_filter->name.c_str());
        if (!m_filter_queue.empty())
        {
            // the bar measures one image; a multi-selection runs them one after another
            ImGui::SameLine();
            ImGui::TextDisabled("(%d more image%s)", int(m_filter_queue.size()), m_filter_queue.size() == 1 ? "" : "s");
        }
        ImGui::ProgressBar(m_running_filter->progress.progress(), ImVec2(-FLT_MIN, 0.f));

        // only asks the filter to stop; drain_running_filter() throws the partial result away
        if (ImGui::Button("Cancel") || ImGui::Shortcut(ImGuiKey_Escape))
            m_running_filter->progress.cancel();

        if (m_running_filter->done.load())
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
