//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "image.h"
#include "imgui_ext.h"

#include <numeric>
#include <smallthreadpool.h>
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

    ImGui::BeginDisabled(!matters);
    for (int i = 0; i < EditSubject::Scope_COUNT; ++i)
        if (ImGui::RadioButton(edit_scope_name(i), m_edit_subject.scope == i))
            m_edit_subject.scope = EditSubject::Scope(i);
    ImGui::EndDisabled();
    if (!matters && img)
        ImGui::Tooltip("This image has a single channel group, so both choices cover the same channels.");

    ImGui::BeginDisabled(!m_roi.has_volume());
    ImGui::Checkbox("Selection only", &m_edit_subject.selection_only);
    ImGui::EndDisabled();
    if (!m_roi.has_volume())
        ImGui::Tooltip("Make a rectangular selection first.");
}

void HDRViewApp::draw_exposure_gamma_dialog(bool &open)
{
    // Applied on confirm rather than as the sliders move: an edit per frame of a drag would fill the
    // history with states nobody asked for, and each one would write every sample it covers.
    static float exposure = 0.f, offset = 0.f, gamma = 1.f;

    if (!ImGui::BeginModalDialog("Exposure/gamma...", open))
        return;

    ImGui::SliderFloat("Exposure", &exposure, -10.f, 10.f, "%.2f");
    ImGui::SliderFloat("Offset", &offset, -1.f, 1.f, "%.3f");
    ImGui::SliderFloat("Gamma", &gamma, MIN_GAMMA, 10.f, "%.3f");

    draw_edit_subject_selector();

    const auto result = ImGui::DialogButtons("Apply");
    if (result == ImGui::DialogResult::Confirm)
    {
        const float scale = std::pow(2.f, exposure);
        const float inv_g = 1.f / std::max(MIN_GAMMA, gamma);
        modify_pixels(current_image(), "Exposure/gamma", m_edit_subject,
                      [scale, offset_ = offset, inv_g](float v, int2, int)
                      {
                          const float x = scale * v + offset_;
                          // A negative sample is meaningful in an HDR image, and pow() of one is not, so
                          // the curve is mirrored through the origin instead of producing a NaN.
                          return x < 0.f ? -std::pow(-x, inv_g) : std::pow(x, inv_g);
                      });
        ImGui::CloseCurrentPopup();
    }
    else if (result == ImGui::DialogResult::Cancel)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void HDRViewApp::draw_brightness_contrast_dialog(bool &open)
{
    static float brightness = 0.f, contrast = 0.f;

    if (!ImGui::BeginModalDialog("Brightness/contrast...", open))
        return;

    ImGui::SliderFloat("Brightness", &brightness, -1.f, 1.f, "%.3f");
    ImGui::SliderFloat("Contrast", &contrast, -1.f, 1.f, "%.3f");

    draw_edit_subject_selector();

    const auto result = ImGui::DialogButtons("Apply");
    if (result == ImGui::DialogResult::Confirm)
    {
        // Contrast sets how steep the line through the midpoint is; brightness moves the midpoint. Taken
        // from the pre-2.0 control so that the two behave the way they used to.
        const float slope    = float(std::tan(lerp(0.0, M_PI_2, contrast / 2.0 + 0.5)));
        const float midpoint = (1.f - brightness) / 2.f;

        modify_pixels(current_image(), "Brightness/contrast", m_edit_subject,
                      [slope, midpoint](float v, int2, int) { return brightness_contrast_linear(v, slope, midpoint); });
        ImGui::CloseCurrentPopup();
    }
    else if (result == ImGui::DialogResult::Cancel)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void HDRViewApp::draw_fill_dialog(bool &open)
{
    static float4 color{0.f, 0.f, 0.f, 1.f};

    if (!ImGui::BeginModalDialog("Fill...", open))
        return;

    ImGui::ColorEdit4("Color", &color.x,
                      ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);

    draw_edit_subject_selector();

    const auto result = ImGui::DialogButtons("Fill");
    if (result == ImGui::DialogResult::Confirm)
    {
        // The one edit so far whose value depends on which channel it is writing: a group's channels
        // arrive in order, so the slot indexes the color. Beyond four -- an "all channels" subject on a
        // multi-layer image -- the components repeat rather than running off the end.
        const float4 c = color;
        modify_pixels(current_image(), "Fill", m_edit_subject, [c](float, int2, int slot) { return c[slot % 4]; });
        ImGui::CloseCurrentPopup();
    }
    else if (result == ImGui::DialogResult::Cancel)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
