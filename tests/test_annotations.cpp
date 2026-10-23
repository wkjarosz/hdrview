//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// User annotations, checked both as the command stream they flatten to and as the geometry that stream
// draws. Sweeps over every shape rather than testing one of them: the properties below hold for whatever
// shapes exist, so a shape added later is covered the moment it is registered.

#include <doctest/doctest.h>

#include "annotations.h"

#include "test_draw_list.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <set>
#include <string>
#include <vector>

using namespace test_draw;

namespace
{

/// One annotation of each shape, with values distinct enough that a mix-up shows.
Annotation sample(Annotation::Shape shape)
{
    Annotation a;
    a.shape        = shape;
    a.points       = {float2{10.f, 20.f}, float2{110.f, 70.f}};
    a.stroke_color = float4{1.f, 0.5f, 0.25f, 1.f};
    a.stroke_width = 4.f;
    a.font_size    = 24.f;
    a.text         = "label";
    if (shape == Annotation::Shape::Text)
        a.points = {a.p0()};

    // A scribble is a path, not a pair of corners: a zigzag across the same extent, so it spans exactly
    // what the other shapes do and the sweeps can hold it to the same properties.
    if (shape == Annotation::Shape::Freehand)
        a.points = {float2{10.f, 20.f}, float2{35.f, 70.f},  float2{60.f, 20.f},
                    float2{85.f, 70.f}, float2{110.f, 20.f}, float2{110.f, 70.f}};
    return a;
}

/// Every shape, so a sweep says so rather than listing them.
std::vector<Annotation::Shape> all_shapes()
{
    std::vector<Annotation::Shape> shapes;
    for (int i = 0; i < int(Annotation::Shape::COUNT); ++i) shapes.push_back(Annotation::Shape(i));
    return shapes;
}

/// Stands in for a font: a fixed box per character, proportional to the size, which is all a box needs.
float2 measure_stand_in(const std::string &, float size, const std::string &text)
{ return float2{0.5f * size * float(text.size()), size}; }

/// Image-to-screen mapping at \p s screen pixels per image pixel, as zooming the viewport gives.
/**
    It measures text with measure_stand_in(), so a Text annotation has a box, and handles, like every other
    shape.
*/
VgTransform scaled_transform(float s)
{
    VgTransform x;
    x.to_screen    = [s](float2 p) { return p * s; };
    x.scale        = s;
    x.measure_text = measure_stand_in;
    return x;
}

} // namespace

TEST_CASE("Every shape flattens to a well-formed command stream")
{
    // The wire format carries no per-command length, so a command's argument count is what makes the
    // stream parseable at all, and an arity mistake is the likeliest way to write a new shape wrongly.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        const std::vector<VgCommand> out = to_vg_commands({sample(shape)}, 1.f);

        REQUIRE(!out.empty());
        for (const auto &c : out)
        {
            CAPTURE(int(c.type));
            const int n = VgCommand::num_floats(c.type);
            REQUIRE(n >= 0); // never emits a command the interpreter does not know
            CHECK(int(c.data.size()) == n);
            CHECK(c.text.empty() == !VgCommand::has_text(c.type));
        }
    }
}

TEST_CASE("Every shape draws within the bounds it declares")
{
    // Ties bounds() to what is actually drawn. Catches the argument-order mistakes a command-level check
    // cannot see: Rect takes a corner and an extent, Ellipse a center and radii, and passing either one
    // the other way puts geometry somewhere bounds() never claimed.
    for (auto shape : all_shapes())
    {
        // Text's extent depends on a font, which bounds() has no access to and this harness has not loaded.
        if (shape == Annotation::Shape::Text)
            continue;

        CAPTURE(annotation_shape_name(shape));

        const Annotation a = sample(shape);
        TestDrawList     d;
        draw_vector_overlay(&d.list, to_vg_commands({a}, 1.f), identity_transform(), IM_COL32_WHITE);

        REQUIRE(d.list.VtxBuffer.Size > 0);
        const auto  b   = vertex_bounds(d.list);
        const Box2f box = a.bounds();

        // A stroke straddles the path, so the drawn extent reaches half a width past the geometry. The
        // slack beyond that is a single pixel, which is tight enough that a shape drawn at the wrong
        // place or the wrong size fails rather than hiding inside the tolerance.
        const float slack = a.stroke_width * 0.5f + 1.f;
        CHECK(b.min_x >= box.min.x - slack);
        CHECK(b.min_y >= box.min.y - slack);
        CHECK(b.max_x <= box.max.x + slack);
        CHECK(b.max_y <= box.max.y + slack);

        // ...and actually reaches the bounds, so a shape collapsed to a point cannot pass the test above.
        CHECK(b.min_x <= box.min.x + slack);
        CHECK(b.min_y <= box.min.y + slack);
        CHECK(b.max_x >= box.max.x - slack);
        CHECK(b.max_y >= box.max.y - slack);
    }
}

TEST_CASE("A stroke is a screen width, and a font size an image one, unless set otherwise")
{
    // The defaults differ on purpose: a stroke is how the markup is drawn, so it stays as easy to see however
    // far out the view is, while how large a string is on the image is its geometry, and shrinks with the
    // feature it labels like every other extent here. Either can be switched, and the command stream has to
    // carry whichever unit is set.
    const Annotation defaults;
    CHECK_FALSE(defaults.stroke_width_relative);
    CHECK(defaults.font_size_relative);

    auto kind = [](bool relative) { return int(relative ? VgCommand::Relative : VgCommand::Absolute); };

    for (auto shape : all_shapes())
        for (bool stroke_relative : {false, true})
            for (bool font_relative : {false, true})
            {
                CAPTURE(annotation_shape_name(shape));
                CAPTURE(stroke_relative);
                CAPTURE(font_relative);

                Annotation a            = sample(shape);
                a.stroke_width_relative = stroke_relative;
                a.font_size_relative    = font_relative;

                int widths = 0, sizes = 0;
                for (const auto &c : to_vg_commands({a}, 1.f))
                {
                    if (c.type == VgCommand::Type::StrokeWidth)
                    {
                        CHECK(c.data[0] == a.stroke_width);
                        CHECK(int(c.data[1]) == kind(stroke_relative));
                        ++widths;
                    }
                    if (c.type == VgCommand::Type::FontSize)
                    {
                        CHECK(c.data[0] == a.font_size);
                        CHECK(int(c.data[1]) == kind(font_relative));
                        ++sizes;
                    }
                }

                // Every shape says one or the other, so neither check above can pass by never running.
                CHECK((shape == Annotation::Shape::Text ? sizes : widths) > 0);
            }
}

TEST_CASE("An invisible annotation draws nothing, and does not disturb its neighbors")
{
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        Annotation hidden = sample(shape);
        hidden.visible    = false;

        CHECK(to_vg_commands({hidden}, 1.f).empty());

        // Hiding one annotation must leave the others exactly as they were: the state the interpreter
        // carries between commands makes "skipped" and "drawn transparently" different things.
        const Annotation visible = sample(Annotation::Shape::Rect);
        CHECK(to_vg_commands({hidden, visible}, 1.f).size() == to_vg_commands({visible}, 1.f).size());
    }
}

TEST_CASE("A filled shape draws more than the same shape unfilled")
{
    for (auto shape : all_shapes())
    {
        // Only the closed shapes have an interior to fill.
        if (shape != Annotation::Shape::Rect && shape != Annotation::Shape::Ellipse)
            continue;

        CAPTURE(annotation_shape_name(shape));

        Annotation unfilled = sample(shape);
        Annotation filled   = unfilled;
        filled.fill_color   = float4{0.f, 0.f, 1.f, 1.f};

        TestDrawList a, b;
        draw_vector_overlay(&a.list, to_vg_commands({unfilled}, 1.f), identity_transform(), IM_COL32_WHITE);
        draw_vector_overlay(&b.list, to_vg_commands({filled}, 1.f), identity_transform(), IM_COL32_WHITE);

        CHECK(b.list.VtxBuffer.Size > a.list.VtxBuffer.Size);
    }
}

TEST_CASE("An arrow's head is proportioned in the unit its stroke is measured in")
{
    // The head is a multiple of the stroke, expressed in image coordinates whatever the stroke is measured
    // in, so it is where the zoom is most easily dropped or applied twice -- and it has to follow the
    // stroke's own unit, growing on screen with the image only when the stroke does.
    auto head_width_at = [](float scale, bool relative)
    {
        Annotation a            = sample(Annotation::Shape::Arrow);
        a.stroke_width_relative = relative;
        a.p0()                  = float2{0.f, 0.f};
        a.p1()                  = float2{1000.f, 0.f}; // long, so the head is never clamped by the shaft

        TestDrawList d;
        VgTransform  x = identity_transform();
        x.scale        = scale;
        x.to_screen    = [scale](float2 p) { return p * scale; };
        draw_vector_overlay(&d.list, to_vg_commands({a}, scale), x, IM_COL32_WHITE);

        // The arrow lies along y = 0, so everything off that line is the head or the stroke around it.
        const auto b = vertex_bounds(d.list);
        return b.max_y - b.min_y;
    };

    // A screen-pixel stroke keeps its head the same size on screen however far the view is zoomed. Were
    // the zoom dropped, the head would grow with the image; were it applied twice, it would shrink.
    CHECK(head_width_at(2.f, false) == doctest::Approx(head_width_at(1.f, false)).epsilon(0.02f));
    CHECK(head_width_at(4.f, false) == doctest::Approx(head_width_at(1.f, false)).epsilon(0.02f));

    // An image-pixel stroke draws a head that grows with the image, in step with the shaft it ends.
    CHECK(head_width_at(2.f, true) == doctest::Approx(2.f * head_width_at(1.f, true)).epsilon(0.05f));
    CHECK(head_width_at(4.f, true) == doctest::Approx(4.f * head_width_at(1.f, true)).epsilon(0.05f));

    // And the head is genuinely wider than the shaft, so the checks above are measuring one.
    Annotation shaft_only = sample(Annotation::Shape::Line);
    shaft_only.p0()       = float2{0.f, 0.f};
    shaft_only.p1()       = float2{1000.f, 0.f};
    TestDrawList d;
    draw_vector_overlay(&d.list, to_vg_commands({shaft_only}, 1.f), identity_transform(), IM_COL32_WHITE);
    const auto sb = vertex_bounds(d.list);
    CHECK(head_width_at(1.f, false) > (sb.max_y - sb.min_y) * 1.5f);
}

TEST_CASE("Every shape reports a label without being given one")
{
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        Annotation a = sample(shape);
        a.label.clear();
        CHECK(!a.display_label().empty());

        a.label = "my label";
        CHECK(a.display_label() == "my label");
    }
}

namespace
{

/// A point on \p a's outline from which straight up, toward smaller y, leads away from the shape.
float2 point_on_outline(const Annotation &a)
{
    float2 h[Annotation::MaxHandles];
    annotation_handles(a, h);
    switch (a.shape)
    {
    // The midpoint of the top edge of the bounding box: on a rectangle's side, and on an ellipse's extreme,
    // which is where the box touches it.
    case Annotation::Shape::Rect:
    case Annotation::Shape::Ellipse: return h[4];
    case Annotation::Shape::Line:
    case Annotation::Shape::Arrow: return (h[0] + h[1]) * 0.5f;
    // A scribble's first point, at the top of its box, and the anchor at the top of a string's.
    default: return a.p0();
    }
}

constexpr float k_slop = 4.f;

/// Which side of a boxed shape's extent its handle \p i moves along each axis: -1 the low, 1 the high, 0 neither.
/**
    In the order annotation_handles() lays them out: the corners (lo, hi.x/lo.y, hi, lo.x/hi.y), then the
    midpoints of the top, right, bottom and left edges.
*/
int2 held_side(int i)
{
    static constexpr int x[8] = {-1, 1, 1, -1, 0, 1, 0, -1};
    static constexpr int y[8] = {-1, -1, 1, 1, -1, 0, 1, 0};
    return int2{x[i], y[i]};
}

} // namespace

TEST_CASE("Every shape is picked up within its tolerance and not beyond, unless it cannot be taken hold of")
{
    // The slop and the stroke straddling the outline widen every shape, both in screen pixels, so the same
    // miss by a few of them lands the same way however far the image is zoomed; a stroke measured in image
    // pixels widens it with the zoom. Later annotations draw over earlier ones, so the topmost is the one
    // picked up, and one hidden or locked is passed over without shielding what is behind it.
    for (auto shape : all_shapes())
        for (bool stroke_relative : {false, true})
            for (float scale : {0.25f, 1.f, 8.f})
            {
                CAPTURE(annotation_shape_name(shape));
                CAPTURE(stroke_relative);
                CAPTURE(scale);

                Annotation a            = sample(shape);
                a.stroke_width_relative = stroke_relative;
                const auto   x          = scaled_transform(scale);
                const float  tol        = k_slop + 0.5f * a.stroke_width * (stroke_relative ? scale : 1.f);
                const float2 on         = x.to_screen(point_on_outline(a));
                const float2 inside     = on - float2{0.f, tol - 1.f};
                const float2 beyond     = on - float2{0.f, 4.f * tol};

                CHECK(annotation_at({a}, inside, x, k_slop) == 0);
                CHECK(annotation_at({a}, beyond, x, k_slop) == -1);
                CHECK(annotation_at({a, a}, inside, x, k_slop) == 1);

                Annotation hidden = a, locked = a;
                hidden.visible = false;
                locked.locked  = true;
                for (const Annotation &out_of_reach : {hidden, locked})
                {
                    CHECK(annotation_at({out_of_reach}, inside, x, k_slop) == -1);
                    CHECK(annotation_at({a, out_of_reach}, inside, x, k_slop) == 0);
                }
            }
}

TEST_CASE("A shape is hit through its middle only once it is filled")
{
    // Otherwise an unfilled outline would swallow every click inside it, and the shape behind it could
    // never be reached.
    for (auto shape : all_shapes())
    {
        // The shapes with an interior to speak of; a line's middle is the line itself.
        if (shape != Annotation::Shape::Rect && shape != Annotation::Shape::Ellipse)
            continue;

        CAPTURE(annotation_shape_name(shape));

        Annotation   a      = sample(shape);
        const float2 middle = (a.p0() + a.p1()) * 0.5f;
        const auto   x      = identity_transform();

        CHECK(annotation_at({a}, middle, x, k_slop) == -1);

        a.fill_color = float4{0.f, 0.f, 1.f, 1.f};
        CHECK(annotation_at({a}, middle, x, k_slop) == 0);
    }
}

TEST_CASE("Every handle can be grabbed anywhere it is drawn, at any zoom")
{
    // Hit testing and drawing read the same list, so this checks that the transform is applied the same way in
    // both directions, the one place a handle can drift from the shape it belongs to. A handle is drawn as a
    // square of the hit radius, so the square is what has to answer: measured as a radius, a click on one of
    // its corners -- which is where a corner handle is aimed at -- would miss, and the press fall through to
    // whatever is under it.
    constexpr float radius = 6.f;
    const float     e = radius - 0.5f, o = radius + 0.5f; //< just inside the square, and just outside it

    for (auto shape : all_shapes())
        for (float scale : {0.5f, 1.f, 4.f})
        {
            CAPTURE(annotation_shape_name(shape));
            CAPTURE(scale);

            const Annotation a = sample(shape);
            const auto       x = scaled_transform(scale);

            float2    h[Annotation::MaxHandles];
            const int count = annotation_handles(a, h, &x);
            CHECK(count == (shape == Annotation::Shape::Text                                        ? 4
                            : shape == Annotation::Shape::Line || shape == Annotation::Shape::Arrow ? 2
                                                                                                    : 8));

            for (int i = 0; i < count; ++i)
            {
                CAPTURE(i);
                const float2 at = x.to_screen(h[i]);
                for (float2 inside : {float2{0.f, 0.f}, float2{-e, -e}, float2{e, -e}, float2{e, e}, float2{-e, e}})
                {
                    CAPTURE(inside.x);
                    CAPTURE(inside.y);
                    CHECK(handle_at(a, at + inside, x, radius) == i);
                }
                // past each side the square ends, though a neighbor's may begin there
                for (float2 outside : {float2{-o, 0.f}, float2{o, 0.f}, float2{0.f, -o}, float2{0.f, o}})
                {
                    CAPTURE(outside.x);
                    CAPTURE(outside.y);
                    CHECK(handle_at(a, at + outside, x, radius) != i);
                }
            }

            // A point well away from every handle finds none, so the checks above are not simply always
            // returning a handle.
            CHECK(handle_at(a, x.to_screen(float2{500.f, 500.f}), x, radius) == -1);
        }
}

TEST_CASE("A dragged handle follows the cursor, and the side it pivots about stays put")
{
    // Corners and endpoints take both of the cursor's coordinates; an edge midpoint takes only the one across
    // its edge, and stays centered along it. A resize pivots about the side not being held, which stays
    // where it was even once the drag carries the handle past it and turns the shape inside out. That
    // renumbers the handles, so each step continues from the index the last one returned, as a drag held
    // through the crossing does.
    const auto x = scaled_transform(1.f);

    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        const Annotation before = sample(shape);
        float2           h[Annotation::MaxHandles];
        const int        count = annotation_handles(before, h, &x);
        REQUIRE(count > 0);

        for (int i = 0; i < count; ++i)
        {
            CAPTURE(i);

            // a small move, crossing nothing; and out past the far side, back through it, and out the other way
            const std::vector<std::vector<float2>> drags{
                {h[i] + float2{5.f, 3.f}}, {float2{300.f, 250.f}, float2{-200.f, -150.f}, float2{60.f, 45.f}}};

            for (const auto &drag : drags)
            {
                Annotation a     = before;
                int        index = i;
                for (float2 to : drag)
                {
                    CAPTURE(to.x);
                    CAPTURE(to.y);

                    // Glyphs cannot be stretched, so a string's box keeps its aspect, its height following the
                    // cursor and the corner opposite the handle staying put; the box then lies on whichever
                    // side of that corner the cursor is, and the corner held is the one that names.
                    if (shape == Annotation::Shape::Text)
                    {
                        float2 lo0, extent0;
                        REQUIRE(text_extent(before, x, lo0, extent0));
                        const float2 fixed{i == 0 || i == 3 ? lo0.x + extent0.x : lo0.x,
                                           i == 0 || i == 1 ? lo0.y + extent0.y : lo0.y};

                        index = move_annotation_handle(a, index, to, &x);
                        REQUIRE(index >= 0);
                        REQUIRE(index < 4);

                        float2 lo, extent;
                        REQUIRE(text_extent(a, x, lo, extent));
                        const float ratio = std::abs(to.y - fixed.y) / extent0.y;
                        CHECK(extent.y == doctest::Approx(std::abs(to.y - fixed.y)));
                        CHECK(extent.x == doctest::Approx(extent0.x * ratio));
                        CHECK(a.font_size == doctest::Approx(before.font_size * ratio));
                        CHECK(lo.x == doctest::Approx(to.x >= fixed.x ? fixed.x : fixed.x - extent.x));
                        CHECK(lo.y == doctest::Approx(to.y >= fixed.y ? fixed.y : fixed.y - extent.y));

                        const int2 side = held_side(index);
                        CHECK((side.x < 0 ? lo.x + extent.x : lo.x) == doctest::Approx(fixed.x));
                        CHECK((side.y < 0 ? lo.y + extent.y : lo.y) == doctest::Approx(fixed.y));
                        continue;
                    }

                    index = move_annotation_handle(a, index, to, &x);
                    REQUIRE(index >= 0);
                    REQUIRE(index < count);

                    float2 after[Annotation::MaxHandles];
                    REQUIRE(annotation_handles(a, after, &x) == count);

                    if (!Annotation::boxed(shape))
                    {
                        // an endpoint is an endpoint however the line is turned around, and the other one stays
                        CHECK(index == i);
                        CHECK(after[i].x == doctest::Approx(to.x));
                        CHECK(after[i].y == doctest::Approx(to.y));
                        CHECK(after[1 - i].x == doctest::Approx(h[1 - i].x));
                        CHECK(after[1 - i].y == doctest::Approx(h[1 - i].y));
                        continue;
                    }

                    // the handle the returned index names is at the cursor along each axis it is free on, and
                    // where it started along the other
                    const int2 side = held_side(i);
                    CHECK(after[index].x == doctest::Approx(side.x ? to.x : h[i].x));
                    CHECK(after[index].y == doctest::Approx(side.y ? to.y : h[i].y));

                    // the box spans from the side not held to the cursor, and an axis not held keeps both sides
                    const Box2f box0 = before.bounds(), box = a.bounds();
                    for (int k = 0; k < 2; ++k)
                    {
                        CAPTURE(k);
                        const float pivot = side[k] < 0 ? box0.max[k] : box0.min[k];
                        CHECK(box.min[k] == doctest::Approx(side[k] ? std::min(pivot, to[k]) : box0.min[k]));
                        CHECK(box.max[k] == doctest::Approx(side[k] ? std::max(pivot, to[k]) : box0.max[k]));
                    }

                    // Rect and Ellipse come back ordered, so everything downstream can read p0 as the low
                    // corner; a scribble's points are its path, which a flip mirrors.
                    if (shape != Annotation::Shape::Freehand)
                    {
                        CHECK(a.p0().x <= a.p1().x);
                        CHECK(a.p0().y <= a.p1().y);
                    }
                }
            }
        }
    }
}

namespace
{

/// Every field of an annotation, so a field left out of the serializer shows up as a difference.
bool same(const Annotation &a, const Annotation &b)
{
    return a.shape == b.shape && a.points == b.points && a.stroke_color == b.stroke_color &&
           a.fill_color == b.fill_color && a.stroke_width == b.stroke_width && a.font_size == b.font_size &&
           a.font_face == b.font_face && a.text_align == b.text_align && a.text == b.text && a.label == b.label &&
           a.smooth == b.smooth && a.visible == b.visible && a.locked == b.locked &&
           a.stroke_width_relative == b.stroke_width_relative && a.font_size_relative == b.font_size_relative;
}

} // namespace

TEST_CASE("Every shape survives a trip through a session file")
{
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        // Every field set away from its default, so a field the serializer forgets comes back as that
        // default and fails here rather than round-tripping by accident.
        Annotation a            = sample(shape);
        a.fill_color            = float4{0.25f, 0.5f, 0.75f, 0.5f};
        a.text_align            = VgCommand::AlignRight | VgCommand::AlignBottom;
        a.label                 = "a label of its own";
        a.visible               = false;
        a.locked                = true;
        a.stroke_width          = 7.5f;
        a.font_size             = 31.f;
        a.font_face             = "mono-bold";
        a.smooth                = true;
        a.stroke_width_relative = true;
        a.font_size_relative    = false;

        const Annotation b = json(a).get<Annotation>();
        CHECK(same(a, b));

        // Each field really is away from its default, so the check above is not comparing two defaults.
        CHECK(!same(a, Annotation{}));
    }
}

TEST_CASE("A session that says nothing about a field reads it as the default")
{
    // What an older session file looks like: the shape, and nothing else. Anything missing has to come
    // back as a freshly constructed annotation's value rather than as whatever was in the variable.
    json j     = json::object();
    j["shape"] = "ellipse";

    Annotation       a = j.get<Annotation>();
    const Annotation d;

    CHECK(a.shape == Annotation::Shape::Ellipse);
    CHECK(a.stroke_color == d.stroke_color);
    CHECK(a.stroke_width == d.stroke_width);
    CHECK(a.visible == d.visible);
    CHECK(a.locked == d.locked);
    CHECK(a.text.empty());
    CHECK(a.label.empty());
}

TEST_CASE("An unknown shape name reads as a shape rather than as nothing")
{
    // A session written by a newer HDRView can name a shape this one does not have. Falling back to the
    // default keeps the rest of the annotation, which is better than dropping it or reading past the table.
    json j     = json::object();
    j["shape"] = "no-such-shape";
    j["width"] = 5.f;

    const Annotation a = j.get<Annotation>();
    CHECK(int(a.shape) >= 0);
    CHECK(int(a.shape) < int(Annotation::Shape::COUNT));
    CHECK(a.stroke_width == doctest::Approx(5.f));
}

TEST_CASE("Simplifying a scribble keeps its shape")
{
    // The property that matters is not how many points survive but how far the survivors stray from the
    // path: every dropped point has to still lie within the tolerance of what is left.
    auto distance_to_path = [](float2 p, const std::vector<float2> &path)
    {
        float best = FLT_MAX;
        for (size_t i = 1; i < path.size(); ++i)
        {
            const float2 a = path[i - 1], b = path[i];
            const float2 ab = b - a;
            const float  l2 = dot(ab, ab);
            const float  t  = l2 > 0.f ? std::clamp(dot(p - a, ab) / l2, 0.f, 1.f) : 0.f;
            best            = std::min(best, length(p - (a + ab * t)));
        }
        return best;
    };

    // A wobble along a diagonal: the wobble is what a tolerance either keeps or drops.
    std::vector<float2> path;
    for (int i = 0; i <= 200; ++i) path.push_back(float2{float(i), float(i) + ((i % 2) ? 0.4f : -0.4f)});

    for (float tolerance : {0.1f, 1.f, 5.f})
    {
        CAPTURE(tolerance);
        const auto simplified = simplify_polyline(path, tolerance);

        REQUIRE(simplified.size() >= 2);
        CHECK(simplified.front() == path.front()); // the ends are never dropped
        CHECK(simplified.back() == path.back());
        CHECK(simplified.size() <= path.size());

        for (const auto &p : path)
        {
            CAPTURE(p.x);
            CHECK(distance_to_path(p, simplified) <= tolerance + 1e-3f);
        }
    }

    // A tolerance past the wobble's size collapses it to the diagonal it wobbles along...
    CHECK(simplify_polyline(path, 5.f).size() < 10);
    // ...while one below it cannot, so the check above is measuring the tolerance and not just shrinkage.
    CHECK(simplify_polyline(path, 0.1f).size() > 100);

    // Nothing to simplify is left alone rather than mangled.
    CHECK(simplify_polyline(path, 0.f).size() == path.size());
    CHECK(simplify_polyline({float2{0.f, 0.f}, float2{1.f, 1.f}}, 10.f).size() == 2);
}

TEST_CASE("A scribble is picked up on its path, not merely inside its box")
{
    // The box is only a first pass. A click in the gap between two strokes is inside the box and must
    // still miss, or a scribble would swallow every click across the rectangle it happens to span.
    Annotation a = sample(Annotation::Shape::Freehand);
    const auto x = identity_transform();

    // On the path.
    CHECK(annotation_at({a}, a.points[1], x, k_slop) == 0);

    // Well inside the box but in the trough between two of the zigzag's legs: above the low vertex at
    // points[1], and between the two peaks either side of it.
    const Box2f  box = a.bounds();
    const float2 gap{a.points[1].x, box.min.y + 5.f};
    REQUIRE(gap.x > box.min.x);
    REQUIRE(gap.x < box.max.x);
    REQUIRE(gap.y < box.max.y);
    CHECK(annotation_at({a}, gap, x, k_slop) == -1);

    // Just outside the box, but within the tolerance of a stroke that reaches the edge. The box is only a
    // rejection test, so it has to be widened by the same tolerance the segments are, or a click a pixel
    // outside a scribble that touches its own bounding box would miss what it is plainly on.
    CHECK(annotation_at({a}, a.points.front() - float2{3.f, 0.f}, x, k_slop) == 0);

    // Outside the box entirely.
    CHECK(annotation_at({a}, float2{500.f, 500.f}, x, k_slop) == -1);
}

TEST_CASE("Resizing a scribble carries its whole path")
{
    // A scribble is resized by the box around it, so every point has to move with that box -- not just the
    // two the other shapes are described by.
    Annotation       a      = sample(Annotation::Shape::Freehand);
    const Annotation before = a;
    const Box2f      box0   = a.bounds();

    // Drag the low corner out, doubling the box in both directions.
    move_annotation_handle(a, 0, box0.min - (box0.max - box0.min));

    REQUIRE(a.points.size() == before.points.size());
    const Box2f box1 = a.bounds();

    // Every point is still inside the box it belongs to, and where it was within it.
    for (size_t i = 0; i < a.points.size(); ++i)
    {
        CAPTURE(i);
        const float2 t0 = (before.points[i] - box0.min) / (box0.max - box0.min);
        const float2 t1 = (a.points[i] - box1.min) / (box1.max - box1.min);
        CHECK(t1.x == doctest::Approx(t0.x).epsilon(1e-4));
        CHECK(t1.y == doctest::Approx(t0.y).epsilon(1e-4));
    }

    // And the box really did change, so the check above is not comparing a shape with itself.
    CHECK(box1.max.x - box1.min.x == doctest::Approx(2.f * (box0.max.x - box0.min.x)));
}

TEST_CASE("A smoothed scribble is a curve through the stroke that was drawn")
{
    // Three properties together, because no one of them is enough: a curve can pass through every point
    // and still leap between them, or run smoothly and still bulge far away from what was drawn.
    Annotation a = sample(Annotation::Shape::Freehand);
    a.smooth     = true;

    const auto path = annotation_path(a);
    REQUIRE(path.size() > a.points.size()); // it really was sampled, not handed back unchanged

    float longest = 0.f;
    for (size_t i = 1; i < a.points.size(); ++i) longest = std::max(longest, length(a.points[i] - a.points[i - 1]));
    REQUIRE(longest > 0.f);

    // It interpolates: every point drawn is a point on the curve, which is what separates a curve through
    // the stroke from one merely near it.
    for (const auto &p : a.points)
    {
        CAPTURE(p.x);
        float best = FLT_MAX;
        for (const auto &q : path) best = std::min(best, length(q - p));
        CHECK(best < 1e-3f);
    }

    // The ends are the ends, so it does not start or finish somewhere the stroke never went.
    CHECK(length(path.front() - a.points.front()) < 1e-4f);
    CHECK(length(path.back() - a.points.back()) < 1e-4f);

    // It is continuous: the samples walk along one curve rather than jumping between pieces of several.
    // A span sampled evenly steps a fraction of its own length at a time.
    for (size_t i = 1; i < path.size(); ++i)
    {
        CAPTURE(i);
        CHECK(length(path[i] - path[i - 1]) <= 0.25f * longest);
    }

    // And it stays close to the stroke: a curve free to bow arbitrarily far from the points is no longer
    // a smoothing of what was drawn. Well clear of where this sits, and well clear of twice it.
    for (const auto &q : path)
    {
        float best = FLT_MAX;
        for (size_t i = 1; i < a.points.size(); ++i)
        {
            const float2 s = a.points[i - 1], e = a.points[i], se = e - s;
            const float  l2 = dot(se, se);
            const float  t  = l2 > 0.f ? std::clamp(dot(q - s, se) / l2, 0.f, 1.f) : 0.f;
            best            = std::min(best, length(q - (s + se * t)));
        }
        CHECK(best <= 0.09f * longest);
    }
}

TEST_CASE("Smoothing changes how a scribble is drawn, not what it is")
{
    Annotation straight = sample(Annotation::Shape::Freehand);
    Annotation curved   = straight;
    curved.smooth       = true;

    // Only a scribble has a path to curve; the flag is inert on everything else.
    for (auto shape : all_shapes())
    {
        if (shape == Annotation::Shape::Freehand)
            continue;

        CAPTURE(annotation_shape_name(shape));
        Annotation a = sample(shape), b = a;
        b.smooth = true;
        CHECK(to_vg_commands({a}, 1.f).size() == to_vg_commands({b}, 1.f).size());
        CHECK(annotation_path(a) == annotation_path(b));
    }

    // A curve is emitted as cubics, one per span, rather than as the straight segments of the polyline.
    const auto flat   = to_vg_commands({straight}, 1.f);
    const auto bezier = to_vg_commands({curved}, 1.f);
    auto       count  = [](const std::vector<VgCommand> &cmds, VgCommand::Type t)
    {
        int n = 0;
        for (const auto &c : cmds)
            if (c.type == t)
                ++n;
        return n;
    };
    CHECK(count(flat, VgCommand::Type::LineTo) == int(straight.points.size()) - 1);
    CHECK(count(flat, VgCommand::Type::BezierTo) == 0);
    CHECK(count(bezier, VgCommand::Type::BezierTo) == int(curved.points.size()) - 1);
    CHECK(count(bezier, VgCommand::Type::LineTo) == 0);
}

TEST_CASE("A smoothed scribble is picked up on the curve, not on the polyline")
{
    // Hit testing reads the same path the drawing does, so a curve that bows away from the straight line
    // between two points is caught where it is drawn rather than where the polyline would have been.
    Annotation a   = sample(Annotation::Shape::Freehand);
    a.smooth       = true;
    a.stroke_width = 1.f; // a thin stroke, so the tolerance cannot paper over a mismatch

    const auto x    = identity_transform();
    const auto path = annotation_path(a);

    // Every point of the drawn curve is on the annotation.
    for (size_t i = 0; i < path.size(); i += 3)
    {
        CAPTURE(i);
        CHECK(annotation_at({a}, path[i], x, 1.f) == 0);
    }
}

TEST_CASE("A text annotation is picked up over its glyphs, not just at its anchor")
{
    // The anchor is where a string starts, not where it is, so hit testing has to measure it. The measure
    // here stands in for a font: a fixed box per character, which is all the property needs.
    Annotation a = sample(Annotation::Shape::Text);
    a.text       = "a caption";
    a.font_size  = 20.f;
    a.points     = {float2{100.f, 100.f}};

    const float2 glyph{10.f, 20.f};
    auto         x = identity_transform();
    x.measure_text = [glyph](const std::string &, float, const std::string &text)
    { return float2{glyph.x * float(text.size()), glyph.y}; };

    const float2 extent = float2{glyph.x * float(a.text.size()), glyph.y};

    for (int align : {VgCommand::AlignLeft | VgCommand::AlignTop, VgCommand::AlignCenter | VgCommand::AlignMiddle,
                      VgCommand::AlignRight | VgCommand::AlignBottom})
    {
        CAPTURE(align);
        a.text_align = align;

        // Wherever the alignment puts the box, its middle is on the annotation and a point well outside
        // it is not.
        const float2 lo     = aligned_text_pos(a.p0(), extent, align);
        const float2 middle = lo + extent * 0.5f;
        CHECK(annotation_at({a}, middle, x, k_slop) == 0);
        CHECK(annotation_at({a}, lo + extent + float2{50.f, 50.f}, x, k_slop) == -1);

        // The far end of a long string is still on it, which the anchor alone would have missed.
        CHECK(annotation_at({a}, lo + float2{extent.x - 1.f, extent.y * 0.5f}, x, k_slop) == 0);
    }

    // With nothing to measure with, the anchor is all there is, and it still answers.
    auto blind = identity_transform();
    CHECK(annotation_at({a}, a.p0(), blind, k_slop) == 0);
}

TEST_CASE("Resizing a text annotation's box scales the size it is drawn at")
{
    // Glyphs cannot be stretched, so a box drag has to come out as a font size. Zoomed, not identity: the box
    // is an image quantity and a font size may be a screen one, so a transform that scales by one would hide
    // the conversion between them entirely.
    constexpr float zoom = 2.f;
    const auto      x    = scaled_transform(zoom);

    for (bool relative : {true, false})
    {
        CAPTURE(relative);

        Annotation a         = sample(Annotation::Shape::Text);
        a.text               = "caption";
        a.font_size          = 20.f;
        a.font_size_relative = relative;
        a.points             = {float2{100.f, 100.f}};
        a.text_align         = VgCommand::AlignLeft | VgCommand::AlignTop;

        float2    handles[Annotation::MaxHandles];
        const int count = annotation_handles(a, handles, &x);
        REQUIRE(count == 4); // the corners; an edge would only stretch it

        // Without something to measure with there is no box, so there is nothing to take hold of.
        CHECK(annotation_handles(a, handles, nullptr) == 0);

        // A size in image pixels draws a box of that height on the image, which grows on screen with the
        // zoom and so shrinks with the image rather than floating over it; one in screen pixels draws a box
        // of that height on screen, whatever the zoom.
        for (float z : {0.25f, 1.f, zoom, 8.f})
        {
            CAPTURE(z);
            float2 lo_z, extent_z;
            REQUIRE(text_extent(a, scaled_transform(z), lo_z, extent_z));
            const float screen_height = a.font_size * (relative ? z : 1.f);
            CHECK(extent_z.y * z == doctest::Approx(screen_height));
            CHECK(extent_z.x * z == doctest::Approx(0.5f * screen_height * float(a.text.size())));
        }

        float2 lo, extent;
        REQUIRE(text_extent(a, x, lo, extent));

        // Drag the low corner up and left so the box doubles in height; the size doubles with it.
        const float2 fixed = lo + extent;
        move_annotation_handle(a, 0, float2{lo.x, fixed.y - 2.f * extent.y}, &x);
        CHECK(a.font_size == doctest::Approx(40.f));

        // The corner opposite the one dragged stayed where it was.
        float2 lo2, extent2;
        REQUIRE(text_extent(a, x, lo2, extent2));
        CHECK((lo2 + extent2).x == doctest::Approx(fixed.x));
        CHECK((lo2 + extent2).y == doctest::Approx(fixed.y));

        // Shrinking works the same way round.
        move_annotation_handle(a, 0, float2{lo2.x, (lo2 + extent2).y - 0.5f * extent2.y}, &x);
        CHECK(a.font_size == doctest::Approx(20.f));
    }
}

TEST_CASE("Text is baked at a handful of sizes, and at the same one however far it is zoomed")
{
    // Dear ImGui rasterizes a font into its atlas at each size it is asked for, so the size it is asked for
    // is quantized and whatever is left over scales the glyphs instead. Two properties matter: the sizes
    // are few and bounded, and the zoom is not one of the things that picks them -- were it, a view being
    // zoomed would rasterize a fresh set every frame and the text would step as it crossed between them.
    std::set<float> baked_sizes;

    for (float size = 0.1f; size < 20000.f; size *= 1.05f)
    {
        CAPTURE(size);

        const float baked = text_baked_size(size);
        baked_sizes.insert(baked);

        // Never rasterized larger than the cap, which is what stops the cost growing with the size.
        CHECK(baked <= 128.f);
        CHECK(baked >= 1.f);

        // Big enough to hold the size it stands in for, until the cap, so text is scaled down rather than
        // up wherever that is possible.
        if (size <= 128.f)
            CHECK(baked >= std::max(1.f, size));
    }

    // A handful, not one per size: powers of two from 1 to the cap.
    CHECK(baked_sizes.size() <= 8);
    CHECK(baked_sizes.size() > 1);
}

// That the zoom is not one of the things picking a baked size is what the box test above checks, by way of
// the box following the zoom only as its unit says: nothing here takes a zoom to be independent of.
