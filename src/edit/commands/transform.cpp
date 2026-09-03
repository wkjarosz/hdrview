//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file transform.cpp
    \author Wojciech Jarosz

    The edits that move samples instead of changing them: the flips and quarter turns, and the ones that
    change how many samples there are.
*/

#include "edit/commands.h"

#include "edit/edit_ops.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <hello_imgui/hello_imgui.h>

namespace
{

/// A flip or a quarter turn: its own inverse, or paired with the opposite one.
/**
    Every sample survives, so this is reversed by performing the opposite and costs a few bytes of history.
*/
class Geometric final : public EditCommand
{
public:
    Geometric(Info info, std::function<void(Image &)> forward, std::function<void(Image &)> backward) :
        EditCommand(std::move(info)), m_forward(std::move(forward)), m_backward(std::move(backward))
    {
        // moves every sample of every channel, so there is nothing for a scope to narrow
        m_info.draws_subject_selector = false;
    }

    void apply(const EditContext &ctx) override
    {
        modify_image(ctx, m_info.names.front(), m_forward, reversible(m_forward, m_backward));
    }

private:
    std::function<void(Image &)> m_forward, m_backward;
};

class Crop final : public EditCommand
{
public:
    Crop() : EditCommand({{"Crop to selection"}, ICON_MY_CROP, ImGuiMod_Alt | ImGuiKey_C})
    {
        // reshapes the image rather than writing into it, so the subject has nothing to say about it
        m_info.draws_subject_selector = false;
    }

    /// Only when there is something to crop to, and it is not already the whole image.
    bool enabled(const EditContext &ctx) const override
    {
        // cropping to the whole image would do nothing
        auto img = ctx.image;
        if (!img || !ctx.roi.has_volume())
            return false;

        Box2i box = ctx.roi;
        box.intersect(img->data_window);
        return box.has_volume() && box != img->data_window;
    }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        // enabled()'s conditions again, per image: a fan-out reaches images the rectangle misses, or
        // already is, and neither is a crop
        Box2i box = ctx.roi;
        box.intersect(img->data_window);
        if (!box.has_volume() || box == img->data_window)
            return;

        modify_image(ctx, "Crop to selection", [box](Image &i) { i.crop(box); }, structure_undo, Extent_Structure);

        // what was selected is now the whole image
        ctx.set_selection(Box2i{});
    }
};

/// Units a size can be given in.
enum SizeUnits : int
{
    Units_Pixels = 0,
    Units_Percent
};

/**
    Width and height, one to a row, with the chain that ties them drawn between the two (Photoshop's).

    \p size is always in pixels; the fields convert. Editing one row with the chain closed drives the other
    from \p original's ratio, so a run of edits cannot drift away from it a rounding at a time.

    \p lower is the smallest value a field may take: one ordinarily, but a size given as a change to the
    current one may be negative, which is how a canvas is trimmed.
*/
void size_fields(int2 *size, int *units, bool *locked, int2 original, int lower)
{
    // one width for every control in the column, so the labels line up
    const float field  = 9.f * HelloImGui::EmSize();
    const int2  before = *size;

    ImGui::SetNextItemWidth(field);
    ImGui::Combo("Units", units, "Pixels\0Percent\0");
    ImGui::Tooltip("Drag either field to sweep the size; ctrl-click one to type an exact value.");

    // where the two rows reach, so the bracket can be hung off the wider of them
    ImGui::RowSpan rows;

    auto row = [&](const char *label, auto &&draw_field)
    {
        ImGui::SetNextItemWidth(field);
        draw_field();
        rows.take();

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(label);
        rows.take();
    };

    if (*units == Units_Percent)
    {
        float2 pct{100.f * float(size->x) / float(std::max(1, original.x)),
                   100.f * float(size->y) / float(std::max(1, original.y))};

        const float pct_lower = 100.f * float(lower) / float(std::max(1, std::max(original.x, original.y)));
        row("Width", [&] { ImGui::DragFloat("##width", &pct.x, 0.5f, pct_lower, 1000.f, "%.1f %%"); });
        row("Height", [&] { ImGui::DragFloat("##height", &pct.y, 0.5f, pct_lower, 1000.f, "%.1f %%"); });

        // the same floor as the fields, which for a relative size is below zero: a percentage given as a
        // change trims when it is negative
        size->x = std::max(lower, int(std::lround(double(pct.x) * 0.01 * double(original.x))));
        size->y = std::max(lower, int(std::lround(double(pct.y) * 0.01 * double(original.y))));
    }
    else
    {
        row("Width", [&] { ImGui::DragInt("##width", &size->x, 1.f, lower, 65536, "%d px"); });
        row("Height", [&] { ImGui::DragInt("##height", &size->y, 1.f, lower, 65536, "%d px"); });

        *size = la::max(*size, int2{lower});
    }

    if (ImGui::RowBracketButton(*locked ? ICON_MY_LINK : ICON_MY_UNLINK, rows, *locked,
                                *locked ? "Width and height are tied to the original aspect ratio. Click to unlink."
                                        : "Width and height are set independently. Click to link."))
        *locked = !*locked;

    // follow whichever row was just edited, from the original ratio and not the current one
    if (*locked && original.x > 0 && original.y > 0)
    {
        if (size->x != before.x)
            size->y = std::max(1, int(std::lround(double(size->x) * double(original.y) / double(original.x))));
        else if (size->y != before.y)
            size->x = std::max(1, int(std::lround(double(size->y) * double(original.x) / double(original.y))));
    }
}

class ImageSize final : public EditCommand
{
public:
    ImageSize() :
        EditCommand({{"Image size...", "Resize the image"},
                     ICON_MY_IMAGE_SIZE,
                     ImGuiMod_Alt | ImGuiMod_Ctrl | ImGuiKey_I,
                     "Resize",
                     30.f})
    {
        m_info.has_dialog = true;

        // replaces the image rather than writing into it, so the subject has nothing to say about it
        m_info.draws_subject_selector = false;
    }

    /// Opens on the image's own size, and does not carry it to the next one.
    void on_open(EditContext &ctx) override
    {
        if (auto img = ctx.image)
            m_size = img->size();
        m_have_size = true;
    }
    void on_close(EditContext &) override { m_have_size = false; }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        const int2 original = img->size();
        if (!m_have_size)
        {
            m_size      = original;
            m_have_size = true;
        }

        ImGui::TextFmt("Current: {} x {} pixels", original.x, original.y);
        ImGui::Separator();

        size_fields(&m_size, &m_units, &m_locked, original, 1);

        // which way the resampling will go: shrinking averages over the samples each output covers,
        // growing interpolates between them
        if (m_size.x < original.x || m_size.y < original.y)
            ImGui::TextUnformatted("Reducing: samples are averaged.");
        else if (m_size.x > original.x || m_size.y > original.y)
            ImGui::TextUnformatted("Enlarging: samples are interpolated.");
    }

    void apply(const EditContext &ctx) override
    {
        const int2 out = m_size;
        if (m_have_size && out.x > 0 && out.y > 0)
            modify_image(ctx, "Image size", [out](Image &i) { i.resample(out); }, structure_undo, Extent_Structure);
    }

private:
    int2 m_size{0, 0};
    bool m_have_size = false;
    int  m_units     = Units_Pixels;
    bool m_locked    = true;
};

class CanvasSize final : public EditCommand
{
public:
    CanvasSize() :
        EditCommand(
            {{"Canvas size..."}, ICON_MY_CANVAS_SIZE, ImGuiMod_Alt | ImGuiMod_Ctrl | ImGuiKey_C, "Resize", 30.f})
    {
        m_info.has_dialog = true;

        m_info.draws_subject_selector = false;
    }

    /// Opens describing the canvas as it is: its own size, or, given relatively, no change at all.
    /**
        The flag is needed because zero is a meaningful relative size.
    */
    void on_open(EditContext &ctx) override
    {
        if (auto img = ctx.image)
            m_size = m_relative ? int2{0} : img->size();
        m_have_size = true;
    }
    void on_close(EditContext &) override { m_have_size = false; }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img)
            return;

        const int2 original = img->size();
        if (!m_have_size)
        {
            m_size      = m_relative ? int2{0} : original;
            m_have_size = true;
        }

        ImGui::TextFmt("Current: {} x {} pixels", original.x, original.y);
        ImGui::Separator();

        // a relative size counts down as well as up, so it is not bounded below by one
        size_fields(&m_size, &m_units, &m_locked, original, m_relative ? -65536 : 1);

        if (ImGui::Checkbox("Relative", &m_relative))
            // re-read rather than reset, so the fields describe the same canvas after the switch
            m_size = m_relative ? m_size - original : original + m_size;
        ImGui::Tooltip("Add the amounts above to the current size instead of replacing it. Negative "
                       "values trim.");

        // what it will produce, which a size given as a change does not say by itself
        if (m_relative)
        {
            const int2 target = la::max(original + m_size, int2{1});
            ImGui::TextFmt("Result: {} x {} pixels", target.x, target.y);
        }

        ImGui::SeparatorText("Anchor");
        ImGui::TextUnformatted("Where the existing pixels sit in the new canvas.");
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const int i = row * 3 + col;
                if (col)
                    ImGui::SameLine();
                if (ImGui::RadioButton(fmt::format("##anchor{}", i).c_str(), int(m_anchor) == i))
                    m_anchor = Image::CanvasAnchor(i);
            }
    }

    void apply(const EditContext &ctx) override
    {
        auto img = ctx.image;
        if (!img || !m_have_size)
            return;

        const int2 out = m_relative ? la::max(img->size() + m_size, int2{1}) : la::max(m_size, int2{1});
        const auto a   = m_anchor;
        modify_image(
            ctx, "Canvas size", [out, a](Image &i) { i.resize_canvas(out, a); }, structure_undo, Extent_Structure);
    }

private:
    int2                m_size{0, 0};
    bool                m_have_size = false;
    int                 m_units     = Units_Pixels;
    bool                m_locked    = false;
    bool                m_relative  = false;
    Image::CanvasAnchor m_anchor    = Image::Anchor_MiddleCenter;
};

} // namespace

void add_transform_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<Geometric>(
        EditCommand::Info{{"Flip image horizontally", "Mirror the pixels horizontally"}, ICON_MY_FLIP_HORIZ},
        [](Image &i) { i.flip_horizontal(); }, [](Image &i) { i.flip_horizontal(); }));
    out.push_back(std::make_unique<Geometric>(
        EditCommand::Info{{"Flip image vertically", "Mirror the pixels vertically"}, ICON_MY_FLIP_VERT},
        [](Image &i) { i.flip_vertical(); }, [](Image &i) { i.flip_vertical(); }));
    out.push_back(std::make_unique<Geometric>(
        EditCommand::Info{{"Rotate 90 degrees clockwise", "Turn clockwise"},
                          ICON_MY_ROTATE_CW,
                          ImGuiMod_Ctrl | ImGuiKey_RightBracket},
        [](Image &i) { i.rotate_90_cw(); }, [](Image &i) { i.rotate_90_ccw(); }));
    out.push_back(std::make_unique<Geometric>(
        EditCommand::Info{{"Rotate 90 degrees counter-clockwise", "Turn counter-clockwise"},
                          ICON_MY_ROTATE_CCW,
                          ImGuiMod_Ctrl | ImGuiKey_LeftBracket},
        [](Image &i) { i.rotate_90_ccw(); }, [](Image &i) { i.rotate_90_cw(); }));

    out.push_back(std::make_unique<Crop>());
    out.push_back(std::make_unique<ImageSize>());
    out.push_back(std::make_unique<CanvasSize>());
}
