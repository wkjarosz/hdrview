//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

// Enough of an ImGui draw list to tessellate and emit geometry, shared by the tests that check what the
// overlay interpreter draws. ImDrawList works without an ImGui context, so these need no window and no
// graphics API.

#include "vector_overlay.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace test_draw
{

/// A draw list with just enough shared state to tessellate and emit geometry.
struct TestDrawList
{
    ImDrawListSharedData shared;
    ImDrawList           list;

    TestDrawList() : list(&shared)
    {
        shared.ClipRectFullscreen   = ImVec4(-8192.f, -8192.f, 8192.f, 8192.f);
        shared.CurveTessellationTol = 1.25f;
        // no anti-aliasing, so a vertex count reflects the path and not the fringe around it
        shared.InitialFlags = ImDrawListFlags_None;
        shared.SetCircleTessellationMaxError(0.30f);

        // what ImGui does at the top of each frame; without it the first PushClipRect has no command to amend
        list._ResetForNewFrame();
        list.PushClipRectFullScreen();
    }
};

/// Identity image-to-screen mapping, so expected coordinates are the ones the commands name.
inline VgTransform identity_transform()
{
    VgTransform x;
    x.to_screen = [](float2 p) { return p; };
    x.scale     = 1.f;
    return x;
}

/// Axis-aligned bounds of everything written into the vertex buffer.
struct Bounds
{
    float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
    bool  empty() const { return min_x > max_x; }
};

inline Bounds vertex_bounds(const ImDrawList &list)
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

inline VgCommand cmd(VgCommand::Type type, std::vector<float> data = {}, std::string text = {})
{
    return VgCommand{type, std::move(data), std::move(text)};
}

} // namespace test_draw
