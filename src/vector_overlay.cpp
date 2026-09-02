//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "vector_overlay.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>

int VgCommand::num_floats(Type type)
{
    switch (type)
    {
    case Type::Save:
    case Type::Restore:
    case Type::Fill:
    case Type::Stroke:
    case Type::BeginPath:
    case Type::ClosePath:
    case Type::DebugDumpPathCache:
    case Type::FontFace: return 0;

    case Type::PathWinding:
    case Type::TextAlign: return 1;

    case Type::StrokeWidth:
    case Type::FontSize: return 2; // value, ScaleKind

    case Type::MoveTo:
    case Type::LineTo:
    case Type::Text: return 2; // Text's string is carried separately

    case Type::Circle: return 3;

    case Type::FillColor:
    case Type::StrokeColor:
    case Type::Ellipse:
    case Type::QuadTo:
    case Type::Rect: return 4;

    case Type::ArcTo:
    case Type::RoundedRect: return 5;

    case Type::Arc:
    case Type::BezierTo: return 6;

    case Type::RoundedRectVarying: return 8;

    case Type::Invalid:
    default: return -1;
    }
}

namespace
{

/// The mutable drawing state, as NanoVG defines it. Save/Restore push and pop this whole struct.
struct VgState
{
    ImU32 fill_color;
    ImU32 stroke_color;
    float stroke_width          = 3.f; // tev's default
    bool  stroke_width_relative = false;
    float font_size             = 20.f; // tev's default
    bool  font_size_relative    = false;
    int   text_align            = VgCommand::AlignLeft | VgCommand::AlignBaseline;
    void *font                  = nullptr;
};

/// One run of points; a path is a list of these, as NanoVG accumulates them between MoveTo calls.
struct SubPath
{
    ImVector<ImVec2> points;
    bool             closed = false;
};

ImU32 color_from(const float *f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(f[0], f[1], f[2], f[3])); }

/**
    Builds a NanoVG-style path out of ImDrawList's own path machinery.

    ImDrawList has one path buffer and no notion of subpaths, so each subpath is built there, for ImGui's
    curve and arc tessellation, then moved out into `m_subpaths` when it ends. Stroke and Fill walk those;
    only BeginPath discards them, matching NanoVG, where a path can be filled and then stroked.
*/
class PathBuilder
{
public:
    PathBuilder(ImDrawList *draw_list) : m_draw_list(draw_list) {}

    void begin()
    {
        m_subpaths.clear();
        m_draw_list->_Path.Size = 0;
    }

    /// Start a new subpath at \p p.
    void move_to(ImVec2 p)
    {
        flush(false);
        m_draw_list->PathLineTo(p);
    }

    /// ImDrawList's Path* helpers append to the subpath under construction; use them via this.
    ImDrawList *path() { return m_draw_list; }

    /// Finish the subpath under construction, if it has enough points to draw.
    void flush(bool closed)
    {
        if (m_draw_list->_Path.Size >= 2)
        {
            m_subpaths.push_back({});
            m_subpaths.back().points = m_draw_list->_Path;
            m_subpaths.back().closed = closed;
        }
        m_draw_list->_Path.Size = 0;
    }

    /// A shape that is a closed subpath all by itself (a rect, a circle): ends whatever preceded it too.
    void add_closed_shape() { flush(true); }

    void close() { flush(true); }

    void stroke(ImU32 color, float width) const
    {
        for (const auto &sp : m_subpaths)
            m_draw_list->AddPolyline(sp.points.Data, sp.points.Size, color, width,
                                     sp.closed ? ImDrawFlags_Closed : ImDrawFlags_None);
    }

    void fill(ImU32 color) const
    {
        // Each subpath is filled on its own. NanoVG fills them together under a winding rule, which is how
        // a subpath marked as a hole punches through the one around it; ImDrawList has no winding, so
        // PathWinding is refused below.
        for (const auto &sp : m_subpaths)
            if (sp.points.Size >= 3)
                m_draw_list->AddConcavePolyFilled(sp.points.Data, sp.points.Size, color);
    }

    /// Whether anything has been built but not yet ended.
    bool building() const { return m_draw_list->_Path.Size > 0; }

private:
    ImDrawList          *m_draw_list;
    std::vector<SubPath> m_subpaths;
};

/// Where AddText should put the string's top-left, given NanoVG's alignment flags.
/**
    ImGui has no notion of a baseline, so Baseline is approximated as a fraction of the line height.
*/
ImVec2 aligned_text_pos(ImVec2 anchor, ImVec2 size, int align)
{
    float x = anchor.x;
    if (align & VgCommand::AlignCenter)
        x -= size.x * 0.5f;
    else if (align & VgCommand::AlignRight)
        x -= size.x;

    float y = anchor.y; // AlignTop
    if (align & VgCommand::AlignMiddle)
        y -= size.y * 0.5f;
    else if (align & VgCommand::AlignBottom)
        y -= size.y;
    else if (align & VgCommand::AlignBaseline)
        y -= size.y * 0.8f;

    return ImVec2(x, y);
}

} // namespace

void draw_vector_overlay(ImDrawList *draw_list, const std::vector<VgCommand> &commands, const VgTransform &xform,
                         uint32_t default_color, const std::function<void(const char *)> &on_unsupported)
{
    if (!draw_list || commands.empty() || !xform.to_screen)
        return;

    auto unsupported = [&on_unsupported](const char *what)
    {
        if (on_unsupported)
            on_unsupported(what);
    };

    const auto  to_screen = [&xform](float x, float y) { return ImVec2(xform.to_screen(float2{x, y})); };
    const float scale     = xform.scale;

    VgState              state;
    std::vector<VgState> stack;
    state.fill_color   = default_color;
    state.stroke_color = default_color;
    state.font         = xform.default_font;

    // subpaths are staged through ImDrawList's own path buffer, which is scratch space between primitives
    // and so is empty on entry and left empty on exit
    PathBuilder path{draw_list};
    draw_list->_Path.Size = 0;

    for (const auto &cmd : commands)
    {
        const float *f = cmd.data.data();
        if (int expected = VgCommand::num_floats(cmd.type); expected < 0 || int(cmd.data.size()) != expected)
        {
            unsupported("a command with the wrong number of arguments");
            continue;
        }

        switch (cmd.type)
        {
        // -- state ------------------------------------------------------------------------------------
        case VgCommand::Type::Save: stack.push_back(state); break;
        case VgCommand::Type::Restore:
            if (!stack.empty())
            {
                state = stack.back();
                stack.pop_back();
            }
            break;

        case VgCommand::Type::FillColor: state.fill_color = color_from(f); break;
        case VgCommand::Type::StrokeColor: state.stroke_color = color_from(f); break;
        case VgCommand::Type::StrokeWidth:
            state.stroke_width          = f[0];
            state.stroke_width_relative = int(f[1]) == VgCommand::Relative;
            break;

        // -- path construction ------------------------------------------------------------------------
        case VgCommand::Type::BeginPath: path.begin(); break;
        case VgCommand::Type::ClosePath: path.close(); break;
        case VgCommand::Type::MoveTo: path.move_to(to_screen(f[0], f[1])); break;
        case VgCommand::Type::LineTo: path.path()->PathLineTo(to_screen(f[0], f[1])); break;

        case VgCommand::Type::BezierTo:
            path.path()->PathBezierCubicCurveTo(to_screen(f[0], f[1]), to_screen(f[2], f[3]), to_screen(f[4], f[5]));
            break;
        case VgCommand::Type::QuadTo:
            path.path()->PathBezierQuadraticCurveTo(to_screen(f[0], f[1]), to_screen(f[2], f[3]));
            break;

        case VgCommand::Type::Arc:
            // NanoVG's winding argument picks the sweep direction; PathArcTo sweeps from a_min to a_max
            {
                const ImVec2 c   = to_screen(f[0], f[1]);
                const float  r   = f[2] * scale;
                const bool   ccw = int(f[5]) == 1;
                path.path()->PathArcTo(c, r, ccw ? f[4] : f[3], ccw ? f[3] : f[4]);
            }
            break;

        case VgCommand::Type::Rect:
        {
            const ImVec2 a = to_screen(f[0], f[1]);
            const ImVec2 b = to_screen(f[0] + f[2], f[1] + f[3]);
            path.flush(false);
            draw_list->PathRect(a, b);
            path.add_closed_shape();
        }
        break;

        case VgCommand::Type::RoundedRect:
        {
            const ImVec2 a = to_screen(f[0], f[1]);
            const ImVec2 b = to_screen(f[0] + f[2], f[1] + f[3]);
            path.flush(false);
            draw_list->PathRect(a, b, f[4] * scale);
            path.add_closed_shape();
        }
        break;

        case VgCommand::Type::Circle:
        {
            // Stop one segment short of a full turn: the subpath is closed when drawn, so sweeping the
            // whole circle would leave its first and last point on top of each other (as AddCircle() does).
            const ImVec2 c    = to_screen(f[0], f[1]);
            const float  r    = f[2] * scale;
            const int    segs = ImMax(3, draw_list->_CalcCircleAutoSegmentCount(r));
            path.flush(false);
            draw_list->PathArcTo(c, r, 0.f, IM_PI * 2.f * (segs - 1) / segs, segs - 1);
            path.add_closed_shape();
        }
        break;

        case VgCommand::Type::Ellipse:
        {
            const ImVec2 c    = to_screen(f[0], f[1]);
            const ImVec2 r    = ImVec2(f[2] * scale, f[3] * scale);
            const int    segs = ImMax(3, draw_list->_CalcCircleAutoSegmentCount(ImMax(r.x, r.y)));
            path.flush(false);
            draw_list->PathEllipticalArcTo(c, r, 0.f, 0.f, IM_PI * 2.f * (segs - 1) / segs, segs - 1);
            path.add_closed_shape();
        }
        break;

        // -- painting ---------------------------------------------------------------------------------
        case VgCommand::Type::Stroke:
            path.flush(false);
            path.stroke(state.stroke_color, state.stroke_width * (state.stroke_width_relative ? scale : 1.f));
            break;
        case VgCommand::Type::Fill:
            path.flush(false);
            path.fill(state.fill_color);
            break;

        // -- text -------------------------------------------------------------------------------------
        case VgCommand::Type::FontSize:
            state.font_size          = f[0];
            state.font_size_relative = int(f[1]) == VgCommand::Relative;
            break;
        case VgCommand::Type::TextAlign: state.text_align = int(f[0]); break;
        case VgCommand::Type::FontFace:
            if (xform.font_for)
                if (void *font = xform.font_for(cmd.text))
                    state.font = font;
            break;

        case VgCommand::Type::Text:
        {
            const float size = ImMax(1.f, state.font_size * (state.font_size_relative ? scale : 1.f));
            auto       *font = (ImFont *)(state.font ? state.font : xform.default_font);
            if (!font)
                break;

            const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, cmd.text.c_str());
            const ImVec2 at     = aligned_text_pos(to_screen(f[0], f[1]), extent, state.text_align);
            draw_list->AddText(font, size, at, state.fill_color, cmd.text.c_str());
        }
        break;

        // -- refused ----------------------------------------------------------------------------------
        case VgCommand::Type::PathWinding:
            // NanoVG marks a subpath as a hole in the one around it, which needs a winding rule ImDrawList
            // does not have; filling anyway would produce a solid shape where a donut was asked for
            unsupported("PathWinding (holes in filled paths)");
            break;

        case VgCommand::Type::ArcTo:
            // the tangent/fillet form: an arc of the given radius joining the current point to two others.
            // ImDrawList only has center-and-angles arcs, so this needs NanoVG's tangent construction.
            unsupported("ArcTo");
            break;

        case VgCommand::Type::RoundedRectVarying:
            // ImDrawList rounds corners by flags off one radius, not a radius per corner.
            unsupported("RoundedRectVarying");
            break;

        case VgCommand::Type::DebugDumpPathCache: break; // a NanoVG debugging aid with nothing to do here

        case VgCommand::Type::Invalid:
        default: unsupported("an unrecognized command"); break;
        }
    }

    draw_list->_Path.Size = 0;
}
