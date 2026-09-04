//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "box.h"
#include "fwd.h"
#include "json.h"
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
    /// The shapes a user can draw.
    enum class Shape : int
    {
        Rect = 0,
        Ellipse,
        Line,
        Arrow,
        Text,
        Freehand,

        COUNT
    };

    Shape shape = Shape::Rect;

    /// Geometry, in image pixel coordinates.
    /*!
        Every shape but Freehand is described by the first and last of these, which p0() and p1() name:
        opposite corners for Rect and Ellipse, in either order; tail and head for Line and Arrow, which do
        differ, an arrow's head being drawn at p1; the anchor alone for Text, whose extent is whatever the
        string measures. Freehand is the path itself, in the order it was drawn.

        Never empty, so p0() and p1() are always a point.
    */
    std::vector<float2> points{float2{0.f, 0.f}, float2{0.f, 0.f}};

    float2       &p0() { return points.front(); }
    float2       &p1() { return points.back(); }
    const float2 &p0() const { return points.front(); }
    const float2 &p1() const { return points.back(); }

    /// The one color the user picks: the outline of a shape, and the color of a Text annotation's glyphs.
    /**
        A subdued forest green by default: legible over most images, and not the white or orange a
        renderer's overlay is drawn in, so whose markup is whose stays readable.
    */
    float4 stroke_color{0.133f, 0.545f, 0.133f, 1.f};
    /// Interior of a closed shape. Alpha zero means unfilled, which is the default and the common case.
    float4 fill_color{0.f, 0.f, 0.f, 0.f};

    /// Screen pixels, so neither thickens nor shrinks as the view is zoomed.
    float stroke_width = 2.f;
    float font_size    = 16.f; ///< Shape::Text only
    /// VgCommand::TextAlign flags, saying where p0 sits relative to the text it anchors.
    int text_align = VgCommand::AlignLeft | VgCommand::AlignTop;

    std::string text;  ///< What a Shape::Text annotation says
    std::string label; ///< What the panel's row says; display_label() supplies one when this is empty

    /// Whether a Freehand path is drawn as a curve through its points rather than as straight segments.
    /**
        The points are the same either way, so this can be turned on and off without losing anything; every
        other shape ignores it.
    */
    bool smooth = false;

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

    /// The most handles any shape shows: four corners and four edge midpoints.
    static constexpr int MaxHandles = 8;

    /// Whether \p shape is resized by a box around it rather than by its own points.
    static bool boxed(Shape shape)
    {
        return shape == Shape::Rect || shape == Shape::Ellipse || shape == Shape::Freehand;
    }
};

/// Read and write an annotation as a session file stores it.
/**
    A field added to Annotation has to be added to both of these, and to the round-trip test that sweeps
    every field, or it will quietly come back as its default.
*/
void to_json(json &j, const Annotation &a);
void from_json(const json &j, Annotation &a);

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

/// Where \p a's handles sit, in image coordinates; returns how many were written to \p out.
/**
    The shapes resized by a box report four corners of it, in the order (lo, hi.x/lo.y, hi, lo.x/hi.y),
    then the midpoints of the edges leading away from each. Line and Arrow report their two endpoints, and
    Text none: it can be moved but has nothing to resize. Drawing and hit testing both read this, so they
    cannot disagree about where a handle is.
*/
int annotation_handles(const Annotation &a, float2 out[Annotation::MaxHandles]);

/// Index of the handle of \p a within \p radius of \p screen_pos, or -1 if none is.
int handle_at(const Annotation &a, float2 screen_pos, const VgTransform &xform, float radius);

/// The polyline \p a is drawn as: its points, or the curve through them sampled closely enough to stand in
/// for it.
/**
    Hit testing reads this rather than the points, so what is clicked is what is drawn.
*/
std::vector<float2> annotation_path(const Annotation &a);

/// Drop the points of \p path that stray less than \p tolerance from the line their neighbors describe.
/**
    Ramer-Douglas-Peucker. A scribble captured a point at a time holds far more of them than its shape
    needs, which costs a session file its size and every frame its draw time.
*/
std::vector<float2> simplify_polyline(const std::vector<float2> &path, float tolerance);

/// Move \p a's handle \p index to \p to, both in image coordinates; returns that handle's index afterwards.
/**
    A corner or edge dragged past its opposite turns the shape inside out rather than stopping, which is
    what a rubber-band resize is expected to do. That re-orders the shape, and so renumbers its handles,
    which is why the index comes back: a drag held across the crossing has to keep hold of the same corner.
*/
int move_annotation_handle(Annotation &a, int index, float2 to);

/// Index of the annotation under \p screen_pos, searched front to back so the topmost wins, or -1.
/**
    \p slop widens every shape by that many screen pixels, so a thin line can still be picked up. Invisible
    and locked annotations are skipped: neither can be taken hold of.
*/
int annotation_at(const std::vector<Annotation> &annotations, float2 screen_pos, const VgTransform &xform, float slop);
