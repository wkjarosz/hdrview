//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ImDrawList;

/*!
    A little retained drawing program layered over an image, in that image's pixel coordinates.

    This is the shape tev's `VectorGraphics` IPC packet carries, kept here rather than beside the protocol
    because an overlay on an image is not inherently a network thing -- the image model stores one, and the
    viewport draws it, neither of which should have to know a socket exists.

    The model is NanoVG's, since that is what the protocol was written against: a mutable graphics state
    (colors, widths, font) with a save/restore stack, and a path built up from subpaths that is then
    stroked, filled, or both. Commands carry their arguments as a flat run of floats whose length is fixed
    per type, plus a string for the two types that need one.

    An independent reimplementation from the protocol's description, as in ipc_packet.h: tev draws these
    commands through NanoVG, where the interpreter here targets Dear ImGui's draw lists.
*/
struct VgCommand
{
    //! Command numbering is the wire protocol's; see tev's VectorGraphics.h.
    enum class Type : int8_t
    {
        Invalid            = 127,
        Save               = 0,
        Restore            = 1,
        FillColor          = 2,
        Fill               = 3,
        StrokeColor        = 4,
        Stroke             = 5,
        BeginPath          = 6,
        ClosePath          = 7,
        PathWinding        = 8,
        DebugDumpPathCache = 9,
        MoveTo             = 10,
        LineTo             = 11,
        ArcTo              = 12,
        Arc                = 13,
        BezierTo           = 14,
        Circle             = 15,
        Ellipse            = 16,
        QuadTo             = 17,
        Rect               = 18,
        RoundedRect        = 19,
        RoundedRectVarying = 20,
        Text               = 21,
        TextAlign          = 22,
        FontFace           = 23,
        FontSize           = 24,
        StrokeWidth        = 25,
    };

    //! Whether a size is in image pixels (and so scales with zoom) or in screen pixels.
    enum ScaleKind : int
    {
        Relative = 0,
        Absolute = 1,
    };

    //! Bit flags, matching NanoVG's NVG_ALIGN_*.
    enum TextAlign : int
    {
        AlignLeft     = 1 << 0,
        AlignCenter   = 1 << 1,
        AlignRight    = 1 << 2,
        AlignTop      = 1 << 3,
        AlignMiddle   = 1 << 4,
        AlignBottom   = 1 << 5,
        AlignBaseline = 1 << 6,
    };

    Type               type = Type::Invalid;
    std::vector<float> data;
    std::string        text; //!< only Text and FontFace carry one

    //! How many floats \p type expects in `data`, or -1 for a type that is not one of ours.
    /*!
        The wire format has no per-command length, so this table is what makes a command stream parseable
        at all: each command's arguments run exactly this long and the next command starts after them.
    */
    static int num_floats(Type type);

    static bool has_text(Type type) { return type == Type::Text || type == Type::FontFace; }
};

//! What draw_vector_overlay() needs to know about where the image is on screen.
struct VgTransform
{
    //! Image pixel coordinates to ImGui screen coordinates. Usually HDRViewApp::app_pos_at_pixel().
    std::function<float2(float2)> to_screen;
    //! Screen pixels per image pixel, for sizes flagged Relative. Always positive, even when flipped.
    float scale = 1.f;
    //! Resolves a NanoVG font-face name ("sans", "sans-bold", "mono") to a font; may return nullptr.
    std::function<void *(const std::string &)> font_for;
    //! Font the overlay starts with, and falls back to when a face name is unknown.
    void *default_font = nullptr;
};

/*!
    Execute \p commands into \p draw_list.

    Unsupported commands are skipped rather than approximated -- an overlay that quietly draws something
    other than what was asked for is worse than one with a hole in it, since the whole point of it is to be
    trusted as a diagnostic. Each distinct one is reported once via \p on_unsupported, which is expected to
    log; it is called with a stable name so a caller can rate-limit on it.

    \param [] draw_list      Where to draw; the overlay leaves its path state as it found it
    \param [] commands       The program, executed in order
    \param [] xform          Where the image is on screen, and how big
    \param [] default_color  Stroke and fill color before the program sets one
    \param [] on_unsupported Called with the name of a command that was skipped, if any
*/
void draw_vector_overlay(ImDrawList *draw_list, const std::vector<VgCommand> &commands, const VgTransform &xform,
                         uint32_t default_color, const std::function<void(const char *)> &on_unsupported = {});
