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
/*!
    Cheap enough to build on the stack wherever one is needed, which is what keeps the app from having to
    hold one and keep it in step.

    Every method here supplies the current image and the current subject, so a command names only what it
    is doing -- which is the whole difference between the interface and the chokepoints behind it.
*/
struct AppEditContext final : EditContext
{
    HDRViewApp *app;

    explicit AppEditContext(HDRViewApp *a) : app(a) {}

    ImagePtr           image() const override { return app->current_image(); }
    const EditSubject &subject() const override { return app->edit_subject(); }
    int                target_group() const override { return app->target_group(); }
    Box2i              selection() const override { return app->roi(); }
    void               set_selection(const Box2i &box) override { app->set_selection(box); }
    float4             background_color() const override { return app->background_color(); }
    ConstImagePtr      clipboard() const override { return app->clipboard(); }
    void               set_clipboard(ImagePtr img) override { app->set_clipboard(std::move(img)); }

    bool modify_pixels(const string &name, const function<float(float, int2, int)> &op) override
    {
        return app->modify_pixels(app->current_image(), name, app->edit_subject(), op);
    }
    bool modify_colors(const string &name, const function<float4(const float4 &, int2)> &op,
                       const function<void(Image &)> &retag) override
    {
        return app->modify_colors(app->current_image(), name, app->edit_subject(), op, retag);
    }
    bool modify_neighborhood(const string &name, const function<float4(const function<float4(int2)> &, int2)> &op,
                             int border_x, int border_y) override
    {
        return app->modify_neighborhood(app->current_image(), name, app->edit_subject(), op, border_x, border_y);
    }
    bool modify_channels(const string &name, const function<Array2Df(const Array2Df &, const Box2i &)> &filter) override
    {
        return app->modify_channels(app->current_image(), name, app->edit_subject(), filter);
    }
    void modify_channels_async(
        const string &name, const function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)> &f) override
    {
        app->modify_channels_async(app->current_image(), name, app->edit_subject(), f);
    }
    void modify_image_async(const string &name, int2 size,
                            const function<Array2Df(const Array2Df &, AtomicProgress)> &op) override
    {
        app->modify_image_async(app->current_image(), name, size, op);
    }
    bool modify_structure(const string &name, const function<void(Image &)> &op) override
    {
        return app->modify_structure(app->current_image(), name, op);
    }
    bool modify_reversibly(const string &name, const function<void(Image &)> &forward,
                           const function<void(Image &)> &backward) override
    {
        return app->modify_image_reversibly(app->current_image(), name, forward, backward);
    }

    void add_image(ImagePtr img, const std::string &partname) override { app->add_image_beside_current(img, partname); }

    void draw_subject_selector() override { app->draw_edit_subject_selector(); }
};

} // namespace

int HDRViewApp::target_group() const
{
    if (m_target_group_override >= 0)
        return m_target_group_override;

    auto img = current_image();
    return img ? img->active_group_index(Target_Primary) : -1;
}

void HDRViewApp::invoke_action_on_group(const string &action_name, int group)
{
    // Restored however the action leaves, since pointing at a group must not move the selection -- and an
    // action that removes the group would otherwise leave the override naming one that is gone.
    const int previous      = m_target_group_override;
    m_target_group_override = group;
    action(action_name).callback();
    m_target_group_override = previous;
}

void HDRViewApp::invoke_edit_command(EditCommand &cmd)
{
    if (cmd.has_dialog())
    {
        dialog(cmd.info().names.front()).open = true;
        return;
    }

    AppEditContext ctx{this};
    cmd.apply(ctx);
}

bool HDRViewApp::edit_command_enabled(const EditCommand &cmd)
{
    AppEditContext ctx{this};
    if (cmd.info().needs_editable && !can_edit(current_image()))
        return false;
    return cmd.enabled(ctx);
}

void HDRViewApp::draw_edit_command_dialog(EditCommand &cmd, bool &open)
{
    const auto     info = cmd.info();
    AppEditContext ctx{this};

    // Before BeginModalDialog(), which consumes `open`: this is the frame the dialog was asked for, and
    // the one where a command reads its defaults off the image.
    if (open)
        cmd.on_open(ctx);

    if (ImGui::BeginModalDialog(info.names.front().c_str(), open, ImGui::DialogPosition::Center))
    {
        // A minimum width, established by an item of that width rather than by SetNextWindowSize(): these
        // dialogs are AlwaysAutoResize, which sizes the window from its contents every frame and ignores a
        // size set from outside. Content wider than this still widens the dialog.
        ImGui::Dummy(ImVec2(info.width_em * HelloImGui::EmSize(), 0.f));

        cmd.draw(ctx);

        if (info.has_subject)
            draw_edit_subject_selector();

        // Applied on confirm rather than as the controls move: an edit per frame of a drag would fill the
        // history with states nobody asked for, and each one would write every sample it covers.
        const auto result = ImGui::DialogButtons(info.confirm.c_str());
        if (result == ImGui::DialogResult::Confirm)
        {
            cmd.apply(ctx);
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
    // Statistics and histograms are cached against this; see Image::content_version.
    ++img->content_version;

    // Deliberately not finalize(): it does far more than rebuild the layer tree. It would premultiply a
    // straight-alpha image a second time, silently darkening it, and re-derive metadata that has not
    // changed. An edit that adds or removes channels needs the tree rebuilt and will have to ask for that
    // specifically; none of the edits so far touches the channel set.

    // Recomputes each group's visibility and the layer tree's per-node counts of visible and hidden
    // descendants, then rebinds the textures. Both the edit and the undo of it come through here, which is
    // what keeps them from invalidating different things -- a structural undo rebuilds the layer tree just
    // as the edit did, and the Images panel walks the result of both.
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

                // The filter sees the whole channel but produces only this rectangle, reading past it
                // just as far as its kernel reaches -- so a selection costs the selection, not the image.
                const Box2i    local    = Box2i{offset, offset + extent};
                const Array2Df filtered = filter(channel, local);

                channel.upload_tile(local, filtered.data());
            }
        },
        [&channels, &bounds, &name](const Image &image) -> UndoPtr
        { return std::make_unique<ChannelRectUndo>(image, channels, bounds, name); });
}

namespace
{

//! The color groups \p subject covers, and every channel of them.
/*!
    Only RGB and RGBA: everything else in an image -- depth, motion vectors, an ID -- is not color, and a
    color operation has no meaning for it, so it is left alone rather than run through one.
*/
std::pair<std::vector<int>, std::vector<int>> resolve_color_groups(const ConstImagePtr &img, const EditSubject &subject)
{
    std::vector<int> groups;
    if (subject.scope == EditSubject::Scope_AllChannels)
    {
        for (int g = 0; g < int(img->groups.size()); ++g) groups.push_back(g);
    }
    else if (int g = img->active_group_index(Target_Primary); img->is_valid_group(g))
        groups.push_back(g);

    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [&img](int g)
                                {
                                    const auto t = img->groups[size_t(g)].type;
                                    return t != ChannelGroup::RGB_Channels && t != ChannelGroup::RGBA_Channels;
                                }),
                 groups.end());

    // Every channel of every covered group, which is the set an undo entry has to hold.
    std::vector<int> channels;
    for (int g : groups)
    {
        const auto &group = img->groups[size_t(g)];
        for (int c = 0; c < group.num_channels; ++c) channels.push_back(group.channels[c]);
    }

    return {groups, channels};
}

} // namespace

bool HDRViewApp::modify_colors(const ImagePtr &img, const string &name, const EditSubject &subject,
                               const function<float4(const float4 &, int2)> &op, const function<void(Image &)> &retag)
{
    if (!can_edit(img))
        return false;

    auto [groups, channels] = resolve_color_groups(img, subject);
    if (groups.empty())
        return false;

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

                // Read, transform, and write as a set: the whole point is that the op sees the components
                // together, so all of them are staged before any is written back.
                std::array<Array2Df, 4> staging;
                for (int c = 0; c < n; ++c) staging[size_t(c)] = Array2Df{extent};

                const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
                stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                                  [&](int y0, int y1, int, int)
                                  {
                                      for (int y = y0; y < y1; ++y)
                                          for (int x = 0; x < extent.x; ++x)
                                          {
                                              // Opaque where the group has no alpha, so an op may read the fourth
                                              // component without asking which kind of group it was handed.
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

            // The samples and what they mean changed together, so they are taken back together.
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

    auto [groups, channels] = resolve_color_groups(img, subject);
    if (groups.empty())
        return false;

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

                // The whole group is staged before any of it is written, so the reader below always sees
                // the image as it was: an op reading its neighbors must not find one it has replaced.
                std::array<Array2Df, 4> staging;
                for (int c = 0; c < n; ++c) staging[size_t(c)] = Array2Df{extent};

                // Reads anywhere in the *channel*, not merely the selection: the samples just outside a
                // selection are real ones and belong in the answer, and only past the image itself does
                // the border mode decide what is there.
                auto read = [&image, &group, n, border_x, border_y](int2 p)
                {
                    const Channel &first = image.channels[size_t(group.channels[0])];
                    const int      x     = wrap_coord(p.x - image.data_window.min.x, first.size().x, border_x);
                    const int      y     = wrap_coord(p.y - image.data_window.min.y, first.size().y, border_y);

                    // Opaque where the group has no alpha, and where the border mode says there is nothing
                    // the color is black -- but transparent black, since that is what "nothing" is.
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

    // The channel list changed wholesale, so the layers and groups built from its names have to be built
    // again -- but only those. finalize() would premultiply a straight-alpha image a second time.
    img->rebuild_layers();
    update_visibility();

    // The view was framing an image of a different size; leaving the zoom and pan alone would put the new
    // one partly or entirely off screen.
    fit_display_window();

    return true;
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

bool HDRViewApp::scope_matters(const ConstImagePtr &img)
{
    // With one group, "the group the viewport is showing" and "every channel" are the same set, so there
    // is nothing for the user to decide.
    return img && img->groups.size() > 1;
}

std::pair<std::vector<int>, Box2i> HDRViewApp::resolve_subject(const ConstImagePtr &img,
                                                               const EditSubject   &subject) const
{
    std::vector<int> channels;
    if (!img)
        return {channels, Box2i{}};

    if (subject.scope == EditSubject::Scope_AllChannels)
    {
        channels.resize(img->channels.size());
        std::iota(channels.begin(), channels.end(), 0);
    }
    else if (int g = img->active_group_index(Target_Primary); img->is_valid_group(g))
    {
        const auto &group = img->groups[size_t(g)];
        for (int c = 0; c < group.num_channels; ++c) channels.push_back(group.channels[c]);
    }

    Box2i bounds = img->data_window;
    // An empty selection means "no selection", not "select nothing" -- leaving the box on should not make
    // edits silently stop working once it is cleared.
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

                // Computed into its own buffer and then handed to upload_tile(), which both writes the
                // samples and pushes just this rectangle to the GPU -- the same path a renderer streams
                // through, rather than a full re-upload for an edit that may cover a few pixels.
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

void HDRViewApp::draw_edit_subject_selector()
{
    // The same setting the Edit menu shows, so a dialog and the menu can never disagree about what the
    // next edit covers.
    auto       img     = current_image();
    const bool matters = scope_matters(img);

    ImGui::SeparatorText("Apply to");

    // Always settable, even where it changes nothing today: the setting is remembered across images, and
    // one that greys out on whichever image happens to be open cannot be set for the next one.
    for (int i = 0; i < EditSubject::Scope_COUNT; ++i)
        if (ImGui::RadioButton(edit_scope_name(i), m_edit_subject.scope == i))
            m_edit_subject.scope = EditSubject::Scope(i);
    if (!matters && img)
        ImGui::Tooltip("This image has a single channel group, so both choices cover the same channels.");

    ImGui::Checkbox("Selection only", &m_edit_subject.selection_only);
    if (!m_roi.has_volume())
        ImGui::Tooltip("There is no selection, so edits cover the whole image.");
}

void HDRViewApp::modify_channels_async(
    const ImagePtr &img, const string &name, const EditSubject &subject,
    const function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)> &filter)
{
    if (!can_edit(img) || m_running_filter)
        return;

    auto [channels, bounds] = resolve_subject(img, subject);
    if (channels.empty() || !bounds.has_volume())
        return;

    auto running      = std::make_unique<RunningFilter>();
    running->image    = img;
    running->name     = name;
    running->channels = channels;
    running->bounds   = bounds;
    running->results.resize(channels.size());

    // The statistics tasks read the very samples the filter is about to, and would otherwise be running
    // alongside it for its whole duration.
    for (auto &c : img->channels) c.cancel_stats();

    RunningFilter *raw = running.get();
    m_running_filter   = std::move(running);

    // Filters every channel into raw->results. Runs on a worker where there is one; see below.
    auto do_the_work = [raw, filter]
    {
        const Box2i local{raw->bounds.min - raw->image->data_window.min,
                          raw->bounds.min - raw->image->data_window.min + raw->bounds.size()};

        const float share = 1.f / float(raw->channels.size());
        for (size_t i = 0; i < raw->channels.size() && !raw->progress.canceled(); ++i)
        {
            // A share of the same total rather than a copy: the filter's own reporting reaches the bar, and
            // -- what a copy got wrong -- Cancel reaches the filter partway through a channel instead of
            // only between channels.
            raw->results[i] = filter(raw->image->channels[size_t(raw->channels[i])], local, int(i),
                                     AtomicProgress{raw->progress, share});
        }

        if (!raw->progress.canceled())
            raw->progress.set_done();

        raw->done.store(true);
    };

#if defined(__EMSCRIPTEN__)
    // The web build is built without pthreads, so there is no worker to run this on and nothing that could
    // draw a progress bar while it ran. It happens inline instead: the page stops responding for the
    // duration, as it would for any other synchronous work, and there is nothing to cancel.
    //
    // Making this cooperative would mean filters that can stop and resume rather than ones that return a
    // finished array, which is a different shape of filter than any of these are.
    do_the_work();
    drain_running_filter();
#else
    dialog("Applying filter...").open = true;

    // Reading the channels is safe for as long as this runs: the chokepoint refuses a second edit while a
    // filter is in flight, and the image cannot be closed without cancelling it first.
    // Kept rather than detached, so that closing the window can stop it and wait; see ~RunningFilter().
    raw->worker = std::thread(
        [this, do_the_work]
        {
            do_the_work();
            // Nothing on screen changes until the frame loop notices, and it may be idle waiting on window
            // events rather than spinning.
            wake_event_loop();
        });
#endif
}

void HDRViewApp::modify_image_async(const ImagePtr &img, const string &name, int2 size,
                                    const function<Array2Df(const Array2Df &, AtomicProgress)> &op)
{
    if (!can_edit(img) || m_running_filter || size.x <= 0 || size.y <= 0)
        return;

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

    // A partial result is not a shorter filter, it is a wrong one, so an abandoned run changes nothing.
    if (running->progress.canceled())
    {
        spdlog::debug("Filter '{}' was canceled.", running->name);
        m_running_filter_resizes = false;
        return;
    }

    if (m_running_filter_resizes)
    {
        // The results are a different size than what they were computed from, so there is no rectangle to
        // write back: the channels are replaced outright and the windows moved to match.
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

        // As above: the window is sized from its contents, so a bar told to fill the available width has
        // none to fill and the dialog shrinks to the Cancel button beneath it.
        ImGui::Dummy(ImVec2(24.f * HelloImGui::EmSize(), 0.f));

        ImGui::TextUnformatted(m_running_filter->name.c_str());
        ImGui::ProgressBar(m_running_filter->progress.progress(), ImVec2(-FLT_MIN, 0.f));

        // Only asks the filter to stop; the work unwinds on its own thread and drain_running_filter()
        // throws the partial result away when it does.
        if (ImGui::Button("Cancel") || ImGui::Shortcut(ImGuiKey_Escape))
            m_running_filter->progress.cancel();

        if (m_running_filter->done.load())
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
