//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "annotations.h"

#include <algorithm>
#include <cmath>

namespace
{

/// An arrowhead's length and half-width, in multiples of the stroke width.
constexpr float k_head_length = 4.f, k_head_half_width = 2.f;
/// Most of the shaft an arrowhead may occupy, so a very short arrow stays an arrow.
constexpr float k_max_head_fraction = 0.6f;

VgCommand cmd(VgCommand::Type type, std::vector<float> data = {}, std::string text = {})
{
    VgCommand c;
    c.type = type;
    c.data = std::move(data);
    c.text = std::move(text);
    return c;
}

std::vector<float> color_floats(const float4 &c) { return {c.x, c.y, c.z, c.w}; }

/// Whether \p c would paint anything at all.
bool is_visible_color(const float4 &c) { return c.w > 0.f; }

/// Emit the shaft and head of an arrow from \p a's p0 to its p1.
void append_arrow(std::vector<VgCommand> &out, const Annotation &a, float scale)
{
    const float2 along  = a.p1 - a.p0;
    const float  length = std::sqrt(along.x * along.x + along.y * along.y);

    // No direction to point in, so no head; the shaft alone keeps a click without a drag visible.
    if (length <= 0.f)
    {
        out.push_back(cmd(VgCommand::Type::BeginPath));
        out.push_back(cmd(VgCommand::Type::MoveTo, {a.p0.x, a.p0.y}));
        out.push_back(cmd(VgCommand::Type::LineTo, {a.p1.x, a.p1.y}));
        out.push_back(cmd(VgCommand::Type::Stroke));
        return;
    }

    const float2 dir  = along / length;
    const float2 perp = float2{-dir.y, dir.x};

    // The head is proportioned in screen pixels, like the stroke it terminates, but goes out in image
    // coordinates with the rest of the geometry -- hence the division by the scale.
    const float head_len =
        std::min(k_head_length * a.stroke_width / std::max(scale, 1e-6f), k_max_head_fraction * length);
    const float  head_half = head_len * (k_head_half_width / k_head_length);
    const float2 base      = a.p1 - dir * head_len;

    // The shaft stops where the head begins rather than running under it, so a translucent stroke does not
    // show through as a darker wedge.
    out.push_back(cmd(VgCommand::Type::BeginPath));
    out.push_back(cmd(VgCommand::Type::MoveTo, {a.p0.x, a.p0.y}));
    out.push_back(cmd(VgCommand::Type::LineTo, {base.x, base.y}));
    out.push_back(cmd(VgCommand::Type::Stroke));

    // The interpreter has no arrowhead command, so the head is a closed subpath that is filled. Its color
    // is the stroke's: an arrowhead is the end of the line, not the interior of a shape.
    const float2 left = base + perp * head_half, right = base - perp * head_half;
    out.push_back(cmd(VgCommand::Type::FillColor, color_floats(a.stroke_color)));
    out.push_back(cmd(VgCommand::Type::BeginPath));
    out.push_back(cmd(VgCommand::Type::MoveTo, {a.p1.x, a.p1.y}));
    out.push_back(cmd(VgCommand::Type::LineTo, {left.x, left.y}));
    out.push_back(cmd(VgCommand::Type::LineTo, {right.x, right.y}));
    out.push_back(cmd(VgCommand::Type::ClosePath));
    out.push_back(cmd(VgCommand::Type::Fill));
}

} // namespace

const char *annotation_shape_name(Annotation::Shape shape)
{
    switch (shape)
    {
    case Annotation::Shape::Rect: return "Rectangle";
    case Annotation::Shape::Ellipse: return "Ellipse";
    case Annotation::Shape::Line: return "Line";
    case Annotation::Shape::Arrow: return "Arrow";
    case Annotation::Shape::Text: return "Text";
    default: return "Annotation";
    }
}

Box2f Annotation::bounds() const
{
    Box2f box;
    box.enclose(p0);
    // Text has no second point, and its extent depends on a font this cannot reach, so report the anchor.
    if (shape != Shape::Text)
        box.enclose(p1);
    return box;
}

std::string Annotation::display_label() const
{
    if (!label.empty())
        return label;
    if (shape == Shape::Text && !text.empty())
        return text;
    return annotation_shape_name(shape);
}

void append_vg_commands(std::vector<VgCommand> &out, const Annotation &a, float scale)
{
    if (a.shape == Annotation::Shape::Text)
    {
        // The interpreter draws text in the *fill* color, so the one color the user picked goes there.
        out.push_back(cmd(VgCommand::Type::FontSize, {a.font_size, float(VgCommand::Absolute)}));
        out.push_back(cmd(VgCommand::Type::TextAlign, {float(a.text_align)}));
        out.push_back(cmd(VgCommand::Type::FillColor, color_floats(a.stroke_color)));
        out.push_back(cmd(VgCommand::Type::Text, {a.p0.x, a.p0.y}, a.text));
        return;
    }

    out.push_back(cmd(VgCommand::Type::StrokeWidth, {a.stroke_width, float(VgCommand::Absolute)}));
    out.push_back(cmd(VgCommand::Type::StrokeColor, color_floats(a.stroke_color)));

    if (a.shape == Annotation::Shape::Arrow)
    {
        append_arrow(out, a, scale);
        return;
    }

    const bool filled = is_visible_color(a.fill_color);
    if (filled)
        out.push_back(cmd(VgCommand::Type::FillColor, color_floats(a.fill_color)));

    out.push_back(cmd(VgCommand::Type::BeginPath));
    switch (a.shape)
    {
    case Annotation::Shape::Rect:
    {
        // Rect takes a corner and an extent, so the corners are ordered first -- a drag that ran right to
        // left would otherwise give it a negative width.
        const float2 lo{std::min(a.p0.x, a.p1.x), std::min(a.p0.y, a.p1.y)};
        const float2 hi{std::max(a.p0.x, a.p1.x), std::max(a.p0.y, a.p1.y)};
        out.push_back(cmd(VgCommand::Type::Rect, {lo.x, lo.y, hi.x - lo.x, hi.y - lo.y}));
    }
    break;

    case Annotation::Shape::Ellipse:
    {
        const float2 center = (a.p0 + a.p1) * 0.5f;
        const float2 radii{std::abs(a.p1.x - a.p0.x) * 0.5f, std::abs(a.p1.y - a.p0.y) * 0.5f};
        out.push_back(cmd(VgCommand::Type::Ellipse, {center.x, center.y, radii.x, radii.y}));
    }
    break;

    case Annotation::Shape::Line:
    default:
        out.push_back(cmd(VgCommand::Type::MoveTo, {a.p0.x, a.p0.y}));
        out.push_back(cmd(VgCommand::Type::LineTo, {a.p1.x, a.p1.y}));
        break;
    }

    // Filled before stroked, so the outline sits over the interior rather than being half-covered by it.
    if (filled)
        out.push_back(cmd(VgCommand::Type::Fill));
    out.push_back(cmd(VgCommand::Type::Stroke));
}

std::vector<VgCommand> to_vg_commands(const std::vector<Annotation> &annotations, float scale)
{
    std::vector<VgCommand> out;
    for (const auto &a : annotations)
        if (a.visible)
            append_vg_commands(out, a, scale);
    return out;
}

namespace
{

/// Distance from \p p to the segment \p a -- \p b, all in the same space.
float distance_to_segment(float2 p, float2 a, float2 b)
{
    const float2 along = b - a;
    const float  len2  = dot(along, along);
    if (len2 <= 0.f)
        return length(p - a);
    const float t = std::clamp(dot(p - a, along) / len2, 0.f, 1.f);
    return length(p - (a + along * t));
}

/// \p a's two defining points, in screen coordinates and ordered so lo is the lower corner.
void screen_extent(const Annotation &a, const VgTransform &xform, float2 &lo, float2 &hi)
{
    const float2 s0 = xform.to_screen(a.p0), s1 = xform.to_screen(a.p1);
    lo = float2{std::min(s0.x, s1.x), std::min(s0.y, s1.y)};
    hi = float2{std::max(s0.x, s1.x), std::max(s0.y, s1.y)};
}

} // namespace

int annotation_handles(const Annotation &a, float2 out[Annotation::MaxHandles])
{
    switch (a.shape)
    {
    case Annotation::Shape::Rect:
    case Annotation::Shape::Ellipse:
    {
        const float2 lo{std::min(a.p0.x, a.p1.x), std::min(a.p0.y, a.p1.y)};
        const float2 hi{std::max(a.p0.x, a.p1.x), std::max(a.p0.y, a.p1.y)};
        out[0] = lo;
        out[1] = float2{hi.x, lo.y};
        out[2] = hi;
        out[3] = float2{lo.x, hi.y};
        for (int i = 0; i < 4; ++i) out[4 + i] = (out[i] + out[(i + 1) % 4]) * 0.5f;
        return 8;
    }

    case Annotation::Shape::Line:
    case Annotation::Shape::Arrow:
        out[0] = a.p0;
        out[1] = a.p1;
        return 2;

    default: return 0;
    }
}

int handle_at(const Annotation &a, float2 screen_pos, const VgTransform &xform, float radius)
{
    float2    handles[Annotation::MaxHandles];
    const int count = annotation_handles(a, handles);
    for (int i = 0; i < count; ++i)
        if (length(xform.to_screen(handles[i]) - screen_pos) <= radius)
            return i;
    return -1;
}

int annotation_at(const std::vector<Annotation> &annotations, float2 screen_pos, const VgTransform &xform, float slop)
{
    for (int i = int(annotations.size()) - 1; i >= 0; --i)
    {
        const Annotation &a = annotations[size_t(i)];
        if (!a.visible || a.locked)
            continue;

        // The stroke straddles the outline, so half of it widens the target along with the slop.
        const float tol    = slop + a.stroke_width * 0.5f;
        const bool  filled = a.fill_color.w > 0.f;

        switch (a.shape)
        {
        case Annotation::Shape::Rect:
        {
            float2 lo, hi;
            screen_extent(a, xform, lo, hi);
            if (filled && screen_pos.x >= lo.x - tol && screen_pos.x <= hi.x + tol && screen_pos.y >= lo.y - tol &&
                screen_pos.y <= hi.y + tol)
                return i;

            const float2 corners[4] = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
            for (int c = 0; c < 4; ++c)
                if (distance_to_segment(screen_pos, corners[c], corners[(c + 1) % 4]) <= tol)
                    return i;
        }
        break;

        case Annotation::Shape::Ellipse:
        {
            float2 lo, hi;
            screen_extent(a, xform, lo, hi);
            const float2 center = (lo + hi) * 0.5f;
            const float2 radii  = (hi - lo) * 0.5f;

            // A degenerate axis has no interior to speak of, so it is treated as the segment it looks like.
            if (radii.x <= 0.f || radii.y <= 0.f)
            {
                if (distance_to_segment(screen_pos, lo, hi) <= tol)
                    return i;
                break;
            }

            // Normalized radius, turned back into a screen distance by the smaller semi-axis: exact on a
            // circle, and close enough on an ellipse that the outline stays as easy to hit as a rectangle's.
            const float2 d      = (screen_pos - center) / radii;
            const float  radial = length(d);
            if (filled && radial <= 1.f)
                return i;
            if (std::abs(radial - 1.f) * std::min(radii.x, radii.y) <= tol)
                return i;
        }
        break;

        case Annotation::Shape::Line:
        case Annotation::Shape::Arrow:
            if (distance_to_segment(screen_pos, xform.to_screen(a.p0), xform.to_screen(a.p1)) <= tol)
                return i;
            break;

        case Annotation::Shape::Text:
            // Only the anchor: the glyphs' extent depends on a font this cannot reach.
            if (length(xform.to_screen(a.p0) - screen_pos) <= std::max(tol, a.font_size * 0.5f))
                return i;
            break;

        default: break;
        }
    }
    return -1;
}

int move_annotation_handle(Annotation &a, int index, float2 to)
{
    if (a.shape == Annotation::Shape::Line || a.shape == Annotation::Shape::Arrow)
    {
        if (index == 0)
            a.p0 = to;
        else if (index == 1)
            a.p1 = to;
        return index; // an endpoint is an endpoint however the line is turned around
    }

    if (a.shape != Annotation::Shape::Rect && a.shape != Annotation::Shape::Ellipse)
        return index;

    float2 lo{std::min(a.p0.x, a.p1.x), std::min(a.p0.y, a.p1.y)};
    float2 hi{std::max(a.p0.x, a.p1.x), std::max(a.p0.y, a.p1.y)};

    // Corners move both of their edges; the midpoints that follow them move only the edge they sit on, in
    // the order annotation_handles() lays them out.
    switch (index)
    {
    case 0: lo = to; break;
    case 1: hi.x = to.x, lo.y = to.y; break;
    case 2: hi = to; break;
    case 3: lo.x = to.x, hi.y = to.y; break;
    case 4: lo.y = to.y; break;
    case 5: hi.x = to.x; break;
    case 6: hi.y = to.y; break;
    case 7: lo.x = to.x; break;
    default: return index;
    }

    // A drag past the far side leaves lo above hi; ordering them here keeps p0 the low corner, which is
    // what every reader of a Rect or Ellipse assumes.
    const bool flip_x = lo.x > hi.x, flip_y = lo.y > hi.y;
    a.p0 = float2{std::min(lo.x, hi.x), std::min(lo.y, hi.y)};
    a.p1 = float2{std::max(lo.x, hi.x), std::max(lo.y, hi.y)};

    // Which is to say the corner the cursor is holding has been renumbered: a horizontal flip exchanges
    // left with right, a vertical one top with bottom, and the edge midpoints follow their edges.
    static constexpr int mirrored_x[4] = {1, 0, 3, 2}, mirrored_y[4] = {3, 2, 1, 0};
    if (index < 4)
    {
        if (flip_x)
            index = mirrored_x[index];
        if (flip_y)
            index = mirrored_y[index];
    }
    else
    {
        if (flip_y && (index == 4 || index == 6))
            index = index == 4 ? 6 : 4;
        if (flip_x && (index == 5 || index == 7))
            index = index == 5 ? 7 : 5;
    }
    return index;
}
