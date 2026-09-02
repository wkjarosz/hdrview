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

bool HDRViewApp::can_edit(const ConstImagePtr &img) { return img && !img->is_live; }

namespace
{

//! HDRViewApp seen through the narrow opening edit commands are written against; see edit/command.h.
//! Cheap enough to build on the stack wherever one is needed. Every method supplies the current image and
//! subject, so a command names only what it is doing.
struct AppEditContext final : EditContext
{
    HDRViewApp *app;

    //! The image this run of the command is against: the current one unless it is fanning out over the
    //! selection; see HDRViewApp::apply_edit_command().
    ImagePtr img;

    explicit AppEditContext(HDRViewApp *a) : app(a), img(a->target_image()) {}
    AppEditContext(HDRViewApp *a, ImagePtr i) : app(a), img(std::move(i)) {}

    ImagePtr           image() const override { return img; }
    const EditSubject &subject() const override { return app->edit_subject(); }

    std::vector<int> target_groups() const override { return app->target_groups(img); }
    Box2i            selection() const override { return app->roi(); }
    void             set_selection(const Box2i &box) override { app->set_selection(box); }
    float4           background_color() const override { return app->background_color(); }
    ConstImagePtr    clipboard() const override { return app->clipboard(); }
    void             set_clipboard(ImagePtr img) override { app->set_clipboard(std::move(img)); }

    bool modify_pixels(const string &name, const function<float(float, int2, int)> &op) override
    {
        return app->modify_pixels(img, name, app->edit_subject(), op);
    }
    bool modify_colors(const string &name, const function<float4(const float4 &, int2)> &op,
                       const function<void(Image &)> &retag) override
    {
        return app->modify_colors(img, name, app->edit_subject(), op, retag);
    }
    bool modify_neighborhood(const string &name, const function<float4(const function<float4(int2)> &, int2)> &op,
                             int border_x, int border_y) override
    {
        return app->modify_neighborhood(img, name, app->edit_subject(), op, border_x, border_y);
    }
    bool modify_channels(const string &name, const function<Array2Df(const Array2Df &, const Box2i &)> &filter) override
    {
        return app->modify_channels(img, name, app->edit_subject(), filter);
    }
    void modify_channels_async(
        const string &name, const function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)> &f) override
    {
        app->modify_channels_async(img, name, app->edit_subject(), f);
    }
    void modify_image_async(const string &name, int2 size,
                            const function<Array2Df(const Array2Df &, AtomicProgress)> &op) override
    {
        app->modify_image_async(img, name, size, op);
    }
    bool modify_structure(const string &name, const function<void(Image &)> &op) override
    {
        return app->modify_structure(img, name, op);
    }
    bool modify_reversibly(const string &name, const function<void(Image &)> &forward,
                           const function<void(Image &)> &backward) override
    {
        return app->modify_image_reversibly(img, name, forward, backward);
    }

    void add_image(ImagePtr img, const std::string &partname) override { app->add_image_beside_current(img, partname); }

    void draw_subject_selector() override { app->draw_edit_subject_selector(); }
};

} // namespace

ImagePtr HDRViewApp::target_image() { return m_target_image_override ? m_target_image_override : current_image(); }

std::vector<int> HDRViewApp::target_groups(const ConstImagePtr &img) const
{
    // the override names a group of one image, and every image numbers its own groups, so it says
    // nothing about the others a fan-out reaches
    return target_groups(img, img == m_target_image_override ? m_target_group_override : -1);
}

std::vector<int> HDRViewApp::target_groups(const ConstImagePtr &img, int pointed_at) const
{
    if (!img)
        return {};

    // a group pointed at from the Images panel names itself alone, unless it is one of the selected
    // ones, in which case the right-click covers the selection, the same way a click inside one does
    if (pointed_at >= 0 && !img->is_group_selected(pointed_at))
        return {pointed_at};

    // the fallback is for an image the panel has never had a say over: one a command just produced, or
    // a test driving a command directly
    std::vector<int> groups = img->selected_groups();
    if (groups.empty() && img->is_valid_group(img->active_group_index(Target_Primary)))
        groups.push_back(img->active_group_index(Target_Primary));

    return groups;
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

void HDRViewApp::apply_edit_command(EditCommand &cmd)
{
    // one context and one apply() per image, so each lands as its own undo entry. Each starts from the
    // same selection, since cropping clears it and the images after the first would find nothing to crop.
    const Box2i roi = m_roi;
    for (const auto &img : edit_command_images(cmd))
    {
        m_roi = roi;
        AppEditContext ctx{this, img};
        cmd.apply(ctx);
    }
}

void HDRViewApp::invoke_edit_command(EditCommand &cmd)
{
    if (cmd.has_dialog())
    {
        dialog(cmd.info().names.front()).open = true;
        return;
    }

    apply_edit_command(cmd);
}

bool HDRViewApp::edit_command_enabled(const EditCommand &cmd)
{
    AppEditContext ctx{this};
    if (cmd.info().needs_editable && !can_edit(target_image()))
        return false;
    return cmd.enabled(ctx);
}

void HDRViewApp::draw_edit_command_dialog(EditCommand &cmd, bool &open)
{
    const auto     info = cmd.info();
    AppEditContext ctx{this};

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
        // history and rewrite every sample it covers
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

    // not finalize(): that would premultiply a straight-alpha image a second time. An edit that changes
    // the channel set rebuilds the layer tree itself; see modify_structure().
    update_visibility();
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

    // a statistics task reads these samples from a worker thread, so it has to be off them before the
    // write, and before the undo entry reads them
    for (auto &c : img->channels) c.cancel_stats();

    // built first: an entry that stores pixels has to see them as they were
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
                            // nothing to remember but the two functions
                            return std::make_unique<LambdaUndo>(name, backward, forward);
                        });
}

bool HDRViewApp::modify_channels(const ImagePtr &img, const string &name, const EditSubject &subject,
                                 const function<Array2Df(const Array2Df &, const Box2i &)> &filter)
{
    if (!can_edit(img))
        return false;

    auto [channels, bounds] = resolve_subject(img, subject);
    if (channels.empty() || !bounds.has_volume())
        return false;

    return modify_image(
        img, name,
        [&channels, &bounds, &filter](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();

            for (int c : channels)
            {
                Channel &channel = image.channels[size_t(c)];

                // the filter sees the whole channel but produces only this rectangle, so a selection
                // costs the selection and not the image
                const Box2i    local    = Box2i{offset, offset + extent};
                const Array2Df filtered = filter(channel, local);

                channel.upload_tile(local, filtered.data());
            }
        },
        [&channels, &bounds, &name](const Image &image) -> UndoPtr
        { return std::make_unique<ChannelRectUndo>(image, channels, bounds, name); });
}

//! The groups \p subject's scope names, before any filtering by what they contain.
static std::vector<int> subject_groups(const Image &img, const EditSubject &subject)
{
    std::vector<int> groups;
    if (subject.scope == EditSubject::Scope_AllChannels)
    {
        for (int g = 0; g < int(img.groups.size()); ++g) groups.push_back(g);
    }
    else if (subject.scope == EditSubject::Scope_SelectedGroups)
        groups = img.selected_groups();
    else if (int g = img.active_group_index(Target_Primary); img.is_valid_group(g))
        groups.push_back(g);

    return groups;
}

std::vector<int> subject_channels(const Image &img, const EditSubject &subject)
{
    // every channel, not the union of every group's, so one belonging to no group is still covered
    if (subject.scope == EditSubject::Scope_AllChannels)
    {
        std::vector<int> channels(img.channels.size());
        std::iota(channels.begin(), channels.end(), 0);
        return channels;
    }

    std::vector<int> channels;
    for (int g : subject_groups(img, subject))
    {
        const auto &group = img.groups[size_t(g)];
        for (int c = 0; c < group.num_channels; ++c) channels.push_back(group.channels[c]);
    }
    return channels;
}

std::pair<std::vector<int>, std::vector<int>> subject_color_groups(const Image &img, const EditSubject &subject)
{
    std::vector<int> groups = subject_groups(img, subject);

    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [&img](int g)
                                {
                                    const auto t = img.groups[size_t(g)].type;
                                    return t != ChannelGroup::RGB_Channels && t != ChannelGroup::RGBA_Channels;
                                }),
                 groups.end());

    // every channel of every covered group, which is the set an undo entry has to hold
    std::vector<int> channels;
    for (int g : groups)
    {
        const auto &group = img.groups[size_t(g)];
        for (int c = 0; c < group.num_channels; ++c) channels.push_back(group.channels[c]);
    }

    return {groups, channels};
}

bool HDRViewApp::modify_colors(const ImagePtr &img, const string &name, const EditSubject &subject,
                               const function<float4(const float4 &, int2)> &op, const function<void(Image &)> &retag)
{
    if (!can_edit(img))
        return false;

    auto [groups, channels] = subject_color_groups(*img, subject);
    if (groups.empty())
    {
        // e.g. an ungrouped image or a depth pass: nothing here is color
        spdlog::warn("'{}' covers no color channel group of '{}'.", name, img->file_and_partname());
        return false;
    }

    Box2i bounds = img->data_window;
    if (subject.selection_only && m_roi.has_volume())
        bounds.intersect(m_roi);
    if (!bounds.has_volume())
        return false;

    return modify_image(
        img, name,
        [&groups, &bounds, &op, &retag](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();

            for (int g : groups)
            {
                const auto &group = image.groups[size_t(g)];
                const int   n     = group.num_channels;

                // the op sees the components together, so all of them are staged before any is written
                std::array<Array2Df, 4> staging;
                for (int c = 0; c < n; ++c) staging[size_t(c)] = Array2Df{extent};

                const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
                stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                                  [&](int y0, int y1, int, int)
                                  {
                                      for (int y = y0; y < y1; ++y)
                                          for (int x = 0; x < extent.x; ++x)
                                          {
                                              // opaque where the group has no alpha, so an op may read the
                                              // fourth component whatever kind of group it was handed
                                              float4 c{0.f, 0.f, 0.f, 1.f};
                                              for (int k = 0; k < n; ++k)
                                                  c[k] = image.channels[size_t(group.channels[k])](offset.x + x,
                                                                                                   offset.y + y);

                                              const float4 out = op(c, int2{bounds.min.x + x, bounds.min.y + y});

                                              for (int k = 0; k < n; ++k) staging[size_t(k)](x, y) = out[k];
                                          }
                                  });

                for (int c = 0; c < n; ++c)
                    image.channels[size_t(group.channels[c])].upload_tile(Box2i{offset, offset + extent},
                                                                          staging[size_t(c)].data());
            }

            if (retag)
                retag(image);
        },
        [&channels, &bounds, &name, &retag](const Image &image) -> UndoPtr
        {
            auto pixels = std::make_unique<ChannelRectUndo>(image, channels, bounds, name);
            if (!retag)
                return pixels;

            // the samples and what they mean changed together, so they are taken back together
            std::vector<UndoPtr> both;
            both.push_back(std::move(pixels));
            both.push_back(std::make_unique<ColorMetadataUndo>(image, name));
            return std::make_unique<CompositeUndo>(name, std::move(both));
        });
}

bool HDRViewApp::modify_neighborhood(const ImagePtr &img, const string &name, const EditSubject &subject,
                                     const function<float4(const function<float4(int2)> &, int2)> &op, int border_x,
                                     int border_y)
{
    if (!can_edit(img))
        return false;

    auto [groups, channels] = subject_color_groups(*img, subject);
    if (groups.empty())
    {
        spdlog::warn("'{}' covers no color channel group of '{}'.", name, img->file_and_partname());
        return false;
    }

    Box2i bounds = img->data_window;
    if (subject.selection_only && m_roi.has_volume())
        bounds.intersect(m_roi);
    if (!bounds.has_volume())
        return false;

    return modify_image(
        img, name,
        [&groups, &bounds, &op, border_x, border_y](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();

            for (int g : groups)
            {
                const auto &group = image.groups[size_t(g)];
                const int   n     = group.num_channels;

                // the whole group is staged before any of it is written, so an op reading its neighbors
                // never finds one this pass has already replaced
                std::array<Array2Df, 4> staging;
                for (int c = 0; c < n; ++c) staging[size_t(c)] = Array2Df{extent};

                // reads anywhere in the channel, not merely the selection; only past the image itself
                // does the border mode decide what is there
                auto read = [&image, &group, n, border_x, border_y](int2 p)
                {
                    const Channel &first = image.channels[size_t(group.channels[0])];
                    const int      x     = wrap_coord(p.x - image.data_window.min.x, first.size().x, border_x);
                    const int      y     = wrap_coord(p.y - image.data_window.min.y, first.size().y, border_y);

                    // opaque where the group has no alpha; where the border mode says there is nothing,
                    // transparent black
                    if (x < 0 || y < 0)
                        return float4{0.f, 0.f, 0.f, n >= 4 ? 0.f : 1.f};

                    float4 c{0.f, 0.f, 0.f, 1.f};
                    for (int k = 0; k < n; ++k) c[k] = image.channels[size_t(group.channels[k])](x, y);
                    return c;
                };

                const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
                stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                                  [&](int y0, int y1, int, int)
                                  {
                                      for (int y = y0; y < y1; ++y)
                                          for (int x = 0; x < extent.x; ++x)
                                          {
                                              const float4 out = op(read, int2{bounds.min.x + x, bounds.min.y + y});
                                              for (int k = 0; k < n; ++k) staging[size_t(k)](x, y) = out[k];
                                          }
                                  });

                for (int c = 0; c < n; ++c)
                    image.channels[size_t(group.channels[c])].upload_tile(Box2i{offset, offset + extent},
                                                                          staging[size_t(c)].data());
            }
        },
        [&channels, &bounds, &name](const Image &image) -> UndoPtr
        { return std::make_unique<ChannelRectUndo>(image, channels, bounds, name); });
}

bool HDRViewApp::modify_structure(const ImagePtr &img, const string &name, const function<void(Image &)> &op)
{
    const bool applied = modify_image(img, name, op, [&name](const Image &image) -> UndoPtr
                                      { return std::make_unique<StructureUndo>(image, name); });
    if (!applied)
        return false;

    // the channel list changed wholesale, so the layers and groups built from its names are rebuilt
    img->rebuild_layers();
    update_visibility();

    // the view was framing an image of a different size
    fit_display_window();

    return true;
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

std::pair<std::vector<int>, Box2i> HDRViewApp::resolve_subject(const ConstImagePtr &img,
                                                               const EditSubject   &subject) const
{
    if (!img)
        return {std::vector<int>{}, Box2i{}};

    std::vector<int> channels = subject_channels(*img, subject);

    Box2i bounds = img->data_window;
    // an empty selection means "no selection", not "select nothing"
    if (subject.selection_only && m_roi.has_volume())
        bounds.intersect(m_roi);

    return {channels, bounds};
}

bool HDRViewApp::modify_pixels(const ImagePtr &img, const string &name, const EditSubject &subject,
                               const function<float(float, int2, int)> &op)
{
    if (!can_edit(img))
        return false;

    auto [channels, bounds] = resolve_subject(img, subject);
    if (channels.empty() || !bounds.has_volume())
        return false;

    return modify_image(
        img, name,
        [&channels, &bounds, &op](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();

            for (size_t slot = 0; slot < channels.size(); ++slot)
            {
                Channel &channel = image.channels[size_t(channels[slot])];

                // upload_tile() writes the samples and pushes just this rectangle to the GPU
                Array2Df  staging{extent};
                const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
                stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                                  [&](int y0, int y1, int, int)
                                  {
                                      for (int y = y0; y < y1; ++y)
                                          for (int x = 0; x < extent.x; ++x)
                                              staging(x, y) = op(channel(offset.x + x, offset.y + y),
                                                                 int2{bounds.min.x + x, bounds.min.y + y}, int(slot));
                                  });

                channel.upload_tile(Box2i{offset, offset + extent}, staging.data());
            }
        },
        [&channels, &bounds, &name](const Image &image) -> UndoPtr
        { return std::make_unique<ChannelRectUndo>(image, channels, bounds, name); });
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

void HDRViewApp::modify_channels_async(
    const ImagePtr &img, const string &name, const EditSubject &subject,
    const function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)> &filter)
{
    if (!can_edit(img))
        return;

    if (m_running_filter)
    {
        m_filter_queue.push_back([this, img, name, subject, filter]
                                 { modify_channels_async(img, name, subject, filter); });
        return;
    }

    auto [channels, bounds] = resolve_subject(img, subject);
    if (channels.empty() || !bounds.has_volume())
        return;

    auto running      = std::make_unique<RunningFilter>();
    running->image    = img;
    running->name     = name;
    running->channels = channels;
    running->bounds   = bounds;
    running->results.resize(channels.size());

    // the statistics tasks read the very samples the filter is about to
    for (auto &c : img->channels) c.cancel_stats();

    RunningFilter *raw = running.get();
    m_running_filter   = std::move(running);

    // filters every channel into raw->results; runs on a worker where there is one, see below
    auto do_the_work = [raw, filter]
    {
        const Box2i local{raw->bounds.min - raw->image->data_window.min,
                          raw->bounds.min - raw->image->data_window.min + raw->bounds.size()};

        const float share = 1.f / float(raw->channels.size());
        for (size_t i = 0; i < raw->channels.size() && !raw->progress.canceled(); ++i)
        {
            // a share of the same total, not a copy, so Cancel reaches the filter partway through a
            // channel and not only between channels
            raw->results[i] = filter(raw->image->channels[size_t(raw->channels[i])], local, int(i),
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

void HDRViewApp::modify_image_async(const ImagePtr &img, const string &name, int2 size,
                                    const function<Array2Df(const Array2Df &, AtomicProgress)> &op)
{
    if (!can_edit(img) || size.x <= 0 || size.y <= 0)
        return;

    if (m_running_filter)
    {
        m_filter_queue.push_back([this, img, name, size, op] { modify_image_async(img, name, size, op); });
        return;
    }

    auto running    = std::make_unique<RunningFilter>();
    running->image  = img;
    running->name   = name;
    running->bounds = img->data_window;
    running->channels.resize(img->channels.size());
    for (size_t i = 0; i < img->channels.size(); ++i) running->channels[i] = int(i);
    running->results.resize(img->channels.size());

    for (auto &c : img->channels) c.cancel_stats();

    RunningFilter *raw       = running.get();
    m_running_filter         = std::move(running);
    m_running_filter_resizes = true;
    m_running_filter_size    = size;

    auto do_the_work = [raw, op]
    {
        const float share = 1.f / float(raw->channels.size());
        for (size_t i = 0; i < raw->channels.size() && !raw->progress.canceled(); ++i)
            raw->results[i] = op(raw->image->channels[i], AtomicProgress{raw->progress, share});

        if (!raw->progress.canceled())
            raw->progress.set_done();

        raw->done.store(true);
    };

#if defined(__EMSCRIPTEN__)
    do_the_work();
    drain_running_filter();
#else
    dialog("Applying filter...").open = true;
    raw->worker                       = std::thread(
        [this, do_the_work]
        {
            do_the_work();
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
        m_running_filter_resizes = false;
        // cancel means the whole run, not just the image it had reached
        m_filter_queue.clear();
        return;
    }

    if (m_running_filter_resizes)
    {
        // the results are a different size than what they were computed from, so the channels are
        // replaced outright and the windows moved to match
        const int2 size          = m_running_filter_size;
        m_running_filter_resizes = false;
        auto &results            = running->results;

        modify_structure(running->image, running->name,
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
                         });
        start_next_filter();
        return;
    }

    const auto &channels = running->channels;
    const Box2i bounds   = running->bounds;
    auto       &results  = running->results;

    modify_image(
        running->image, running->name,
        [&channels, &bounds, &results](Image &image)
        {
            const int2 offset = bounds.min - image.data_window.min;
            const int2 extent = bounds.size();
            for (size_t i = 0; i < channels.size(); ++i)
                image.channels[size_t(channels[i])].upload_tile(Box2i{offset, offset + extent}, results[i].data());
        },
        [&channels, &bounds, &running](const Image &image) -> UndoPtr
        { return std::make_unique<ChannelRectUndo>(image, channels, bounds, running->name); });

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
