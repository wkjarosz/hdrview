//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// The annotate tool and the Annotations panel: creating, picking up and editing a user's markup, and the
// marks drawn over a text annotation while it is being typed. What an annotation is, and how it flattens
// into drawing commands, is annotations.{h,cpp}.

#include "app.h"

#include "fonts.h"
#include "image.h"

#include "imgui_internal.h"

using namespace std;
using namespace HelloImGui;

/// The icon the panel and the shape picker show for \p shape.
static const char *annotation_shape_icon(Annotation::Shape shape)
{
    switch (shape)
    {
    case Annotation::Shape::Rect: return ICON_MY_SHAPE_RECT;
    case Annotation::Shape::Ellipse: return ICON_MY_SHAPE_ELLIPSE;
    case Annotation::Shape::Line: return ICON_MY_SHAPE_LINE;
    case Annotation::Shape::Arrow: return ICON_MY_SHAPE_ARROW;
    case Annotation::Shape::Text: return ICON_MY_SHAPE_TEXT;
    case Annotation::Shape::Freehand: return ICON_MY_SHAPE_FREEHAND;
    default: return ICON_MY_ANNOTATE;
    }
}

void HDRViewApp::set_annotation_shape(Annotation::Shape shape)
{
    m_annotation_shape = shape;

    // The tool's button says which shape it will draw, so the palette shows the choice without being asked.
    action(mouse_mode_action_name(MouseMode_Annotate)).icon = annotation_shape_icon(shape);
}

int HDRViewApp::active_annotation() const
{
    auto img = current_image();
    if (!img || m_active_annotation_on != img || m_active_annotation < 0 ||
        m_active_annotation >= int(img->annotations.size()))
        return -1;
    return m_active_annotation;
}

void HDRViewApp::set_active_annotation(int index)
{
    m_active_annotation    = index;
    m_active_annotation_on = index < 0 ? nullptr : current_image();
}

void HDRViewApp::cancel_annotation_drag()
{
    // Against the image the drag began on, which is not necessarily the one that is current now.
    const auto held  = m_active_annotation_on;
    const int  index = m_active_annotation;
    if (held && index >= 0 && index < int(held->annotations.size()))
    {
        if (m_annotation_drag == AnnotationDrag::Creating)
            held->annotations.erase(held->annotations.begin() + index);
        else if (m_annotation_drag != AnnotationDrag::None)
            held->annotations[size_t(index)] = m_annotation_drag_start;
    }

    if (m_annotation_drag == AnnotationDrag::Creating)
        set_active_annotation(-1);

    m_annotation_drag        = AnnotationDrag::None;
    m_annotation_drag_handle = -1;
}

// How far the cursor must travel before a scribble keeps another point, and how far a point may stray from
// its neighbors before being dropped again. Both screen pixels: a stroke should record the same shape
// however far the image is zoomed.
static constexpr float k_scribble_step = 2.f, k_scribble_tolerance = 1.f;

void HDRViewApp::handle_annotate_tool()
{
    auto  img = current_image();
    auto &io  = ImGui::GetIO();

    // A drag whose image is no longer current is abandoned, half-drawn shape and all: playback or a close
    // has put something else in its place.
    if (m_annotation_drag != AnnotationDrag::None && m_active_annotation_on != img)
    {
        cancel_annotation_drag();
        set_active_annotation(-1);
    }

    if (m_annotation_drag != AnnotationDrag::None && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        cancel_annotation_drag();
        return;
    }

    // Slop and handle radius are screen quantities, like everything else about how an annotation looks.
    const float slop   = 0.25f * HelloImGui::EmSize();
    const float radius = 0.3f * HelloImGui::EmSize();

    // Screen pixels per image pixel, which turns the two scribble constants into the image distances the
    // points are actually kept in.
    const VgTransform xform_for_handles = viewport_transform();
    const float       xform_scale       = xform_for_handles.scale;

    auto        &list  = img->annotations;
    const float2 pixel = pixel_at_app_pos(io.MousePos);

    // Double-clicking a text annotation on the image edits what it says, which is where it is read.
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        if (const int i = annotation_at(list, io.MousePos, viewport_transform(), slop);
            i >= 0 && list[size_t(i)].shape == Annotation::Shape::Text)
        {
            set_active_annotation(i);
            m_annotation_edit_text = true;

            // The press that opened it had already taken hold of the annotation to move it.
            m_annotation_drag = AnnotationDrag::None;
            return;
        }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto xform  = viewport_transform();
        const int  active = active_annotation();

        // Handles of the annotation already in hand come first: they are drawn over everything, and often
        // lie over a neighbor that would otherwise take the click.
        if (active >= 0)
            if (const int h = handle_at(list[size_t(active)], io.MousePos, xform, radius); h >= 0)
            {
                m_annotation_drag        = AnnotationDrag::Resizing;
                m_annotation_drag_handle = h;
                m_annotation_drag_start  = list[size_t(active)];
                return;
            }

        if (const int i = annotation_at(list, io.MousePos, xform, slop); i >= 0)
        {
            set_active_annotation(i);
            m_annotation_drag       = AnnotationDrag::Moving;
            m_annotation_drag_start = list[size_t(i)];
            m_annotation_grab       = pixel;
            return;
        }

        // Empty space draws a new one, this being a drawing tool; the pan tool and a scroll still move the
        // view. A click that never becomes a drag is undone on release, which is how clicking away clears
        // the selection.
        Annotation a = m_annotation_style;
        a.shape      = m_annotation_shape;
        a.points     = {pixel, pixel};
        list.push_back(a);
        set_active_annotation(int(list.size()) - 1);
        m_annotation_drag       = AnnotationDrag::Creating;
        m_annotation_drag_start = a;
        return;
    }

    const int active = active_annotation();
    if (m_annotation_drag == AnnotationDrag::None || active < 0)
        return;

    Annotation &a = list[size_t(active)];

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        switch (m_annotation_drag)
        {
        case AnnotationDrag::Creating:
            if (a.shape == Annotation::Shape::Freehand)
            {
                // A point per frame would record how fast the cursor moved, so one is kept only once the
                // last is a couple of screen pixels behind.
                const float step = k_scribble_step / std::max(xform_scale, 1e-6f);
                if (length(pixel - a.points.back()) >= step)
                    a.points.push_back(pixel);
            }
            else
                a.p1() = pixel;
            break;

        case AnnotationDrag::Moving:
        {
            // Against the annotation as it was at mouse-down, so a move never accumulates rounding.
            const float2 delta = pixel - m_annotation_grab;
            a.points           = m_annotation_drag_start.points;
            for (auto &p : a.points) p += delta;
        }
        break;

        case AnnotationDrag::Resizing:
            // Dragging a corner past its opposite renumbers the handles, so the drag follows the index
            // back and keeps hold of the corner under the cursor.
            m_annotation_drag_handle = move_annotation_handle(a, m_annotation_drag_handle, pixel, &xform_for_handles);
            break;

        default: break;
        }
    }
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (m_annotation_drag == AnnotationDrag::Creating)
        {
            // A captured scribble holds far more points than its shape needs. Simplified once, on
            // release, so what is dropped is judged against the whole stroke.
            if (a.shape == Annotation::Shape::Freehand)
                a.points = simplify_polyline(a.points, k_scribble_tolerance / std::max(xform_scale, 1e-6f));

            // A click that never became a drag leaves nothing behind: a shape with no extent is invisible
            // and cannot be taken hold of again. Text is the exception, a click being how one is placed,
            // and the panel opens its name for typing straight afterwards.
            if (a.shape == Annotation::Shape::Text)
                m_annotation_edit_text = true;
            else if (a.bounds().min == a.bounds().max)
            {
                list.pop_back();
                set_active_annotation(-1);
            }
        }

        m_annotation_drag        = AnnotationDrag::None;
        m_annotation_drag_handle = -1;
    }
}

void HDRViewApp::draw_text_editing() const
{
    const int active = active_annotation();
    if (active < 0 || !m_draw_annotations)
        return;

    const Annotation &a = current_image()->annotations[size_t(active)];
    if (a.shape != Annotation::Shape::Text)
        return;

    const auto xform = viewport_transform();

    // The same box the hit test uses, so what is boxed is what can be clicked.
    float2 lo, hi;
    if (!text_screen_box(a, xform, lo, hi))
        return;

    auto *draw_list = ImGui::GetBackgroundDrawList();

    // Boxed, so a selected string reads as selected even where its corners are not yet in reach.
    draw_list->AddRect(lo - 2.f, hi + 2.f, ImGui::GetColorU32(ImGuiCol_Border));

    // Dear ImGui draws the caret and the selection inside InputTextEx, which cannot be called from out
    // here; the state behind it can be read, so the same marks are drawn from it.
    ImGuiInputTextState *state = ImGui::GetInputTextState(m_annotation_rename_id);
    if (!state || m_annotation_renaming != active)
        return;

    // How far into the string a byte offset is, measured the way the box was. One line, as the overlay's
    // Text command is.
    const float line_h = hi.y - lo.y;
    auto        place  = [&](int offset)
    {
        const int n = std::clamp(offset, 0, int(a.text.size()));
        return xform.measure_text(a.font_face, a.font_size, a.text.substr(0, size_t(n))).x * xform.scale;
    };

    if (state->HasSelection())
    {
        const float from = place(std::min(state->GetSelectionStart(), state->GetSelectionEnd()));
        const float to   = place(std::max(state->GetSelectionStart(), state->GetSelectionEnd()));
        draw_list->AddRectFilled(lo + float2{from, 0.f}, lo + float2{to, line_h},
                                 ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
    }

    // Blinking as Dear ImGui's own does, so it reads as the same caret.
    if (std::fmod(state->CursorAnim, 1.20f) <= 0.80f)
    {
        const float x = place(state->GetCursorPos());
        draw_list->AddLine(lo + float2{x, 0.f}, lo + float2{x, line_h}, ImGui::GetColorU32(ImGuiCol_Text));
    }
}

/// What a row calls an annotation, cut with an ellipsis at \p width.
/**
    A text annotation is named by what it says, which can be a paragraph, so the row shows as much of it as
    it has room for.
*/
static std::string row_name(const Annotation &a, float width)
{
    std::string name =
        a.shape == Annotation::Shape::Text && a.label.empty() && !a.text.empty() ? a.text : a.display_label();

    if (ImGui::CalcTextSize(name.c_str()).x <= width)
        return name;

    const float ellipsis = ImGui::CalcTextSize("...").x;
    while (!name.empty() && ImGui::CalcTextSize(name.c_str()).x + ellipsis > width) name.pop_back();
    return name + "...";
}

/// A size, with the unit it is measured in chosen from a menu at the end of the field.
/**
    The protocol carries a scale kind on each of the two sizes it has, so both are offered: image pixels,
    which zoom with what they mark, and screen pixels, which do not. Switching converts \p value at the
    current \p scale, so what is on screen does not jump when the unit under it changes.
*/
static bool size_drag(const char *id, float &value, bool &relative, float speed, float lo_limit, float hi_limit,
                      float scale, float width)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float       arrow = ImGui::GetFrameHeight();

    ImGui::PushID(id);

    // The drag takes the whole width; the menu is laid over its right-hand end, so the two are one frame
    // rather than two side by side.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::DragFloat("##value", &value, speed, lo_limit, hi_limit, relative ? "%.2f img" : "%.1f px");
    ImGui::SetItemTooltip("%s", relative ? "Image pixels: zooms with what it marks."
                                         : "Screen pixels: the same size however far the view is zoomed.");

    const ImVec2 field_lo = ImGui::GetItemRectMin(), field_hi = ImGui::GetItemRectMax();
    const ImVec2 resume = ImGui::GetCursorScreenPos();

    // Drawn the way BeginCombo draws its own arrow: a button-colored box over the end of the frame, rounded
    // on the right, with the arrow inset by the frame padding at full size.
    const ImVec2 at{field_hi.x - arrow, field_lo.y};
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton("##units", ImVec2(arrow, field_hi.y - field_lo.y));

    auto *draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(at, field_hi,
                             ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button),
                             style.FrameRounding, ImDrawFlags_RoundCornersRight);
    ImGui::RenderArrow(draw_list, ImVec2(at.x + style.FramePadding.y, at.y + style.FramePadding.y),
                       ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Down, 1.f);

    // The menu was placed by hand, so the layout goes on from where the field left it.
    ImGui::SetCursorScreenPos(resume);

    if (clicked)
        ImGui::OpenPopup("##units");

    // Under the field's own bottom-left, where a combo puts its list, instead of wherever the cursor was.
    ImGui::SetNextWindowPos(ImVec2(field_lo.x, field_hi.y));
    if (ImGui::BeginPopup("##units"))
    {
        // Converted at the zoom it is switched under, so the size on screen is what it was.
        const float zoom = std::max(scale, 1e-6f);
        if (ImGui::Selectable("Relative px (zooms with the image)", relative) && !relative)
        {
            value /= zoom;
            relative = changed = true;
        }
        if (ImGui::Selectable("Absolute px (fixed on screen)", !relative) && relative)
        {
            value *= zoom;
            relative = false;
            changed  = true;
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

void HDRViewApp::draw_annotations_window()
{
    auto img = current_image();
    if (!img)
    {
        ImGui::TextDisabled("No image loaded.");
        return;
    }

    auto     &list   = img->annotations;
    const int active = active_annotation();

    // Taken before anything below flattens them, for the popup that wants ordinary widgets: the rows put
    // both to nothing so their buttons sit against each other and stay one line tall.
    const ImVec2 frame_padding  = ImGui::GetStyle().FramePadding;
    const float  item_spacing_x = ImGui::GetStyle().ItemSpacing.x;

    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
    ImGui::Checkbox("Show annotations in viewport", &m_draw_annotations);
    ImGui::PopStyleVar();

    // One row of controls, editing whichever annotation is in hand, or the look the next one will be drawn
    // with when none is. The same widgets either way, so there is nothing to learn twice.
    Annotation &edited = active >= 0 ? list[size_t(active)] : m_annotation_style;
    draw_annotation_controls(edited);

    // The viewport asks for this when a text annotation is placed with a click, which leaves it saying
    // nothing, and when one is double-clicked to be edited. Both open its row for typing.
    if (m_annotation_edit_text && active >= 0)
    {
        m_annotation_renaming   = active;
        m_annotation_rename_was = list[size_t(active)].text;
        snprintf(m_annotation_rename, sizeof(m_annotation_rename), "%s", m_annotation_rename_was.c_str());
    }

    // Applied after the list is drawn: removing a row mid-list renumbers the ones still to come.
    int erase = -1;

    // Where the rows are, so a drag can say which one the cursor is over.
    float first_row_top = 0.f, row_height = 0.f;

    // The same table the Images panel's list is: an outer border, striped rows, and rows reaching the
    // full width. A child window would inset them by its padding.
    static constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit |
                                                   ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("AnnotationList", 1, table_flags))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        // A row is as tall as its text, like the Images panel's, and its buttons sit against each other:
        // each already carries its own highlight, which is all the separation they need. No frame padding
        // either, which would otherwise be subtracted from a square button and push its glyph off center.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, 0.f);

        // Square, and big enough for the widest glyph any of these buttons draws: an icon is wider than the
        // font size it is drawn at, so a square of that would clip it.
        float side = ImGui::GetTextLineHeight();
        for (const char *icon : {ICON_MY_VISIBILITY, ICON_MY_VISIBILITY_OFF, ICON_MY_LOCK, ICON_MY_LOCK_OPEN,
                                 ICON_MY_SMOOTH, ICON_MY_POLYLINE, ICON_MY_FONT, ICON_MY_TRASH_CAN})
            side = std::max(side, ImGui::CalcTextSize(icon).x);
        const ImVec2 icon_sz{side, side};

        auto flat_toggle = [icon_sz](const char *on, const char *off, bool &value, const char *tooltip)
        {
            const bool clicked = ImGui::FlatButton(value ? on : off, false, icon_sz);
            if (clicked)
                value = !value;
            ImGui::SetItemTooltip("%s", tooltip);
            ImGui::SameLine();
            return clicked;
        };

        // The renderer's own overlay, listed but never edited: it belongs to the process sending it, and
        // this is the one place overlays are switched on and off.
        if (!img->vector_overlay.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            flat_toggle(ICON_MY_VISIBILITY, ICON_MY_VISIBILITY_OFF, img->vector_overlay_visible,
                        "Draw the renderer's overlay.");
            ImGui::TextDisabled(ICON_MY_LIST_VIEW " Renderer overlay");
            ImGui::Tooltip("Drawing commands streamed in by a renderer. They can be hidden here, but only "
                           "the process sending them can change them.");
        }

        for (int i = 0; i < int(list.size()); ++i)
        {
            auto &a = list[size_t(i)];
            ImGui::PushID(i);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            const float row_x = ImGui::GetCursorPosX();
            const float row_w = ImGui::GetContentRegionAvail().x;

            // The highlight goes down first and spans the row, with everything else drawn back over it,
            // so selecting reads across the whole row.
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable("##row", i == active,
                                  ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                                  ImVec2(0.f, icon_sz.y)))
                set_active_annotation(i);

            // Which row a press took hold of. ImGui's active item cannot say: its id is this row's index,
            // and reordering moves annotations between indices, so it would stay on the row number and the
            // list would flicker between two orders.
            if (ImGui::IsItemActivated())
                m_annotation_row_drag = i;

            // Taken while the selectable is the item under the cursor, and acted on below, once the name
            // it applies to has been drawn.
            const bool renamed_here = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            // Where the rows sit, for the reorder below. The pitch is measured between the first two rather
            // than assumed, since a table decides row heights for itself.
            if (i == 0)
            {
                first_row_top = ImGui::GetItemRectMin().y;
                row_height    = ImGui::GetItemRectSize().y;
            }
            else if (i == 1)
                row_height = ImGui::GetItemRectMin().y - first_row_top;

            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(row_x);

            flat_toggle(ICON_MY_VISIBILITY, ICON_MY_VISIBILITY_OFF, a.visible, "Draw this annotation.");
            flat_toggle(ICON_MY_LOCK, ICON_MY_LOCK_OPEN, a.locked,
                        "A locked annotation cannot be picked up in the viewport.");

            // One column, whatever the shape has to put in it: a scribble runs a curve through its points,
            // a text annotation picks its face and size, and the rest hold the place so the shape icons
            // beside them still line up.
            if (a.shape == Annotation::Shape::Freehand)
            {
                if (flat_toggle(ICON_MY_SMOOTH, ICON_MY_POLYLINE, a.smooth,
                                "Draw this scribble as a curve through its points."))
                    m_annotation_style.smooth = a.smooth;
            }
            else if (a.shape == Annotation::Shape::Text)
            {
                if (ImGui::FlatButton(ICON_MY_FONT, false, icon_sz))
                    ImGui::OpenPopup("##font");
                ImGui::SetItemTooltip("The face and size this text is drawn in.");
                ImGui::SameLine();
                draw_font_popup(a, frame_padding, item_spacing_x);
            }
            else
            {
                ImGui::Dummy(icon_sz);
                ImGui::SameLine();
            }

            if (m_annotation_renaming == i)
            {
                // Only the name becomes a field; the icons stay where they are, so a row does not
                // rearrange itself the moment it is renamed.
                ImGui::TextUnformatted(annotation_shape_icon(a.shape));
                ImGui::SameLine();

                // Focused the frame it appears, so the name can be typed straight away; once it holds the
                // focus it is the active item, so this stops asking for it. Enter or clicking away keeps
                // the name, and Escape drops it, InputText restoring its own buffer.
                if (!ImGui::IsAnyItemActive())
                    ImGui::SetKeyboardFocusHere();

                const float field_w = row_x + row_w - icon_sz.x - ImGui::GetCursorPosX();

                ImGui::SetNextItemWidth(field_w);
                const bool edited = ImGui::InputTextWithHint("##rename", a.display_label().c_str(), m_annotation_rename,
                                                             sizeof(m_annotation_rename));

                // So the viewport can find this field's caret and selection and show them on the image.
                m_annotation_rename_id = ImGui::GetItemID();

                // Focusing a field from code selects all of it; an edit opened from the image continues
                // what is already there, so the caret goes to the end instead.
                if (m_annotation_edit_text)
                    if (auto *state = ImGui::GetInputTextState(m_annotation_rename_id))
                    {
                        state->ReloadUserBufAndMoveToEnd();
                        m_annotation_edit_text = false;
                    }

                // For a text annotation the row's name is what it says, so this is how the text is typed,
                // and it is written as it is typed, so the image shows it before the field is left.
                auto &named = a.shape == Annotation::Shape::Text ? a.text : a.label;
                if (edited)
                    named = m_annotation_rename;

                if (ImGui::IsItemDeactivated())
                {
                    // Escape asks for what was there before, and every keystroke has already been applied.
                    if (!ImGui::IsItemDeactivatedAfterEdit())
                        named = m_annotation_rename_was;
                    m_annotation_renaming  = -1;
                    m_annotation_edit_text = false;

                    // A text annotation with nothing to say is not kept: placing one and thinking better
                    // of it leaves nothing behind, as a click that never became a drag does.
                    if (a.shape == Annotation::Shape::Text && a.text.empty())
                        erase = i;
                }
            }
            else
            {
                const float name_w = row_x + row_w - icon_sz.x - ImGui::GetCursorPosX() -
                                     ImGui::CalcTextSize(annotation_shape_icon(a.shape)).x - ImGui::CalcTextSize(" ").x;
                ImGui::TextUnformatted(
                    fmt::format("{} {}", annotation_shape_icon(a.shape), row_name(a, name_w)).c_str());

                // Double-clicking the row renames it in place, which is where the name is read.
                if (renamed_here)
                {
                    m_annotation_renaming   = i;
                    m_annotation_rename_was = a.shape == Annotation::Shape::Text ? a.text : a.label;
                    snprintf(m_annotation_rename, sizeof(m_annotation_rename), "%s", m_annotation_rename_was.c_str());
                }
            }

            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(row_x + row_w - icon_sz.x);
            if (ImGui::FlatButton(ICON_MY_TRASH_CAN, false, icon_sz))
                erase = i;
            ImGui::SetItemTooltip("Delete this annotation.");

            ImGui::PopID();
        }

        ImGui::PopStyleVar(2);
        ImGui::EndTable();
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_annotation_row_drag = -1;

    // The row the cursor is over now, which a fast drag can be several away from. The list is drawn in
    // order, so moving a row is what puts one annotation in front of another.
    if (erase < 0 && m_annotation_row_drag >= 0 && m_annotation_row_drag < int(list.size()) && row_height > 0.f)
    {
        const int to =
            std::clamp(int((ImGui::GetIO().MousePos.y - first_row_top) / row_height), 0, int(list.size()) - 1);
        if (to != m_annotation_row_drag)
        {
            m_annotation_renaming = -1;

            Annotation moved = list[size_t(m_annotation_row_drag)];
            list.erase(list.begin() + m_annotation_row_drag);
            list.insert(list.begin() + to, moved);

            // What is in hand is the annotation, not the row it was in, so the selection follows it.
            if (active == m_annotation_row_drag)
                set_active_annotation(to);
            else if (active > m_annotation_row_drag && active <= to)
                set_active_annotation(active - 1);
            else if (active < m_annotation_row_drag && active >= to)
                set_active_annotation(active + 1);

            m_annotation_row_drag = to;
        }
    }

    if (erase >= 0)
    {
        // The row being renamed is about to be a different annotation, or none.
        m_annotation_renaming = -1;

        list.erase(list.begin() + erase);
        // The rows after it have shifted down, so what was in hand has to shift with them.
        if (active == erase)
            set_active_annotation(-1);
        else if (active > erase)
            set_active_annotation(active - 1);
    }
}

/// The "Add:" label and the picker that says which shape the tool draws next, as one table cell.
void HDRViewApp::draw_shape_picker(bool named)
{
    ImGui::TableNextColumn();
    const auto &style = ImGui::GetStyle();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Add:");
    ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string preview = named ? fmt::format("{} {}", annotation_shape_icon(m_annotation_shape),
                                                    annotation_shape_name(m_annotation_shape))
                                      : annotation_shape_icon(m_annotation_shape);
    if (ImGui::BeginCombo("##shape", preview.c_str()))
    {
        for (int n = 0; n < int(Annotation::Shape::COUNT); ++n)
        {
            const auto shape = Annotation::Shape(n);
            // The names are always spelled out in the list, whatever the closed control has room for.
            const bool is_selected = shape == m_annotation_shape;
            if (ImGui::Selectable(
                    fmt::format("{} {}", annotation_shape_icon(shape), annotation_shape_name(shape)).c_str(),
                    is_selected))
                set_annotation_shape(shape);
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Which shape the annotate tool draws next.");
}

void HDRViewApp::draw_font_popup(Annotation &a, const ImVec2 &frame_padding, float item_spacing_x)
{
    if (!ImGui::BeginPopup("##font"))
        return;

    // The rows flatten their padding to stay one line tall; a popup off one of them is an ordinary window
    // and wants ordinary widgets. Its own two rows sit closer than the default, which is spaced for a
    // window.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, frame_padding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(item_spacing_x, frame_padding.y));

    const auto &faces = annotation_font_faces();
    const char *shown = faces.front().label;
    for (const auto &f : faces)
        if (a.font_face == f.name)
            shown = f.label;

    ImGui::SetNextItemWidth(EmSize(8));
    if (ImGui::BeginCombo("##face", shown))
    {
        for (const auto &f : faces)
        {
            const bool is_selected = a.font_face == f.name;
            if (ImGui::Selectable(f.label, is_selected))
            {
                a.font_face                  = f.name;
                m_annotation_style.font_face = a.font_face;
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (size_drag("size", a.font_size, a.font_size_relative, 0.25f, Annotation::MinFontSize, Annotation::MaxFontSize,
                  viewport_transform().scale, EmSize(9)))
    {
        m_annotation_style.font_size          = a.font_size;
        m_annotation_style.font_size_relative = a.font_size_relative;
    }

    // Which corner or edge of the string lands on the point it was placed at, which is what NanoVG's
    // align flags say. Each square carries a dot where its own anchor sits, drawn because FontAwesome has
    // no diagonal arrows and four of the nine would be blank under it. Baseline is the tenth position and
    // has no square; only a renderer sends it.
    constexpr int h_of[3] = {VgCommand::AlignLeft, VgCommand::AlignCenter, VgCommand::AlignRight};
    constexpr int v_of[3] = {VgCommand::AlignTop, VgCommand::AlignMiddle, VgCommand::AlignBottom};

    static const char *const rows[3] = {"top", "middle", "bottom"};
    static const char *const cols[3] = {"left", "center", "right"};

    const float side = ImGui::GetFrameHeight() * 0.8f;
    const float grid = 3.f * side + 2.f;

    // Set against the middle of the grid. The offset goes inside the label's own group; outside it, it
    // would move the first square alone, the other two lining up with the line the label left behind.
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 0.5f * (grid - ImGui::GetTextLineHeight()));
    ImGui::TextUnformatted("Anchor");
    ImGui::EndGroup();
    ImGui::SameLine(0.f, item_spacing_x);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::BeginGroup();
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            ImGui::PushID(row * 3 + col);
            if (col)
                ImGui::SameLine();

            const bool on = (a.text_align & h_of[col]) != 0 && (a.text_align & v_of[row]) != 0;
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
            if (ImGui::Button("##cell", ImVec2(side, side)))
            {
                a.text_align                  = h_of[col] | v_of[row];
                m_annotation_style.text_align = a.text_align;
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Anchor the text at its %s %s", rows[row], cols[col]);

            // The dot sits where this square's own anchor would, so a square says what it does.
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            const float  inset = 0.25f * side;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(ImLerp(lo.x + inset, hi.x - inset, 0.5f * float(col)),
                                                               ImLerp(lo.y + inset, hi.y - inset, 0.5f * float(row))),
                                                        ImMax(1.5f, side * 0.1f), ImGui::GetColorU32(ImGuiCol_Text));

            ImGui::PopID();
        }
    ImGui::EndGroup();
    ImGui::PopStyleVar(2);

    ImGui::PopStyleVar(2);
    ImGui::EndPopup();
}

void HDRViewApp::draw_annotation_controls(Annotation &a)
{
    const auto &style = ImGui::GetStyle();

    // Icon alone, or icon and name once the row can spare the difference. It snaps between the two rather
    // than growing with the panel, so the control does not change size as the panel is resized. Both widths
    // leave room for the drop-down arrow, which is a frame wide at the right-hand end.
    const float arrow_w   = ImGui::GetFrameHeight();
    const float combo_pad = 2.f * style.FramePadding.x + arrow_w;

    float names_w = 0.f;
    for (int n = 0; n < int(Annotation::Shape::COUNT); ++n)
        names_w = std::max(names_w, ImGui::CalcTextSize(annotation_shape_name(Annotation::Shape(n))).x);

    const float combo_narrow = ImGui::IconSize().x + combo_pad;
    const float combo_wide   = ImGui::IconSize().x + style.ItemInnerSpacing.x + names_w + combo_pad;

    // "Add:" and the picker share a column, separated by their own spacing: in columns of their own the
    // cell padding between them would set the label further from what it labels than from the drag beside.
    const float add_text_w = ImGui::CalcTextSize("Add:").x + style.ItemInnerSpacing.x;
    const float colors_w   = ImGui::GetFrameHeight();
    const float width_min  = EmSize(3.5f);

    // Spare width all goes to the width drag, until there is enough of it for the picker to spell its
    // names out as well.
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool  named = avail >= colors_w + width_min + add_text_w + combo_wide + 3.f * style.CellPadding.x;
    const float add_w = add_text_w + (named ? combo_wide : combo_narrow);

    if (ImGui::BeginTable("##AnnotationControls", 3,
                          ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX |
                              ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("add", ImGuiTableColumnFlags_WidthFixed, add_w);
        ImGui::TableSetupColumn("colors", ImGuiTableColumnFlags_WidthFixed, colors_w);
        ImGui::TableSetupColumn("width", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableNextRow();

        draw_shape_picker(named);

        ImGui::TableNextColumn();
        bool restyled = ImGui::StrokeFillSwatches("Colors", a.stroke_color, a.fill_color);

        ImGui::TableNextColumn();
        restyled |= size_drag("width", a.stroke_width, a.stroke_width_relative, 0.05f, 0.01f, 512.f,
                              viewport_transform().scale, ImGui::GetContentRegionAvail().x);

        // Restyling the annotation in hand also sets what the next one will look like, so a color or a
        // width chosen once carries forward instead of being forgotten when the selection is dropped.
        if (restyled && &a != &m_annotation_style)
        {
            m_annotation_style.stroke_color          = a.stroke_color;
            m_annotation_style.fill_color            = a.fill_color;
            m_annotation_style.stroke_width          = a.stroke_width;
            m_annotation_style.stroke_width_relative = a.stroke_width_relative;
        }

        ImGui::EndTable();
    }
}
