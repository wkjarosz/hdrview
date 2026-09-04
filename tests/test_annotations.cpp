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

} // namespace

TEST_CASE("Every shape flattens to a well-formed command stream")
{
    // The wire format carries no per-command length, so a command's argument count is what makes the
    // stream parseable at all -- and an arity mistake is the likeliest way to write a new shape wrongly.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        std::vector<VgCommand> out;
        append_vg_commands(out, sample(shape), 1.f);

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
    // cannot see -- Rect takes a corner and an extent, Ellipse a center and radii, and passing either one
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

TEST_CASE("A stroke is a screen width, and a font size an image one")
{
    // The two are measured differently on purpose: a stroke is how the markup is drawn, so it stays as easy
    // to see however far out the view is, while how large a string is on the image is its geometry, and
    // shrinks with the feature it labels like every other extent here.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        std::vector<VgCommand> out;
        append_vg_commands(out, sample(shape), 1.f);

        int widths = 0, sizes = 0;
        for (const auto &c : out)
        {
            if (c.type == VgCommand::Type::StrokeWidth)
            {
                CHECK(int(c.data[1]) == int(VgCommand::Absolute));
                ++widths;
            }
            if (c.type == VgCommand::Type::FontSize)
            {
                CHECK(int(c.data[1]) == int(VgCommand::Relative));
                ++sizes;
            }
        }

        // Every shape says one or the other, so neither check above can pass by never running.
        CHECK(widths + sizes > 0);
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

        // Hiding one annotation must leave the others exactly as they were -- the state the interpreter
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

TEST_CASE("An arrow's head is proportioned in screen pixels")
{
    // The head is the one piece of geometry that has to be expressed in image coordinates while being
    // sized like a screen quantity, so it is the one place the scale can be dropped or applied twice.
    auto head_width_at = [](float scale)
    {
        Annotation a = sample(Annotation::Shape::Arrow);
        a.p0()       = float2{0.f, 0.f};
        a.p1()       = float2{1000.f, 0.f}; // long, so the head is never clamped by the shaft

        TestDrawList d;
        VgTransform  x = identity_transform();
        x.scale        = scale;
        x.to_screen    = [scale](float2 p) { return p * scale; };
        draw_vector_overlay(&d.list, to_vg_commands({a}, scale), x, IM_COL32_WHITE);

        // The arrow lies along y = 0, so everything off that line is the head or the stroke around it.
        const auto b = vertex_bounds(d.list);
        return b.max_y - b.min_y;
    };

    // Doubling the zoom must leave the head the same size on screen. Were the scale dropped, the head
    // would double along with the image; were it applied twice, it would halve.
    CHECK(head_width_at(2.f) == doctest::Approx(head_width_at(1.f)).epsilon(0.02f));
    CHECK(head_width_at(4.f) == doctest::Approx(head_width_at(1.f)).epsilon(0.02f));

    // And the head is genuinely wider than the shaft, so the check above is measuring one.
    Annotation shaft_only = sample(Annotation::Shape::Line);
    shaft_only.p0()       = float2{0.f, 0.f};
    shaft_only.p1()       = float2{1000.f, 0.f};
    TestDrawList d;
    draw_vector_overlay(&d.list, to_vg_commands({shaft_only}, 1.f), identity_transform(), IM_COL32_WHITE);
    const auto sb = vertex_bounds(d.list);
    CHECK(head_width_at(1.f) > (sb.max_y - sb.min_y) * 1.5f);
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

/// Image-to-screen mapping at \p s screen pixels per image pixel, as zooming the viewport gives.
VgTransform scaled_transform(float s)
{
    VgTransform x;
    x.to_screen = [s](float2 p) { return p * s; };
    x.scale     = s;
    return x;
}

/// A point on \p a's outline. Which point that is differs by shape, which is the whole of the difference.
float2 point_on_outline(const Annotation &a)
{
    float2    h[Annotation::MaxHandles];
    const int n = annotation_handles(a, h);
    switch (a.shape)
    {
    // The midpoint of a bounding-box edge: on a rectangle's side, and on an ellipse's extreme, which is
    // where the box touches it.
    case Annotation::Shape::Rect:
    case Annotation::Shape::Ellipse: return h[4];
    case Annotation::Shape::Line:
    case Annotation::Shape::Arrow: return (h[0] + h[1]) * 0.5f;
    default: return a.p0(); // Text is only ever its anchor
    }
    (void)n;
}

constexpr float k_slop = 4.f;

} // namespace

TEST_CASE("Every shape is picked up on its outline, and not far from it")
{
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        const Annotation a = sample(shape);
        const auto       x = identity_transform();

        CHECK(annotation_at({a}, point_on_outline(a), x, k_slop) == 0);

        // Far outside anything the fixture spans.
        CHECK(annotation_at({a}, float2{500.f, 500.f}, x, k_slop) == -1);
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

TEST_CASE("Every handle is found where it is drawn")
{
    // Hit testing and drawing read the same list, so this is really checking that the transform is applied
    // the same way in both directions -- the one place a handle can drift from the shape it belongs to.
    constexpr float radius = 6.f;

    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        const Annotation a = sample(shape);
        for (float scale : {0.5f, 1.f, 4.f})
        {
            CAPTURE(scale);
            const auto x = scaled_transform(scale);

            float2    h[Annotation::MaxHandles];
            const int count = annotation_handles(a, h);
            CHECK(count == (shape == Annotation::Shape::Text    ? 0
                            : shape == Annotation::Shape::Line  ? 2
                            : shape == Annotation::Shape::Arrow ? 2
                                                                : 8));

            for (int i = 0; i < count; ++i)
            {
                CAPTURE(i);
                CHECK(handle_at(a, x.to_screen(h[i]), x, radius) == i);
            }

            // A point well away from every handle finds none, so the check above is not simply always
            // returning the first one.
            CHECK(handle_at(a, x.to_screen(float2{500.f, 500.f}), x, radius) == -1);
        }
    }
}

TEST_CASE("The topmost annotation is the one picked up")
{
    // Later annotations draw over earlier ones, so the search has to run the other way; a forward search
    // would hand back whichever was drawn first and looks buried.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        const Annotation under = sample(shape);
        const Annotation over  = sample(shape);
        const float2     p     = point_on_outline(over);

        CHECK(annotation_at({under, over}, p, identity_transform(), k_slop) == 1);
    }
}

TEST_CASE("An annotation that cannot be taken hold of is not picked up")
{
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        const Annotation a = sample(shape);
        const float2     p = point_on_outline(a);
        const auto       x = identity_transform();

        REQUIRE(annotation_at({a}, p, x, k_slop) == 0);

        Annotation hidden = a;
        hidden.visible    = false;
        CHECK(annotation_at({hidden}, p, x, k_slop) == -1);

        Annotation locked = a;
        locked.locked     = true;
        CHECK(annotation_at({locked}, p, x, k_slop) == -1);

        // ...and one of those in front does not shield the one behind it.
        CHECK(annotation_at({a, hidden}, p, x, k_slop) == 0);
    }
}

TEST_CASE("How easy a shape is to hit does not change with zoom")
{
    // Slop and stroke width are screen quantities, like the stroke itself, so the same miss by a few
    // screen pixels has to land the same way however far the image is zoomed in.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        const Annotation a = sample(shape);

        for (float scale : {0.25f, 1.f, 8.f})
        {
            CAPTURE(scale);
            const auto   x  = scaled_transform(scale);
            const float2 on = x.to_screen(point_on_outline(a));

            // Just inside the tolerance the stroke and slop together allow, and well outside it.
            const float tol = k_slop + a.stroke_width * 0.5f;
            CHECK(annotation_at({a}, on + float2{0.f, tol - 1.f}, x, k_slop) == 0);
            CHECK(annotation_at({a}, on + float2{0.f, 4.f * tol}, x, k_slop) == -1);
        }
    }
}

TEST_CASE("A dragged handle follows the cursor, in the directions it is free to move")
{
    // Corners and endpoints take both of the cursor's coordinates; an edge midpoint takes only the one
    // across the edge it sits on, and stays centered along it.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        float2    h[Annotation::MaxHandles];
        const int count = annotation_handles(sample(shape), h);

        for (int i = 0; i < count; ++i)
        {
            CAPTURE(i);

            // Small enough not to drag anything past the far side, which is the next test's business.
            const float2 delta{5.f, 3.f};

            Annotation a = sample(shape);
            move_annotation_handle(a, i, h[i] + delta);

            float2 after[Annotation::MaxHandles];
            annotation_handles(a, after);

            const bool free_x = i < 4 || i == 5 || i == 7;
            const bool free_y = i < 4 || i == 4 || i == 6;
            CHECK(after[i].x == doctest::Approx(h[i].x + (free_x ? delta.x : 0.f)));
            CHECK(after[i].y == doctest::Approx(h[i].y + (free_y ? delta.y : 0.f)));
        }
    }
}

TEST_CASE("A corner dragged past the opposite one turns the shape inside out rather than sticking")
{
    const auto      x      = identity_transform();
    constexpr float radius = 6.f;

    for (auto shape : {Annotation::Shape::Rect, Annotation::Shape::Ellipse})
    {
        CAPTURE(annotation_shape_name(shape));

        Annotation a = sample(shape);
        float2     h[Annotation::MaxHandles];
        annotation_handles(a, h);

        // Handle 0 is the low corner; this drags it well beyond the high one.
        const float2 to = h[2] + float2{50.f, 40.f};
        move_annotation_handle(a, 0, to);

        // The shape comes back ordered, so everything downstream can still read p0 as the low corner...
        CHECK(a.p0().x <= a.p1().x);
        CHECK(a.p0().y <= a.p1().y);

        // ...and the cursor is still holding a corner of it, rather than having let go at the crossing.
        CHECK(handle_at(a, x.to_screen(to), x, radius) >= 0);
    }
}

TEST_CASE("Dragging one handle leaves the opposite side of the shape alone")
{
    // A resize pivots about the side you are not holding. Were the shape re-centered or re-ordered
    // wrongly, this is what would move.
    for (auto shape : {Annotation::Shape::Rect, Annotation::Shape::Ellipse})
    {
        CAPTURE(annotation_shape_name(shape));

        const Annotation before = sample(shape);
        float2           h0[Annotation::MaxHandles];
        annotation_handles(before, h0);

        // Handle 0 is the low corner, so handle 2, the high one, is what must not move.
        Annotation a = before;
        move_annotation_handle(a, 0, float2{0.f, 5.f});

        float2 h1[Annotation::MaxHandles];
        annotation_handles(a, h1);
        CHECK(h1[2].x == doctest::Approx(h0[2].x));
        CHECK(h1[2].y == doctest::Approx(h0[2].y));

        // And the corner that was dragged did move, so the check above is not comparing two unchanged
        // shapes.
        CHECK(length(h1[0] - h0[0]) > 1.f);
    }
}

TEST_CASE("A handle held across the far side keeps its grip")
{
    // Dragging a corner past its opposite renumbers the handles. If the index did not come back renumbered,
    // a drag held through the crossing would let go and start moving a different corner, and the shape
    // would jump. Each step here continues the drag from the index the last one returned.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        float2    h[Annotation::MaxHandles];
        const int count = annotation_handles(sample(shape), h);

        for (int i = 0; i < count; ++i)
        {
            CAPTURE(i);

            Annotation a     = sample(shape);
            int        index = i;

            // Out well past the opposite side, back through it, and out the other way again.
            for (float2 to : {float2{300.f, 250.f}, float2{-200.f, -150.f}, float2{60.f, 45.f}})
            {
                CAPTURE(to.x);
                index = move_annotation_handle(a, index, to);
                REQUIRE(index >= 0);
                REQUIRE(index < count);

                float2 after[Annotation::MaxHandles];
                annotation_handles(a, after);

                // The handle now named by that index is the one the cursor is holding: it tracks the
                // cursor in whichever directions it is free to move.
                const bool free_x = index < 4 || index == 5 || index == 7;
                const bool free_y = index < 4 || index == 4 || index == 6;
                if (free_x)
                    CHECK(after[index].x == doctest::Approx(to.x));
                if (free_y)
                    CHECK(after[index].y == doctest::Approx(to.y));
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
           a.smooth == b.smooth && a.visible == b.visible && a.locked == b.locked;
}

} // namespace

TEST_CASE("Every shape survives a trip through a session file")
{
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        // Every field set away from its default, so a field the serializer forgets comes back as that
        // default and fails here rather than round-tripping by accident.
        Annotation a   = sample(shape);
        a.fill_color   = float4{0.25f, 0.5f, 0.75f, 0.5f};
        a.text_align   = VgCommand::AlignRight | VgCommand::AlignBottom;
        a.label        = "a label of its own";
        a.visible      = false;
        a.locked       = true;
        a.stroke_width = 7.5f;
        a.font_size    = 31.f;
        a.font_face    = "mono-bold";
        a.smooth       = true;

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

    // The points are untouched, so the flag can be turned on and off without losing the stroke.
    CHECK(straight.points == curved.points);

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
    // Glyphs cannot be stretched, so a box drag has to come out as a font size. The measure here stands in
    // for a font: a fixed box per character, proportional to the size, which is all the property needs.
    // Zoomed, not identity: a font size is a screen quantity and the box is an image one, so a transform
    // that scales by one would hide the conversion between them entirely.
    constexpr float zoom = 2.f;
    auto            x    = scaled_transform(zoom);
    x.measure_text       = [](const std::string &, float size, const std::string &text)
    { return float2{0.5f * size * float(text.size()), size}; };

    Annotation a = sample(Annotation::Shape::Text);
    a.text       = "caption";
    a.font_size  = 20.f;
    a.points     = {float2{100.f, 100.f}};
    a.text_align = VgCommand::AlignLeft | VgCommand::AlignTop;

    float2    handles[Annotation::MaxHandles];
    const int count = annotation_handles(a, handles, &x);
    REQUIRE(count == 4); // the corners; an edge would only stretch it

    // Without something to measure with there is no box, so there is nothing to take hold of.
    CHECK(annotation_handles(a, handles, nullptr) == 0);

    float2 lo, extent;
    REQUIRE(text_extent(a, x, lo, extent));

    // The box is in image coordinates, and a font size is too, so it is the same box however far the view
    // is zoomed -- which is what makes a string shrink with the image rather than floating over it.
    CHECK(extent.y == doctest::Approx(a.font_size));
    CHECK(extent.x == doctest::Approx(0.5f * a.font_size * float(a.text.size())));

    for (float other_zoom : {0.25f, 1.f, 8.f})
    {
        CAPTURE(other_zoom);
        auto y         = scaled_transform(other_zoom);
        y.measure_text = x.measure_text;

        float2 lo_z, extent_z;
        REQUIRE(text_extent(a, y, lo_z, extent_z));
        CHECK(extent_z.x == doctest::Approx(extent.x));
        CHECK(extent_z.y == doctest::Approx(extent.y));
    }

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
