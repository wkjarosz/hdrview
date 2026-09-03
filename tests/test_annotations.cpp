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
    a.p0           = float2{10.f, 20.f};
    a.p1           = float2{110.f, 70.f};
    a.stroke_color = float4{1.f, 0.5f, 0.25f, 1.f};
    a.stroke_width = 4.f;
    a.font_size    = 24.f;
    a.text         = "label";
    if (shape == Annotation::Shape::Text)
        a.p1 = a.p0;
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

TEST_CASE("Stroke width and font size are screen quantities, and do not scale with zoom")
{
    // The decision that separates user annotations from what a renderer sends: markup stays legible at any
    // zoom rather than dwindling with the feature it points at. Zoom is the only difference between these.
    for (auto shape : all_shapes())
    {
        CAPTURE(annotation_shape_name(shape));

        std::vector<VgCommand> out;
        append_vg_commands(out, sample(shape), 1.f);

        for (const auto &c : out)
            if (c.type == VgCommand::Type::StrokeWidth || c.type == VgCommand::Type::FontSize)
                CHECK(int(c.data[1]) == int(VgCommand::Absolute));
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
        a.p0         = float2{0.f, 0.f};
        a.p1         = float2{1000.f, 0.f}; // long, so the head is never clamped by the shaft

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
    shaft_only.p0         = float2{0.f, 0.f};
    shaft_only.p1         = float2{1000.f, 0.f};
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
    default: return a.p0; // Text is only ever its anchor
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
        const float2 middle = (a.p0 + a.p1) * 0.5f;
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
        CHECK(a.p0.x <= a.p1.x);
        CHECK(a.p0.y <= a.p1.y);

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
