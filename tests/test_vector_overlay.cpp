//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// The overlay interpreter, driven against a real ImDrawList and checked on the geometry it produces.
// ImDrawList works without an ImGui context, so this needs no window and no graphics API -- which is what
// makes the drawing itself testable rather than only inspectable in a screenshot.

#include <doctest/doctest.h>

#include "vector_overlay.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>

namespace
{

//! A draw list with just enough shared state to tessellate and emit geometry.
struct TestDrawList
{
    ImDrawListSharedData shared;
    ImDrawList           list;

    TestDrawList() : list(&shared)
    {
        shared.ClipRectFullscreen   = ImVec4(-8192.f, -8192.f, 8192.f, 8192.f);
        shared.CurveTessellationTol = 1.25f;
        // No anti-aliasing, so a vertex count reflects the path itself rather than the AA fringe around it.
        shared.InitialFlags = ImDrawListFlags_None;
        shared.SetCircleTessellationMaxError(0.30f);

        // What ImGui does to a draw list at the top of each frame; without it the command buffer is empty
        // and the first PushClipRect has no command to amend.
        list._ResetForNewFrame();
        list.PushClipRectFullScreen();
    }
};

//! Identity image-to-screen mapping, so expected coordinates are the ones the commands name.
VgTransform identity_transform()
{
    VgTransform x;
    x.to_screen = [](float2 p) { return p; };
    x.scale     = 1.f;
    return x;
}

//! Axis-aligned bounds of everything written into the vertex buffer.
struct Bounds
{
    float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
    bool  empty() const { return min_x > max_x; }
};

Bounds vertex_bounds(const ImDrawList &list)
{
    Bounds b;
    for (int i = 0; i < list.VtxBuffer.Size; ++i)
    {
        const ImVec2 p = list.VtxBuffer[i].pos;
        b.min_x        = std::min(b.min_x, p.x);
        b.min_y        = std::min(b.min_y, p.y);
        b.max_x        = std::max(b.max_x, p.x);
        b.max_y        = std::max(b.max_y, p.y);
    }
    return b;
}

VgCommand cmd(VgCommand::Type type, std::vector<float> data = {}) { return VgCommand{type, std::move(data), ""}; }

} // namespace

TEST_CASE("A stroked rectangle lands where the commands put it")
{
    TestDrawList d;

    // The shape the official tevclient example draws around each finished tile.
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

    // The stroke straddles the path, so with anti-aliasing off the geometry is exactly half a line width
    // outside the rectangle on every side: [10,110]x[20,70] grown by 1.
    CHECK(b.min_x == doctest::Approx(9.f).epsilon(0.01f));
    CHECK(b.min_y == doctest::Approx(19.f).epsilon(0.01f));
    CHECK(b.max_x == doctest::Approx(111.f).epsilon(0.01f));
    CHECK(b.max_y == doctest::Approx(71.f).epsilon(0.01f));
}

TEST_CASE("Relative sizes scale with the image, absolute ones do not")
{
    // A stroke flagged Relative is in image pixels and so has to get thicker as the view zooms in; one
    // flagged Absolute is in screen pixels and must not. Zoom is the only difference between these two.
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
    // The polygon is inscribed, so it falls just inside the true circle -- by less than the tessellation
    // error the segment count was chosen for, which is well under a pixel.
    CHECK(b.min_x == doctest::Approx(60.f).epsilon(0.005f));
    CHECK(b.max_x == doctest::Approx(140.f).epsilon(0.005f));
    CHECK(b.min_y == doctest::Approx(60.f).epsilon(0.005f));
    CHECK(b.max_y == doctest::Approx(140.f).epsilon(0.005f));
}

TEST_CASE("Each subpath is drawn, not just the last one")
{
    // NanoVG accumulates subpaths and paints them together; ImDrawList has one path buffer, so this is the
    // case that would silently drop everything but the final MoveTo run if the staging were wrong.
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
    // ...and the second subpath is actually out at x=100, not collapsed onto the first.
    CHECK(vertex_bounds(two.list).max_x == doctest::Approx(110.f).epsilon(0.1f));
}

TEST_CASE("A path can be filled and then stroked, as NanoVG allows")
{
    // Only BeginPath discards the path; painting it does not. A viewer that cleared on Fill would drop the
    // outline that a client expects to follow it.
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
    // Restore has to put back the width Save captured; without it the second line would come out thick.
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
    // The interpreter is fed by a network parser, so a command whose data does not match its type has to be
    // survivable rather than indexed into.
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
    // The path buffer is scratch space shared with everything else drawing this frame; leaving points in it
    // would corrupt whatever primitive came next.
    TestDrawList d;
    draw_vector_overlay(&d.list,
                        {
                            cmd(VgCommand::Type::BeginPath), cmd(VgCommand::Type::MoveTo, {0.f, 0.f}),
                            cmd(VgCommand::Type::LineTo, {10.f, 10.f}),
                            // deliberately left unpainted and unclosed
                        },
                        identity_transform(), IM_COL32_WHITE);

    CHECK(d.list._Path.Size == 0);
}
