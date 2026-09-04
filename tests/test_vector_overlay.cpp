//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// The overlay interpreter, driven against a real ImDrawList and checked on the geometry it produces.
// ImDrawList works without an ImGui context, so this needs no window and no graphics API.

#include <doctest/doctest.h>

#include "vector_overlay.h"

#include "test_draw_list.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>

using namespace test_draw;

TEST_CASE("A stroked rectangle lands where the commands put it")
{
    TestDrawList d;

    // the shape the tevclient example draws around each finished tile
    draw_vector_overlay(&d.list,
                        {
                            cmd(VgCommand::Type::BeginPath),
                            cmd(VgCommand::Type::StrokeColor, {1.f, 1.f, 1.f, 1.f}),
                            cmd(VgCommand::Type::StrokeWidth, {2.f, float(VgCommand::Absolute)}),
                            cmd(VgCommand::Type::Rect, {10.f, 20.f, 100.f, 50.f}),
                            cmd(VgCommand::Type::Stroke),
                        },
                        identity_transform(), IM_COL32_WHITE);

    REQUIRE(d.list.VtxBuffer.Size > 0);
    const auto b = vertex_bounds(d.list);

    // the stroke straddles the path, so with anti-aliasing off the geometry is half a line width outside the
    // rectangle on every side: [10,110]x[20,70] grown by 1
    CHECK(b.min_x == doctest::Approx(9.f).epsilon(0.01f));
    CHECK(b.min_y == doctest::Approx(19.f).epsilon(0.01f));
    CHECK(b.max_x == doctest::Approx(111.f).epsilon(0.01f));
    CHECK(b.max_y == doctest::Approx(71.f).epsilon(0.01f));
}

TEST_CASE("Relative sizes scale with the image, absolute ones do not")
{
    // Relative widths are in image pixels and thicken as the view zooms in; Absolute ones are in screen
    // pixels. Zoom is the only difference between the two draws below.
    auto stroked_line = [](float scale, VgCommand::ScaleKind kind)
    {
        TestDrawList d;
        VgTransform  x = identity_transform();
        x.scale        = scale;
        draw_vector_overlay(&d.list,
                            {
                                cmd(VgCommand::Type::BeginPath),
                                cmd(VgCommand::Type::StrokeWidth, {4.f, float(kind)}),
                                cmd(VgCommand::Type::MoveTo, {0.f, 50.f}),
                                cmd(VgCommand::Type::LineTo, {100.f, 50.f}),
                                cmd(VgCommand::Type::Stroke),
                            },
                            x, IM_COL32_WHITE);
        const auto b = vertex_bounds(d.list);
        return b.max_y - b.min_y; // the drawn thickness
    };

    CHECK(stroked_line(1.f, VgCommand::Relative) == doctest::Approx(4.f).epsilon(0.05f));
    CHECK(stroked_line(4.f, VgCommand::Relative) == doctest::Approx(16.f).epsilon(0.05f));

    CHECK(stroked_line(1.f, VgCommand::Absolute) == doctest::Approx(4.f).epsilon(0.05f));
    CHECK(stroked_line(4.f, VgCommand::Absolute) == doctest::Approx(4.f).epsilon(0.05f));
}

TEST_CASE("A filled circle covers its radius")
{
    TestDrawList d;
    draw_vector_overlay(&d.list,
                        {
                            cmd(VgCommand::Type::FillColor, {1.f, 0.f, 0.f, 1.f}),
                            cmd(VgCommand::Type::BeginPath),
                            cmd(VgCommand::Type::Circle, {100.f, 100.f, 40.f}),
                            cmd(VgCommand::Type::Fill),
                        },
                        identity_transform(), IM_COL32_WHITE);

    REQUIRE(d.list.VtxBuffer.Size >= 3);
    const auto b = vertex_bounds(d.list);
    // the inscribed polygon falls inside the true circle by less than the tessellation error, well under a pixel
    CHECK(b.min_x == doctest::Approx(60.f).epsilon(0.005f));
    CHECK(b.max_x == doctest::Approx(140.f).epsilon(0.005f));
    CHECK(b.min_y == doctest::Approx(60.f).epsilon(0.005f));
    CHECK(b.max_y == doctest::Approx(140.f).epsilon(0.005f));
}

TEST_CASE("Each subpath is drawn, not just the last one")
{
    // NanoVG accumulates subpaths and paints them together; ImDrawList has one path buffer to stage them in
    TestDrawList one, two;

    const auto square = [](float x)
    {
        return std::vector<VgCommand>{
            cmd(VgCommand::Type::MoveTo, {x, 0.f}),
            cmd(VgCommand::Type::LineTo, {x + 10.f, 0.f}),
            cmd(VgCommand::Type::LineTo, {x + 10.f, 10.f}),
            cmd(VgCommand::Type::ClosePath),
        };
    };

    std::vector<VgCommand> single{cmd(VgCommand::Type::BeginPath)};
    for (auto &c : square(0.f)) single.push_back(c);
    single.push_back(cmd(VgCommand::Type::Stroke));

    std::vector<VgCommand> pair{cmd(VgCommand::Type::BeginPath)};
    for (auto &c : square(0.f)) pair.push_back(c);
    for (auto &c : square(100.f)) pair.push_back(c);
    pair.push_back(cmd(VgCommand::Type::Stroke));

    draw_vector_overlay(&one.list, single, identity_transform(), IM_COL32_WHITE);
    draw_vector_overlay(&two.list, pair, identity_transform(), IM_COL32_WHITE);

    CHECK(two.list.VtxBuffer.Size == 2 * one.list.VtxBuffer.Size);
    // ...and the second subpath is out at x=100, not collapsed onto the first
    CHECK(vertex_bounds(two.list).max_x == doctest::Approx(110.f).epsilon(0.1f));
}

TEST_CASE("A path can be filled and then stroked, as NanoVG allows")
{
    // only BeginPath discards the path; painting it does not
    TestDrawList fill_only, fill_then_stroke;

    const std::vector<VgCommand> shape{
        cmd(VgCommand::Type::BeginPath),
        cmd(VgCommand::Type::Rect, {0.f, 0.f, 50.f, 50.f}),
        cmd(VgCommand::Type::Fill),
    };
    std::vector<VgCommand> both = shape;
    both.push_back(cmd(VgCommand::Type::Stroke));

    draw_vector_overlay(&fill_only.list, shape, identity_transform(), IM_COL32_WHITE);
    draw_vector_overlay(&fill_then_stroke.list, both, identity_transform(), IM_COL32_WHITE);

    CHECK(fill_then_stroke.list.VtxBuffer.Size > fill_only.list.VtxBuffer.Size);
}

TEST_CASE("Save and Restore bracket changes to the drawing state")
{
    // Restore has to put back the width Save captured, or the second line comes out thick
    TestDrawList d;
    draw_vector_overlay(&d.list,
                        {
                            cmd(VgCommand::Type::StrokeWidth, {2.f, float(VgCommand::Absolute)}),
                            cmd(VgCommand::Type::Save),
                            cmd(VgCommand::Type::StrokeWidth, {20.f, float(VgCommand::Absolute)}),
                            cmd(VgCommand::Type::Restore),
                            cmd(VgCommand::Type::BeginPath),
                            cmd(VgCommand::Type::MoveTo, {0.f, 50.f}),
                            cmd(VgCommand::Type::LineTo, {100.f, 50.f}),
                            cmd(VgCommand::Type::Stroke),
                        },
                        identity_transform(), IM_COL32_WHITE);

    const auto b = vertex_bounds(d.list);
    CHECK((b.max_y - b.min_y) == doctest::Approx(2.f).epsilon(0.05f));
}

TEST_CASE("Commands HDRView cannot draw are reported rather than approximated")
{
    TestDrawList             d;
    std::vector<std::string> reported;

    draw_vector_overlay(&d.list,
                        {
                            cmd(VgCommand::Type::PathWinding, {1.f}),
                            cmd(VgCommand::Type::ArcTo, {0.f, 0.f, 1.f, 1.f, 5.f}),
                            cmd(VgCommand::Type::RoundedRectVarying, {0, 0, 10, 10, 1, 2, 3, 4}),
                        },
                        identity_transform(), IM_COL32_WHITE,
                        [&reported](const char *what) { reported.push_back(what); });

    CHECK(reported.size() == 3);
    CHECK(d.list.VtxBuffer.Size == 0); // nothing was drawn in their place
}

TEST_CASE("A command carrying the wrong number of arguments is skipped, not read past")
{
    // the interpreter is fed by a network parser, so its input is untrusted
    TestDrawList             d;
    std::vector<std::string> reported;

    draw_vector_overlay(&d.list,
                        {
                            VgCommand{VgCommand::Type::Rect, {1.f}, ""}, // wants four floats
                            cmd(VgCommand::Type::BeginPath),
                            cmd(VgCommand::Type::Rect, {0.f, 0.f, 10.f, 10.f}),
                            cmd(VgCommand::Type::Stroke),
                        },
                        identity_transform(), IM_COL32_WHITE,
                        [&reported](const char *what) { reported.push_back(what); });

    CHECK(reported.size() == 1);
    CHECK(d.list.VtxBuffer.Size > 0); // the well-formed commands after it still drew
}

TEST_CASE("An overlay leaves the draw list's shared path buffer empty")
{
    // the path buffer is scratch space shared with everything else drawing this frame
    TestDrawList d;
    draw_vector_overlay(&d.list,
                        {
                            cmd(VgCommand::Type::BeginPath), cmd(VgCommand::Type::MoveTo, {0.f, 0.f}),
                            cmd(VgCommand::Type::LineTo, {10.f, 10.f}),
                            // left unpainted and unclosed
                        },
                        identity_transform(), IM_COL32_WHITE);

    CHECK(d.list._Path.Size == 0);
}

TEST_CASE("Scaled-up text moves with its anchor rather than jumping between pixels")
{
    // Text is laid out at a size the font is baked at and the glyphs are then scaled up, which is what
    // keeps a zoomed view from rasterizing a fresh set every frame. ImFont::RenderText lays them out from
    // a whole-pixel position, so scaling them about anywhere else multiplies the fraction of a pixel
    // between the two, and the text jumps by that multiple every time it crosses a pixel boundary.
    ImFontAtlas atlas;
    ImFont     *font = atlas.AddFontDefault();
    REQUIRE(font != nullptr);

    // A small size drawn into a hugely zoomed view: the size the glyphs bake at is small, so what they are
    // scaled by is large, and so is anything it multiplies.
    VgTransform x  = identity_transform();
    x.scale        = 40.f;
    x.default_font = font;

    auto drawn_offset = [&](float anchor_x)
    {
        std::vector<VgCommand> program{
            cmd(VgCommand::Type::FillColor, {1.f, 1.f, 1.f, 1.f}),
            cmd(VgCommand::Type::FontSize, {4.f, float(VgCommand::Relative)}),
            cmd(VgCommand::Type::TextAlign, {float(VgCommand::AlignLeft | VgCommand::AlignTop)}),
            cmd(VgCommand::Type::Text, {anchor_x, 50.f}, "Hjy")};

        TestDrawList d;
        draw_vector_overlay(&d.list, program, x, IM_COL32_WHITE);
        REQUIRE(d.list.VtxBuffer.Size > 0);

        // Where the glyphs landed relative to where the text was asked to go. However the layout rounds
        // internally, this cannot depend on where in a pixel the anchor happens to fall.
        return vertex_bounds(d.list).min_x - anchor_x;
    };

    const float reference = drawn_offset(100.f);

    // Across a whole pixel in small steps, which is what carries the anchor over a boundary.
    for (int step = 0; step <= 20; ++step)
    {
        const float anchor = 100.f + 0.1f * float(step);
        CAPTURE(anchor);

        // Half a screen pixel: far under the forty a boundary crossing would move it by, and loose enough
        // that the arithmetic behind the layout is not what is being pinned.
        CHECK(std::abs(drawn_offset(anchor) - reference) < 0.5f);
    }
}
