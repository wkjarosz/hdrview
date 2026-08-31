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

#include <numeric>
#include <smallthreadpool.h>
#include <spdlog/spdlog.h>
#include <thread>

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

    // What the color's alpha is taken to mean, which is genuinely two different operations.
    enum Mode : int
    {
        Mode_Blend = 0, //!< Coverage: lay the color over what is there
        Mode_Replace    //!< Write it outright, alpha channel included
    };
    static int mode = Mode_Blend;

    ImGui::SetNextWindowSize(ImVec2(24.f * HelloImGui::EmSize(), 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Fill...", open, ImGui::DialogPosition::Center))
    {
        ImGui::ColorEdit4("Color", &color.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);

        ImGui::RadioButton("Blend over", &mode, Mode_Blend);
        ImGui::Tooltip("The color's alpha is how much of it to lay over what is already there. At 1 it "
                       "covers completely; at 0 it changes nothing. Works on any image, with or without an "
                       "alpha channel of its own.");
        ImGui::SameLine();
        ImGui::RadioButton("Replace", &mode, Mode_Replace);
        ImGui::Tooltip("Write the color as given, including its alpha, so the filled region takes on that "
                       "transparency. Needs the image to have an alpha channel for the alpha to land "
                       "anywhere.");

        if (mode == Mode_Replace)
            if (auto img = current_image(); img && img->is_valid_group(img->active_group_index(Target_Primary)))
                if (!group_has_alpha(img->groups[size_t(img->active_group_index(Target_Primary))].type))
                    ImGui::TextUnformatted("This channel group has no alpha, so the color's alpha has\n"
                                           "nowhere to go. Blend over instead to use it as coverage.");

        draw_edit_subject_selector();

        const auto result = ImGui::DialogButtons("Fill");
        if (result == ImGui::DialogResult::Confirm)
        {
            const float4 c = color;

            // Whether the samples in memory are premultiplied, which finalize() makes them whenever alpha
            // means transparency. Both modes have to match that or the result is out by a factor of alpha.
            bool premultiplied = false;
            int  alpha_slot    = -1;
            if (auto img = current_image(); img && img->is_valid_group(img->active_group_index(Target_Primary)))
            {
                const auto &group = img->groups[size_t(img->active_group_index(Target_Primary))];
                if (img->alpha_type != AlphaType_None && group_has_alpha(group.type))
                {
                    premultiplied = true;
                    alpha_slot    = group.num_channels - 1;
                }
            }

            if (mode == Mode_Replace)
            {
                // Premultiplied storage wants the color scaled by its own alpha; the alpha channel itself
                // is stored as given.
                float4 v = c;
                if (premultiplied)
                    v = float4{c.x * c.w, c.y * c.w, c.z * c.w, c.w};

                modify_pixels(current_image(), "Fill", m_edit_subject,
                              [v](float, int2, int slot) { return v[slot % 4]; });
            }
            else
            {
                // Source-over. With premultiplied storage the color contributes a*c and what was there
                // keeps (1-a) of itself, which is the same expression for colour and alpha alike -- the
                // alpha channel's own "color" being 1.
                const float a = c.w;
                modify_pixels(current_image(), "Fill", m_edit_subject,
                              [c, a, alpha_slot](float old, int2, int slot)
                              {
                                  const float src = (slot == alpha_slot) ? 1.f : c[slot % 4];
                                  return a * src + (1.f - a) * old;
                              });
            }
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

namespace
{

//! Units a size can be given in.
enum SizeUnits : int
{
    Units_Pixels = 0,
    Units_Percent
};

/*!
    Width and height side by side, with the chain link that ties them.

    \p size is always in pixels; the fields convert. Editing one side with the link closed drives the other
    from \p original's ratio rather than from the current values, so a run of edits cannot drift away from
    the ratio a rounding at a time.
*/
void size_fields(int2 *size, int *units, bool *locked, int2 original)
{
    const float field  = 7.f * HelloImGui::EmSize();
    const int2  before = *size;

    ImGui::SetNextItemWidth(9.f * HelloImGui::EmSize());
    ImGui::Combo("Units", units, "Pixels\0Percent\0");
    ImGui::Tooltip("Drag either field to sweep the size; ctrl-click one to type an exact value.");

    if (*units == Units_Percent)
    {
        float2 pct{100.f * float(size->x) / float(std::max(1, original.x)),
                   100.f * float(size->y) / float(std::max(1, original.y))};

        ImGui::SetNextItemWidth(field);
        ImGui::DragFloat("##width", &pct.x, 0.5f, 1.f, 1000.f, "%.1f %%");
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted("\xc3\x97"); // multiplication sign, as a size is written
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(field);
        ImGui::DragFloat("##height", &pct.y, 0.5f, 1.f, 1000.f, "%.1f %%");

        size->x = std::max(1, int(std::lround(double(pct.x) * 0.01 * double(original.x))));
        size->y = std::max(1, int(std::lround(double(pct.y) * 0.01 * double(original.y))));
    }
    else
    {
        ImGui::SetNextItemWidth(field);
        ImGui::DragInt("##width", &size->x, 1.f, 1, 65536, "%d px");
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted("\xc3\x97");
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(field);
        ImGui::DragInt("##height", &size->y, 1.f, 1, 65536, "%d px");

        *size = la::max(*size, int2{1});
    }

    // The link sits to the right of the pair it ties together.
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::Button(*locked ? ICON_MY_LINK : ICON_MY_UNLINK))
        *locked = !*locked;
    ImGui::Tooltip(*locked ? "Width and height are tied to the original aspect ratio. Click to unlink."
                           : "Width and height are set independently. Click to link.");

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted("Width, height");

    // Follow whichever side was just edited, from the original ratio rather than the current one.
    if (*locked && original.x > 0 && original.y > 0)
    {
        if (size->x != before.x)
            size->y = std::max(1, int(std::lround(double(size->x) * double(original.y) / double(original.x))));
        else if (size->y != before.y)
            size->x = std::max(1, int(std::lround(double(size->y) * double(original.x) / double(original.y))));
    }
}

} // namespace

void HDRViewApp::draw_canvas_size_dialog(bool &open)
{
    static int2                size{0, 0};
    static int                 units    = Units_Pixels;
    static bool                locked   = false;
    static bool                relative = false;
    static Image::CanvasAnchor anchor   = Image::Anchor_MiddleCenter;

    ImGui::SetNextWindowSize(ImVec2(30.f * HelloImGui::EmSize(), 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Canvas size...", open, ImGui::DialogPosition::Center))
    {
        auto img = current_image();
        if (!img)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const int2 original = img->size();
        if (size.x <= 0 || size.y <= 0)
            size = original;

        ImGui::TextFmt("Current: {} x {} pixels", original.x, original.y);
        ImGui::Separator();

        size_fields(&size, &units, &locked, original);

        ImGui::Checkbox("Relative", &relative);
        ImGui::Tooltip("Add the amounts above to the current size instead of replacing it. Negative "
                       "values trim.");

        const int2 target = relative ? la::max(original + size, int2{1}) : size;
        if (relative)
            ImGui::TextFmt("Result: {} x {} pixels", target.x, target.y);

        ImGui::SeparatorText("Anchor");
        ImGui::TextUnformatted("Where the existing pixels sit in the new canvas.");
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const int i = row * 3 + col;
                if (col)
                    ImGui::SameLine();
                if (ImGui::RadioButton(fmt::format("##anchor{}", i).c_str(), int(anchor) == i))
                    anchor = Image::CanvasAnchor(i);
            }

        const auto result = ImGui::DialogButtons("Resize");
        if (result == ImGui::DialogResult::Confirm)
        {
            const int2 out = target;
            const auto a   = anchor;
            modify_structure(current_image(), "Canvas size", [out, a](Image &i) { i.resize_canvas(out, a); });
            size = int2{0};
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
        {
            size = int2{0};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_image_size_dialog(bool &open)
{
    static int2 size{0, 0};
    static int  units  = Units_Pixels;
    static bool locked = true;

    ImGui::SetNextWindowSize(ImVec2(30.f * HelloImGui::EmSize(), 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Image size...", open, ImGui::DialogPosition::Center))
    {
        auto img = current_image();
        if (!img)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const int2 original = img->size();
        if (size.x <= 0 || size.y <= 0)
            size = original;

        ImGui::TextFmt("Current: {} x {} pixels", original.x, original.y);
        ImGui::Separator();

        size_fields(&size, &units, &locked, original);

        // Which way the resampling will go, since the two directions do different things: shrinking
        // averages over the samples each output covers, growing interpolates between them.
        if (size.x < original.x || size.y < original.y)
            ImGui::TextUnformatted("Reducing: samples are averaged.");
        else if (size.x > original.x || size.y > original.y)
            ImGui::TextUnformatted("Enlarging: samples are interpolated.");

        const auto result = ImGui::DialogButtons("Resize");
        if (result == ImGui::DialogResult::Confirm)
        {
            const int2 out = size;
            modify_structure(current_image(), "Image size", [out](Image &i) { i.resample(out); });
            size = int2{0};
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
        {
            size = int2{0};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_blur_dialog(bool &open)
{
    // What kernel is wanted, which decides what the controls below mean.
    enum Kind : int
    {
        Kind_Gaussian = 0, //!< The real thing, at a cost that grows with sigma
        Kind_FastGaussian, //!< Repeated boxes converging on it, at a cost independent of sigma
        Kind_Box           //!< Boxes as an effect in their own right
    };
    static int   kind         = Kind_Gaussian;
    static float sigma        = 2.f;
    static float sigma_y      = 2.f;
    static int   half_width   = 2;
    static int   half_width_y = 2;
    static int   iterations   = 6;
    static bool  link_axes    = true;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Blur...", open, ImGui::DialogPosition::Center))
    {
        // Addressed by the enum rather than by literal, which is how "Box" came to select the fast
        // Gaussian sitting between them.
        ImGui::RadioButton("Gaussian", &kind, Kind_Gaussian);
        ImGui::SameLine();
        ImGui::RadioButton("Fast Gaussian", &kind, Kind_FastGaussian);
        ImGui::SameLine();
        ImGui::RadioButton("Box", &kind, Kind_Box);

        if (kind == Kind_Box)
        {
            ImGui::SliderInt("Half width", &half_width, 0, 64);
            ImGui::Checkbox("Same in both directions", &link_axes);
            if (!link_axes)
                ImGui::SliderInt("Half width (vertical)", &half_width_y, 0, 64);

            // Repeating widens the result here, which is the point: this is the box blur as an effect, and
            // n passes of a stated width is the thing being asked for.
            ImGui::SliderInt("Passes", &iterations, 1, 16);
            ImGui::Tooltip("Each pass widens the blur. For a Gaussian of a given width, use Fast Gaussian.");
        }
        else
        {
            ImGui::SliderFloat("Sigma", &sigma, 0.f, 64.f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Same in both directions", &link_axes);
            if (!link_axes)
                ImGui::SliderFloat("Sigma (vertical)", &sigma_y, 0.f, 64.f, "%.2f", ImGuiSliderFlags_Logarithmic);

            if (kind == Kind_FastGaussian)
            {
                // Accuracy alone: the box width is solved for from sigma and the count, so the result stays
                // the width asked for however many passes it takes to get there.
                ImGui::SliderInt("Quality", &iterations, 1, 12);
                ImGui::Tooltip("Box blur passes. More is closer to a true Gaussian and costs proportionally "
                               "more; the amount of blur does not change. Three is already hard to tell "
                               "apart, and one is a plain box.");
            }
        }

        draw_edit_subject_selector();

        const auto result = ImGui::DialogButtons("Apply");
        if (result == ImGui::DialogResult::Confirm)
        {
            if (kind == Kind_Box)
            {
                const int hx = half_width, hy = link_axes ? half_width : half_width_y, n = iterations;
                modify_channels_async(current_image(), "Box blur", m_edit_subject,
                                      [hx, hy, n](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                      { return box_blurred(src, r, hx, hy, n, p); });
            }
            else if (kind == Kind_FastGaussian)
            {
                const float sx = sigma, sy = link_axes ? sigma : sigma_y;
                const int   n = iterations;
                modify_channels_async(current_image(), "Gaussian blur", m_edit_subject,
                                      [sx, sy, n](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                      { return fast_gaussian_blurred(src, r, sx, sy, n, p); });
            }
            else
            {
                const float sx = sigma, sy = link_axes ? sigma : sigma_y;
                modify_channels_async(current_image(), "Gaussian blur", m_edit_subject,
                                      [sx, sy](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                      { return gaussian_blurred(src, r, sx, sy, p); });
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
            modify_channels_async(current_image(), "Unsharp mask", m_edit_subject,
                                  [s, a](const Array2Df &src, const Box2i &r, AtomicProgress p)
                                  { return unsharp_masked(src, r, s, a, p); });
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void HDRViewApp::modify_channels_async(
    const ImagePtr &img, const string &name, const EditSubject &subject,
    const function<Array2Df(const Array2Df &, const Box2i &, AtomicProgress)> &filter)
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
            raw->results[i] =
                filter(raw->image->channels[size_t(raw->channels[i])], local, AtomicProgress{raw->progress, share});
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
    std::thread(
        [this, do_the_work]
        {
            do_the_work();
            // Nothing on screen changes until the frame loop notices, and it may be idle waiting on window
            // events rather than spinning.
            wake_event_loop();
        })
        .detach();
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
    std::thread(
        [this, do_the_work]
        {
            do_the_work();
            wake_event_loop();
        })
        .detach();
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

    ImGui::SetNextWindowSize(ImVec2(32.f * HelloImGui::EmSize(), 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Applying filter...", open, ImGui::DialogPosition::Center))
    {
        if (!m_running_filter)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

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

void HDRViewApp::draw_median_dialog(bool &open)
{
    static float radius = 2.f;

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Median filter...", open, ImGui::DialogPosition::Center))
    {
        ImGui::SliderFloat("Radius", &radius, 0.f, 32.f, "%.1f");

        static bool disc = true;
        ImGui::Checkbox("Circular window", &disc);
        ImGui::Tooltip("A square window reaches root-two farther at its corners than along its axes, which "
                       "leaves a faint squareness in what it removes.");
        ImGui::Tooltip("Removes lone outliers -- fireflies in a render -- without the smearing a blur "
                       "would cause. Costs the area of the disc per sample, so a large radius is slow.");

        draw_edit_subject_selector();

        const auto result = ImGui::DialogButtons("Apply");
        if (result == ImGui::DialogResult::Confirm)
        {
            const float r = radius;
            const bool  d = disc;
            modify_channels_async(current_image(), "Median filter", m_edit_subject,
                                  [r, d](const Array2Df &src, const Box2i &region, AtomicProgress p)
                                  { return median_filtered(src, region, r, d, p); });
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_remap_dialog(bool &open)
{
    static int  src_mapping = EnvMapping_LatLong;
    static int  dst_mapping = EnvMapping_Angular;
    static int2 size{0, 0};
    static int  supersample = 8;

    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Remap envmap...", open, ImGui::DialogPosition::Center))
    {
        auto img = current_image();
        if (!img)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        if (size.x <= 0 || size.y <= 0)
            size = img->size();

        auto mapping_combo = [](const char *label, int *value)
        {
            if (ImGui::BeginCombo(label, envmapping_name(*value)))
            {
                for (int i = 0; i < EnvMapping_COUNT; ++i)
                    if (ImGui::Selectable(envmapping_name(i), *value == i))
                        *value = i;
                ImGui::EndCombo();
            }
        };

        mapping_combo("Source", &src_mapping);
        mapping_combo("Target", &dst_mapping);

        ImGui::DragInt2("Width, height", &size.x, 1.f, 1, 65536, "%d px");
        size = la::max(size, int2{1});

        static int sampling = EnvMapSampling_EWA;
        ImGui::RadioButton("EWA", &sampling, EnvMapSampling_EWA);
        ImGui::Tooltip("Reads a mip pyramid through an ellipse shaped by the area each output pixel covers "
                       "in the source. That area is far wider than it is tall near a lat-long's poles, "
                       "which is the case a mip level on its own cannot represent. Costs the same whatever "
                       "the scale.");
        ImGui::SameLine();
        ImGui::RadioButton("Point", &sampling, EnvMapSampling_Point);
        ImGui::Tooltip("Averages a grid of samples inside each output pixel. Exact when enlarging, but a "
                       "reduction of more than the sample count still aliases.");

        if (sampling == EnvMapSampling_EWA)
        {
            ImGui::SliderInt("Taps", &supersample, 1, 32);
            ImGui::Tooltip("Probes strung along the long axis of the footprint. The mip level covers the "
                           "short axis, so this is what keeps the long one sharp -- too few and it aliases, "
                           "since the level has to rise to cover what the probes cannot walk.");
        }
        else
        {
            ImGui::SliderInt("Samples per axis", &supersample, 1, 8);
            ImGui::Tooltip("Averaged within each output pixel, so the cost is its square.");
        }

        const auto result = ImGui::DialogButtons("Remap");
        if (result == ImGui::DialogResult::Confirm)
        {
            const auto s = EnvMapping(src_mapping), d = EnvMapping(dst_mapping);
            const int2 out_size = size;
            const int  ss       = supersample;
            const auto mode     = EnvMapSampling(sampling);
            modify_image_async(current_image(), "Remap envmap", out_size,
                               [s, d, out_size, ss, mode](const Array2Df &src, AtomicProgress p)
                               { return remapped_envmap(src, out_size, d, s, mode, ss, p); });
            size = int2{0};
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
        {
            size = int2{0};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_irradiance_dialog(bool &open)
{
    static int  mapping = EnvMapping_LatLong;
    static int2 size{64, 32};

    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Irradiance envmap...", open, ImGui::DialogPosition::Center))
    {
        if (ImGui::BeginCombo("Mapping", envmapping_name(mapping)))
        {
            for (int i = 0; i < EnvMapping_COUNT; ++i)
                if (ImGui::Selectable(envmapping_name(i), mapping == i))
                    mapping = i;
            ImGui::EndCombo();
        }

        ImGui::DragInt2("Width, height", &size.x, 1.f, 1, 8192, "%d px");
        size = la::max(size, int2{1});
        ImGui::Tooltip("Every output direction integrates over every input one, so this costs the two "
                       "resolutions multiplied together. The result is smooth enough that a small output "
                       "loses nothing.");

        if (auto img = current_image())
        {
            const double ops = double(size.x) * double(size.y) * double(img->size().x) * double(img->size().y);
            if (ops > 2e9)
                ImGui::TextUnformatted("This will take a while at these sizes.");
        }

        const auto result = ImGui::DialogButtons("Convolve");
        if (result == ImGui::DialogResult::Confirm)
        {
            const auto m        = EnvMapping(mapping);
            const int2 out_size = size;
            modify_image_async(current_image(), "Irradiance envmap", out_size,
                               [m, out_size](const Array2Df &src, AtomicProgress p)
                               { return irradiance_envmap(src, out_size, m, p); });
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_zap_gremlins_dialog(bool &open)
{
    // The two 1.8 offered: take what the neighbours say, or write something chosen. The first is almost
    // always what is wanted; the second is there for when a run of them has no good neighbour to ask.
    enum Mode : int
    {
        Mode_Median = 0,
        Mode_Value
    };
    static int    mode = Mode_Median;
    static float4 value{0.f, 0.f, 0.f, 1.f};

    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginModalDialog("Zap gremlins...", open, ImGui::DialogPosition::Center))
    {
        ImGui::TextWrapped("A NaN or an infinity is not a measurement. One of either makes the minimum, the "
                           "maximum and the average of the whole channel meaningless, and it survives every "
                           "filter it passes through, so it is worth removing before anything else is done.");
        ImGui::Spacing();

        if (auto img = current_image())
            if (auto *stats = img->channels[img->groups[img->selected_group].channels[0]].get_stats())
                if (stats->computed)
                    ImGui::TextFmt("{} NaN and {} infinite samples in this channel.", stats->summary.nan_pixels,
                                   stats->summary.inf_pixels);

        ImGui::RadioButton("Median of neighbors", &mode, Mode_Median);
        ImGui::Tooltip("Puts back something the surrounding samples agree with, so a firefly in a smooth "
                       "region leaves no trace.");
        ImGui::SameLine();
        ImGui::RadioButton("Fill with", &mode, Mode_Value);

        ImGui::BeginDisabled(mode != Mode_Value);
        ImGui::ColorEdit4("Value", &value.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);
        ImGui::EndDisabled();

        draw_edit_subject_selector();

        const auto result = ImGui::DialogButtons("Zap");
        if (result == ImGui::DialogResult::Confirm)
        {
            if (mode == Mode_Median)
                modify_channels(current_image(), "Zap gremlins", m_edit_subject,
                                [](const Array2Df &src, const Box2i &r) { return zapped_gremlins(src, r); });
            else
            {
                // Per channel, so the chosen color reaches the component it belongs to.
                const float4 v = value;
                modify_pixels(current_image(), "Zap gremlins", m_edit_subject,
                              [v](float s, int2, int slot) { return std::isfinite(s) ? s : v[slot % 4]; });
            }
            ImGui::CloseCurrentPopup();
        }
        else if (result == ImGui::DialogResult::Cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
