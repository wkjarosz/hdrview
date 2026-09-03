//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "box.h"
#include "fwd.h"
#include "vector_overlay.h"

#include <string>
#include <vector>

/*!
    A shape the user drew over an image, in that image's pixel coordinates.

    Distinct from Image::vector_overlay, which is what a renderer sends over IPC: a command stream,
    replaced wholesale whenever it sends again. These are the user's own markup, kept as shapes so they
    can be addressed and edited one at a time, and flattened into those same commands only to be drawn.

    Deliberately not image data: annotations stay out of the undo history, never mark an image modified,
    and are never rasterized. They are how the image is being looked at, like the exposure or the
    selection, so an image a renderer owns can carry them as freely as one loaded from a file.

    Geometry is in image pixels, so an annotation stays on the feature it was drawn over as the view is
    panned and zoomed. Stroke widths and text sizes are in *screen* pixels, so markup stays legible at any
    zoom.
*/
struct Annotation
{
    /// The shapes a user can draw. Two points describe every one of them; see p0/p1.
    enum class Shape : int
    {
        Rect = 0,
        Ellipse,
        Line,
        Arrow,
        Text,

        COUNT
    };

    Shape shape = Shape::Rect;

    /// Geometry, in image pixel coordinates.
    /*!
        Rect and Ellipse take p0 and p1 as opposite corners, in either order. Line and Arrow take them as
        tail and head, which do differ: an arrow's head is drawn at p1. Text uses p0 as its anchor and
        leaves p1 equal to it, since its extent is whatever the string measures at the current font size.
    */
    float2 p0{0.f, 0.f}, p1{0.f, 0.f};

    /// The one color the user picks: the outline of a shape, and the color of a Text annotation's glyphs.
    /**
        Cyan by default, which stands out over most images and is not the white or orange a renderer's
        overlay is drawn in, so whose markup is whose stays readable.
    */
    float4 stroke_color{0.2f, 0.9f, 1.f, 1.f};
    /// Interior of a closed shape. Alpha zero means unfilled, which is the default and the common case.
    float4 fill_color{0.f, 0.f, 0.f, 0.f};

    /// Screen pixels, so neither thickens nor shrinks as the view is zoomed.
    float stroke_width = 2.f;
    float font_size    = 16.f; ///< Shape::Text only
    /// VgCommand::TextAlign flags, saying where p0 sits relative to the text it anchors.
    int text_align = VgCommand::AlignLeft | VgCommand::AlignTop;

    std::string text;  ///< What a Shape::Text annotation says
    std::string label; ///< What the panel's row says; display_label() supplies one when this is empty

    bool visible = true; ///< Drawn, and hit-tested; an invisible annotation is neither
    bool locked  = false;

    /// Axis-aligned bounds in image pixel coordinates.
    /*!
        The geometry only. A stroke straddles the outline and text extends from its anchor, so what is
        drawn reaches a little past this; callers pad it themselves, the padding being a screen quantity
        and this an image one.
    */
    Box2f bounds() const;

    /// What the panel's row shows: the label, else the text, else the shape's name.
    std::string display_label() const;
};

/// Human-readable name for \p shape, as the panel and the shape picker show it.
const char *annotation_shape_name(Annotation::Shape shape);

/*!
    Append \p a to \p out as overlay drawing commands.

    \param [] out   Commands are appended; existing ones are left alone
    \param [] a     The annotation, drawn even if it is not visible -- callers filter
    \param [] scale Screen pixels per image pixel, needed only to size an arrowhead, whose proportions are
                    a screen quantity while its geometry has to be expressed in image coordinates
*/
void append_vg_commands(std::vector<VgCommand> &out, const Annotation &a, float scale);

/// Flatten every visible annotation of \p annotations, in order, so later ones draw over earlier ones.
std::vector<VgCommand> to_vg_commands(const std::vector<Annotation> &annotations, float scale);
