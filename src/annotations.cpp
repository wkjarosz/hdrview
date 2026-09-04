//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "annotations.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace
{

// Stable identifiers for Shape as stored in .hsess session files. Independent of the display names
// annotation_shape_name() returns, so relabeling one in the GUI cannot change what an old session means.
const char *const g_shape_ids[int(Annotation::Shape::COUNT)] = {"rect", "ellipse", "line", "arrow", "text", "freehand"};

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

/// How finely a curved span is sampled when it has to stand in as a polyline.
constexpr int k_curve_samples = 8;

/// The cubic Bezier for the span from \p pts[i] to \p pts[i + 1], as its four control points.
/**
    Uniform Catmull-Rom: the curve passes through every point, and each tangent is a sixth of the vector
    between that point's neighbors. An end repeats its own point for the neighbor it lacks, so the curve
    starts and finishes along the path rather than turning away from it.
*/
void catmull_rom_span(const std::vector<float2> &pts, size_t i, float2 b[4])
{
    const float2 &p1 = pts[i], &p2 = pts[i + 1];
    const float2 &p0 = i > 0 ? pts[i - 1] : p1;
    const float2 &p3 = i + 2 < pts.size() ? pts[i + 2] : p2;

    b[0] = p1;
    b[1] = p1 + (p2 - p0) / 6.f;
    b[2] = p2 - (p3 - p1) / 6.f;
    b[3] = p2;
}

/// Emit the shaft and head of an arrow from \p a's p0 to its p1.
void append_arrow(std::vector<VgCommand> &out, const Annotation &a, float scale)
{
    const float2 along  = a.p1() - a.p0();
    const float  length = std::sqrt(along.x * along.x + along.y * along.y);

    // No direction to point in, so no head; the shaft alone keeps a click without a drag visible.
    if (length <= 0.f)
    {
        out.push_back(cmd(VgCommand::Type::BeginPath));
        out.push_back(cmd(VgCommand::Type::MoveTo, {a.p0().x, a.p0().y}));
        out.push_back(cmd(VgCommand::Type::LineTo, {a.p1().x, a.p1().y}));
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
    const float2 base      = a.p1() - dir * head_len;

    // The shaft stops where the head begins rather than running under it, so a translucent stroke does not
    // show through as a darker wedge.
    out.push_back(cmd(VgCommand::Type::BeginPath));
    out.push_back(cmd(VgCommand::Type::MoveTo, {a.p0().x, a.p0().y}));
    out.push_back(cmd(VgCommand::Type::LineTo, {base.x, base.y}));
    out.push_back(cmd(VgCommand::Type::Stroke));

    // The interpreter has no arrowhead command, so the head is a closed subpath that is filled. Its color
    // is the stroke's: an arrowhead is the end of the line, not the interior of a shape.
    const float2 left = base + perp * head_half, right = base - perp * head_half;
    out.push_back(cmd(VgCommand::Type::FillColor, color_floats(a.stroke_color)));
    out.push_back(cmd(VgCommand::Type::BeginPath));
    out.push_back(cmd(VgCommand::Type::MoveTo, {a.p1().x, a.p1().y}));
    out.push_back(cmd(VgCommand::Type::LineTo, {left.x, left.y}));
    out.push_back(cmd(VgCommand::Type::LineTo, {right.x, right.y}));
    out.push_back(cmd(VgCommand::Type::ClosePath));
    out.push_back(cmd(VgCommand::Type::Fill));
}

} // namespace

void to_json(json &j, const Annotation &a)
{
    j              = json::object();
    j["shape"]     = g_shape_ids[size_t(a.shape) < std::size(g_shape_ids) ? size_t(a.shape) : 0];
    j["points"]    = a.points;
    j["stroke"]    = a.stroke_color;
    j["fill"]      = a.fill_color;
    j["width"]     = a.stroke_width;
    j["font_size"] = a.font_size;
    j["font_face"] = a.font_face;
    j["align"]     = a.text_align;
    j["smooth"]    = a.smooth;
    j["visible"]   = a.visible;
    j["locked"]    = a.locked;

    // Omitted when empty, which is the common case: a shape with no text and no label of its own.
    if (!a.text.empty())
        j["text"] = a.text;
    if (!a.label.empty())
        j["label"] = a.label;
}

void from_json(const json &j, Annotation &a)
{
    a = Annotation{};

    const auto id = j.value<std::string>("shape", g_shape_ids[0]);
    for (size_t i = 0; i < std::size(g_shape_ids); ++i)
        if (id == g_shape_ids[i])
            a.shape = Annotation::Shape(i);

    // Anything the file leaves out keeps the default it was constructed with, so a session written by an
    // older version reads as one of those rather than as garbage.
    // A shape with no points at all could not be drawn or picked up, so an empty array is refused rather
    // than stored: p0() and p1() are allowed to assume there is always a point.
    if (j.contains("points"))
        if (auto pts = j.at("points").get<std::vector<float2>>(); !pts.empty())
            a.points = std::move(pts);
    if (j.contains("stroke"))
        j.at("stroke").get_to(a.stroke_color);
    if (j.contains("fill"))
        j.at("fill").get_to(a.fill_color);

    a.stroke_width = j.value("width", a.stroke_width);
    a.font_size    = j.value("font_size", a.font_size);
    a.font_face    = j.value<std::string>("font_face", a.font_face);
    a.text_align   = j.value("align", a.text_align);
    a.smooth       = j.value("smooth", a.smooth);
    a.visible      = j.value("visible", a.visible);
    a.locked       = j.value("locked", a.locked);
    a.text         = j.value<std::string>("text", a.text);
    a.label        = j.value<std::string>("label", a.label);
}

const std::vector<AnnotationFace> &annotation_font_faces()
{
    static const std::vector<AnnotationFace> faces{
        {"sans", "Sans"}, {"sans-bold", "Sans bold"}, {"mono", "Mono"}, {"mono-bold", "Mono bold"}};
    return faces;
}

const char *annotation_shape_name(Annotation::Shape shape)
{
    switch (shape)
    {
    case Annotation::Shape::Rect: return "Rectangle";
    case Annotation::Shape::Ellipse: return "Ellipse";
    case Annotation::Shape::Line: return "Line";
    case Annotation::Shape::Arrow: return "Arrow";
    case Annotation::Shape::Text: return "Text";
    case Annotation::Shape::Freehand: return "Scribble";
    default: return "Annotation";
    }
}

Box2f Annotation::bounds() const
{
    Box2f box;
    box.enclose(p0());
    // Text has no second point, and its extent depends on a font this cannot reach, so report the anchor.
    if (shape != Shape::Text)
        for (const auto &p : points) box.enclose(p);
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
        out.push_back(cmd(VgCommand::Type::FontFace, {}, a.font_face));
        out.push_back(cmd(VgCommand::Type::FontSize, {a.font_size, float(VgCommand::Absolute)}));
        out.push_back(cmd(VgCommand::Type::TextAlign, {float(a.text_align)}));
        out.push_back(cmd(VgCommand::Type::FillColor, color_floats(a.stroke_color)));
        out.push_back(cmd(VgCommand::Type::Text, {a.p0().x, a.p0().y}, a.text));
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
        const float2 lo{std::min(a.p0().x, a.p1().x), std::min(a.p0().y, a.p1().y)};
        const float2 hi{std::max(a.p0().x, a.p1().x), std::max(a.p0().y, a.p1().y)};
        out.push_back(cmd(VgCommand::Type::Rect, {lo.x, lo.y, hi.x - lo.x, hi.y - lo.y}));
    }
    break;

    case Annotation::Shape::Ellipse:
    {
        const float2 center = (a.p0() + a.p1()) * 0.5f;
        const float2 radii{std::abs(a.p1().x - a.p0().x) * 0.5f, std::abs(a.p1().y - a.p0().y) * 0.5f};
        out.push_back(cmd(VgCommand::Type::Ellipse, {center.x, center.y, radii.x, radii.y}));
    }
    break;

    case Annotation::Shape::Freehand:
        // The path as it was drawn, point to point or as the curve through them -- the interpreter
        // tessellates a cubic itself, so a smooth stroke costs one command per span rather than a sampled
        // polyline. Closed when it is to be filled, so the fill has an interior to cover, and left open
        // otherwise, which is what a stroked scribble should look like.
        out.push_back(cmd(VgCommand::Type::MoveTo, {a.points.front().x, a.points.front().y}));
        if (a.smooth)
            for (size_t i = 0; i + 1 < a.points.size(); ++i)
            {
                float2 b[4];
                catmull_rom_span(a.points, i, b);
                out.push_back(cmd(VgCommand::Type::BezierTo, {b[1].x, b[1].y, b[2].x, b[2].y, b[3].x, b[3].y}));
            }
        else
            for (size_t i = 1; i < a.points.size(); ++i)
                out.push_back(cmd(VgCommand::Type::LineTo, {a.points[i].x, a.points[i].y}));
        if (filled)
            out.push_back(cmd(VgCommand::Type::ClosePath));
        break;

    case Annotation::Shape::Line:
    default:
        out.push_back(cmd(VgCommand::Type::MoveTo, {a.p0().x, a.p0().y}));
        out.push_back(cmd(VgCommand::Type::LineTo, {a.p1().x, a.p1().y}));
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

/// Distance from \p p to the box \p lo -- \p hi, and zero inside it.
float distance_to_box(float2 p, float2 lo, float2 hi)
{
    const float dx = std::max({lo.x - p.x, 0.f, p.x - hi.x});
    const float dy = std::max({lo.y - p.y, 0.f, p.y - hi.y});
    return std::sqrt(dx * dx + dy * dy);
}

/// The extent of \p pts in screen coordinates, ordered so lo is the lower corner.
void screen_extent(const std::vector<float2> &pts, const VgTransform &xform, float2 &lo, float2 &hi)
{
    lo = float2{FLT_MAX, FLT_MAX};
    hi = float2{-FLT_MAX, -FLT_MAX};
    for (const auto &p : pts)
    {
        const float2 s = xform.to_screen(p);
        lo             = float2{std::min(lo.x, s.x), std::min(lo.y, s.y)};
        hi             = float2{std::max(hi.x, s.x), std::max(hi.y, s.y)};
    }
}

void screen_extent(const Annotation &a, const VgTransform &xform, float2 &lo, float2 &hi)
{
    screen_extent(a.points, xform, lo, hi);
}

} // namespace

int annotation_handles(const Annotation &a, float2 out[Annotation::MaxHandles])
{
    switch (a.shape)
    {
    case Annotation::Shape::Rect:
    case Annotation::Shape::Ellipse:
    case Annotation::Shape::Freehand:
    {
        const Box2f  box = a.bounds();
        const float2 lo = box.min, hi = box.max;
        out[0] = lo;
        out[1] = float2{hi.x, lo.y};
        out[2] = hi;
        out[3] = float2{lo.x, hi.y};
        for (int i = 0; i < 4; ++i) out[4 + i] = (out[i] + out[(i + 1) % 4]) * 0.5f;
        return 8;
    }

    case Annotation::Shape::Line:
    case Annotation::Shape::Arrow:
        out[0] = a.p0();
        out[1] = a.p1();
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
            if (distance_to_segment(screen_pos, xform.to_screen(a.p0()), xform.to_screen(a.p1())) <= tol)
                return i;
            break;

        case Annotation::Shape::Freehand:
        {
            const auto path = annotation_path(a);

            // The box first: a scribble is mostly empty space, and walking hundreds of segments to answer
            // "nowhere near it" is the common case. Measured over the drawn path rather than the points,
            // since a curve through them bows outside the box they describe.
            float2 lo, hi;
            screen_extent(path, xform, lo, hi);
            if (distance_to_box(screen_pos, lo, hi) > tol)
                break;
            for (size_t k = 1; k < path.size(); ++k)
                if (distance_to_segment(screen_pos, xform.to_screen(path[k - 1]), xform.to_screen(path[k])) <= tol)
                    return i;
        }
        break;

        case Annotation::Shape::Text:
            // Only the anchor: the glyphs' extent depends on a font this cannot reach.
            if (length(xform.to_screen(a.p0()) - screen_pos) <= std::max(tol, a.font_size * 0.5f))
                return i;
            break;

        default: break;
        }
    }
    return -1;
}

std::vector<float2> annotation_path(const Annotation &a)
{
    if (!a.smooth || a.shape != Annotation::Shape::Freehand || a.points.size() < 2)
        return a.points;

    std::vector<float2> out;
    out.reserve((a.points.size() - 1) * k_curve_samples + 1);
    out.push_back(a.points.front());

    for (size_t i = 0; i + 1 < a.points.size(); ++i)
    {
        float2 b[4];
        catmull_rom_span(a.points, i, b);
        for (int k = 1; k <= k_curve_samples; ++k)
        {
            const float t = float(k) / float(k_curve_samples), u = 1.f - t;
            out.push_back(u * u * u * b[0] + 3.f * u * u * t * b[1] + 3.f * u * t * t * b[2] + t * t * t * b[3]);
        }
    }
    return out;
}

std::vector<float2> simplify_polyline(const std::vector<float2> &path, float tolerance)
{
    if (path.size() < 3 || tolerance <= 0.f)
        return path;

    // Iterative Ramer-Douglas-Peucker: keep the endpoints, keep whichever point between them strays
    // furthest if it strays far enough, and consider the two halves it makes in turn. Iterative rather
    // than recursive because a captured scribble can be thousands of points long.
    std::vector<bool>                      keep(path.size(), false);
    std::vector<std::pair<size_t, size_t>> spans{{0, path.size() - 1}};
    keep.front() = keep.back() = true;

    while (!spans.empty())
    {
        const auto [first, last] = spans.back();
        spans.pop_back();
        if (last <= first + 1)
            continue;

        size_t worst   = first;
        float  worst_d = 0.f;
        for (size_t i = first + 1; i < last; ++i)
            if (const float d = distance_to_segment(path[i], path[first], path[last]); d > worst_d)
            {
                worst   = i;
                worst_d = d;
            }

        if (worst_d > tolerance)
        {
            keep[worst] = true;
            spans.push_back({first, worst});
            spans.push_back({worst, last});
        }
    }

    std::vector<float2> out;
    out.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i)
        if (keep[i])
            out.push_back(path[i]);
    return out;
}

int move_annotation_handle(Annotation &a, int index, float2 to)
{
    if (a.shape == Annotation::Shape::Line || a.shape == Annotation::Shape::Arrow)
    {
        if (index == 0)
            a.p0() = to;
        else if (index == 1)
            a.p1() = to;
        return index; // an endpoint is an endpoint however the line is turned around
    }

    if (!Annotation::boxed(a.shape))
        return index;

    const Box2f before = a.bounds();
    float2      lo = before.min, hi = before.max;

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

    // A drag past the far side leaves lo above hi; ordering them here keeps the first point the low corner,
    // which is what every reader of a Rect or Ellipse assumes.
    const bool   flip_x = lo.x > hi.x, flip_y = lo.y > hi.y;
    const float2 to_lo{std::min(lo.x, hi.x), std::min(lo.y, hi.y)};
    const float2 to_hi{std::max(lo.x, hi.x), std::max(lo.y, hi.y)};

    // Every point moves with the box, which for a Rect or Ellipse -- whose two points are its corners -- is
    // the corner drag it always was, and for a scribble scales the whole path. An axis the box has no
    // extent along cannot be scaled, so those points go to the new edge instead.
    const float2 extent = before.max - before.min;
    for (auto &p : a.points)
    {
        const float2 t{extent.x > 0.f ? (p.x - before.min.x) / extent.x : 0.f,
                       extent.y > 0.f ? (p.y - before.min.y) / extent.y : 0.f};
        p = to_lo + t * (to_hi - to_lo);
    }

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
