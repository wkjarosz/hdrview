//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "edit/filters.h"
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
        ImGui::Tooltip("There is no selection, so edits cover the whole image.");
}

void HDRViewApp::draw_exposure_gamma_dialog(bool &open)
{
    // Applied on confirm rather than as the sliders move: an edit per frame of a drag would fill the
    // history with states nobody asked for, and each one would write every sample it covers.
    static float exposure = 0.f, offset = 0.f, gamma = 1.f;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Exposure/gamma...", open, ImGui::DialogPosition::Center))
    {
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
}

void HDRViewApp::draw_brightness_contrast_dialog(bool &open)
{
    static float brightness = 0.f, contrast = 0.f;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Brightness/contrast...", open, ImGui::DialogPosition::Center))
    {
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

            modify_pixels(current_image(), "Brightness/contrast", m_edit_subject, [slope, midpoint](float v, int2, int)
                          { return brightness_contrast_linear(v, slope, midpoint); });
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_fill_dialog(bool &open)
{
    static float4 color{0.f, 0.f, 0.f, 1.f};

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Fill...", open, ImGui::DialogPosition::Center))
    {
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
}

void HDRViewApp::draw_canvas_size_dialog(bool &open)
{
    static int2                width_height{0, 0};
    static Image::CanvasAnchor anchor = Image::Anchor_MiddleCenter;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Canvas size...", open, ImGui::DialogPosition::Center))
    {
        auto img = current_image();
        if (!img)
        {
            // Whatever it was about is gone; close rather than draw against nothing.
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        // Opens showing what the image currently is, so the dialog starts as a no-op rather than with whatever
        // was typed into it last time against a different image.
        if (width_height.x <= 0 || width_height.y <= 0)
            width_height = img->size();

        ImGui::InputInt2("Width, height", &width_height.x);
        width_height = la::max(width_height, int2{1});

        ImGui::SeparatorText("Anchor");
        // Which edges absorb the difference. Laid out as the 3x3 it means.
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const int i = row * 3 + col;
                if (col)
                    ImGui::SameLine();
                if (ImGui::RadioButton(fmt::format("##anchor{}", i).c_str(), int(anchor) == i))
                    anchor = Image::CanvasAnchor(i);
            }
        }

        const auto result = ImGui::DialogButtons("Resize");
        if (result == ImGui::DialogResult::Confirm)
        {
            const int2 size = width_height;
            const auto a    = anchor;
            modify_structure(current_image(), "Canvas size", [size, a](Image &i) { i.resize_canvas(size, a); });
            width_height = int2{0}; // so the next open reads the new size
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
        {
            width_height = int2{0};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_image_size_dialog(bool &open)
{
    static int2 width_height{0, 0};
    static bool keep_aspect = true;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Image size...", open, ImGui::DialogPosition::Center))
    {
        auto img = current_image();
        if (!img)
        {
            // Whatever it was about is gone; close rather than draw against nothing.
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const int2 current = img->size();
        if (width_height.x <= 0 || width_height.y <= 0)
            width_height = current;

        const int2 before = width_height;
        ImGui::InputInt2("Width, height", &width_height.x);
        width_height = la::max(width_height, int2{1});

        ImGui::Checkbox("Keep aspect ratio", &keep_aspect);
        if (keep_aspect && current.x > 0 && current.y > 0)
        {
            // Follow whichever the user just changed, so typing into either field drives the other.
            if (width_height.x != before.x)
                width_height.y = std::max(1, int(std::lround(double(width_height.x) * current.y / current.x)));
            else if (width_height.y != before.y)
                width_height.x = std::max(1, int(std::lround(double(width_height.y) * current.x / current.y)));
        }

        ImGui::TextFmt("From {}x{}", current.x, current.y);

        const auto result = ImGui::DialogButtons("Resize");
        if (result == ImGui::DialogResult::Confirm)
        {
            const int2 size = width_height;
            modify_structure(current_image(), "Image size", [size](Image &i) { i.resample(size); });
            width_height = int2{0};
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
        {
            width_height = int2{0};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_blur_dialog(bool &open)
{
    static int   kind         = 0; // 0 = Gaussian, 1 = box
    static float sigma        = 2.f;
    static float sigma_y      = 2.f;
    static int   half_width   = 2;
    static int   half_width_y = 2;
    static bool  link_axes    = true;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Blur...", open, ImGui::DialogPosition::Center))
    {
        ImGui::RadioButton("Gaussian", &kind, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Box", &kind, 1);

        if (kind == 0)
        {
            ImGui::SliderFloat("Sigma", &sigma, 0.f, 64.f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Same in both directions", &link_axes);
            if (!link_axes)
                ImGui::SliderFloat("Sigma (vertical)", &sigma_y, 0.f, 64.f, "%.2f", ImGuiSliderFlags_Logarithmic);
        }
        else
        {
            ImGui::SliderInt("Half width", &half_width, 0, 64);
            ImGui::Checkbox("Same in both directions", &link_axes);
            if (!link_axes)
                ImGui::SliderInt("Half width (vertical)", &half_width_y, 0, 64);
        }

        draw_edit_subject_selector();

        const auto result = ImGui::DialogButtons("Apply");
        if (result == ImGui::DialogResult::Confirm)
        {
            if (kind == 0)
            {
                const float sx = sigma, sy = link_axes ? sigma : sigma_y;
                modify_channels(current_image(), "Gaussian blur", m_edit_subject,
                                [sx, sy](const Array2Df &src, const Box2i &r)
                                { return gaussian_blurred(src, r, sx, sy); });
            }
            else
            {
                const int hx = half_width, hy = link_axes ? half_width : half_width_y;
                modify_channels(current_image(), "Box blur", m_edit_subject,
                                [hx, hy](const Array2Df &src, const Box2i &r) { return box_blurred(src, r, hx, hy); });
            }
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_unsharp_mask_dialog(bool &open)
{
    static float sigma = 2.f, amount = 1.f;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Unsharp mask...", open, ImGui::DialogPosition::Center))
    {
        ImGui::SliderFloat("Radius", &sigma, 0.1f, 32.f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Amount", &amount, 0.f, 5.f, "%.2f");

        draw_edit_subject_selector();

        const auto result = ImGui::DialogButtons("Apply");
        if (result == ImGui::DialogResult::Confirm)
        {
            const float s = sigma, a = amount;
            modify_channels(current_image(), "Unsharp mask", m_edit_subject,
                            [s, a](const Array2Df &src, const Box2i &r) { return unsharp_masked(src, r, s, a); });
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
