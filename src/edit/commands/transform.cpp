//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file transform.cpp
    \author Wojciech Jarosz

    The edits that move samples rather than change them: the flips and quarter turns, which every sample
    survives, and the ones that change how many samples there are.
*/

#include "edit/commands.h"

#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <hello_imgui/hello_imgui.h>

namespace
{

//! A flip or a quarter turn: its own inverse, or paired with the opposite one.
/*!
    Reversed by performing the opposite rather than by storing pixels, since every sample survives -- so a
    full-image geometric change costs a few bytes of history.
*/
class Geometric final : public EditCommand
{
public:
    Geometric(Info info, std::function<void(Image &)> forward, std::function<void(Image &)> backward) :
        m_info(std::move(info)), m_forward(std::move(forward)), m_backward(std::move(backward))
    {
    }

    Info info() const override { return m_info; }

    void apply(EditContext &ctx) override { ctx.modify_reversibly(m_info.names.front(), m_forward, m_backward); }

private:
    Info                         m_info;
    std::function<void(Image &)> m_forward, m_backward;
};

class Crop final : public EditCommand
{
public:
    Info info() const override { return {{"Crop to selection"}, ICON_MY_CROP, ImGuiMod_Alt | ImGuiKey_C}; }

    //! Only when there is something to crop to, and it is not already the whole image.
    bool enabled(const EditContext &ctx) const override
    {
        // Needs something to crop to, and cropping to the whole image would do nothing.
        auto img = ctx.image();
        if (!img || !ctx.selection().has_volume())
            return false;

        Box2i box = ctx.selection();
        box.intersect(img->data_window);
        return box.has_volume() && box != img->data_window;
    }

    void apply(EditContext &ctx) override
    {
        const Box2i box = ctx.selection();
        ctx.modify_structure("Crop to selection", [box](Image &i) { i.crop(box); });

        // What was selected is now the whole image, so the selection has nothing left to say.
        ctx.set_selection(Box2i{});
    }
};

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

class ImageSize final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Image size...", "Resize the image"},
                ICON_MY_IMAGE_SIZE,
                ImGuiMod_Alt | ImGuiMod_Ctrl | ImGuiKey_I,
                ImGuiInputFlags_None,
                "Resize",
                30.f,
                false};
    }

    //! Opens on the image's own size, and does not carry it to the next one.
    void on_open(EditContext &ctx) override
    {
        if (auto img = ctx.image())
            m_size = img->size();
    }
    void on_close(EditContext &) override { m_size = int2{0}; }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        const int2 original = img->size();
        if (m_size.x <= 0 || m_size.y <= 0)
            m_size = original;

        ImGui::TextFmt("Current: {} x {} pixels", original.x, original.y);
        ImGui::Separator();

        size_fields(&m_size, &m_units, &m_locked, original);

        // Which way the resampling will go, since the two directions do different things: shrinking
        // averages over the samples each output covers, growing interpolates between them.
        if (m_size.x < original.x || m_size.y < original.y)
            ImGui::TextUnformatted("Reducing: samples are averaged.");
        else if (m_size.x > original.x || m_size.y > original.y)
            ImGui::TextUnformatted("Enlarging: samples are interpolated.");
    }

    void apply(EditContext &ctx) override
    {
        const int2 out = m_size;
        if (out.x > 0 && out.y > 0)
            ctx.modify_structure("Image size", [out](Image &i) { i.resample(out); });
    }

private:
    int2 m_size{0, 0};
    int  m_units  = Units_Pixels;
    bool m_locked = true;
};

class CanvasSize final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Canvas size..."},
                ICON_MY_CANVAS_SIZE,
                ImGuiMod_Alt | ImGuiMod_Ctrl | ImGuiKey_C,
                ImGuiInputFlags_None,
                "Resize",
                30.f,
                false};
    }

    void on_open(EditContext &ctx) override
    {
        if (auto img = ctx.image())
            m_size = img->size();
    }
    void on_close(EditContext &) override { m_size = int2{0}; }

    void draw(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        const int2 original = img->size();
        if (m_size.x <= 0 || m_size.y <= 0)
            m_size = original;

        ImGui::TextFmt("Current: {} x {} pixels", original.x, original.y);
        ImGui::Separator();

        size_fields(&m_size, &m_units, &m_locked, original);

        ImGui::Checkbox("Relative", &m_relative);
        ImGui::Tooltip("Add the amounts above to the current size instead of replacing it. Negative "
                       "values trim.");

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

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img || m_size.x <= 0 || m_size.y <= 0)
            return;

        const int2 out = m_relative ? la::max(img->size() + m_size, int2{1}) : m_size;
        const auto a   = m_anchor;
        ctx.modify_structure("Canvas size", [out, a](Image &i) { i.resize_canvas(out, a); });
    }

private:
    int2                m_size{0, 0};
    int                 m_units    = Units_Pixels;
    bool                m_locked   = false;
    bool                m_relative = false;
    Image::CanvasAnchor m_anchor   = Image::Anchor_MiddleCenter;
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
