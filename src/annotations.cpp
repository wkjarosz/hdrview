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
