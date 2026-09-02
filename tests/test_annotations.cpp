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
