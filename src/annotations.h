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

/// A shape the user drew over an image, in that image's pixel coordinates.
/*!
    View state, like the exposure: annotations stay out of the undo history, never mark an image modified,
    and are never rasterized, so an image a renderer owns can carry them as freely as one from a file.
    They are kept as shapes, addressable one at a time, and flattened into Image::vector_overlay's commands
    to be drawn.

    Geometry is in image pixels, a string's size among it, so it stays on the feature it was drawn over as
    the view is panned and zoomed. Stroke widths are in *screen* pixels, so a line stays easy to see at any
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

    /// Geometry, in image pixel coordinates. Never empty, so p0() and p1() always name a point.
    /*!
        Freehand is the path as drawn; every other shape uses the first and last alone. Rect and Ellipse
        take them as opposite corners, in either order, Line and Arrow as tail and head, an arrow's head
        being at p1, and Text as its anchor.
    */
    std::vector<float2> points{float2{0.f, 0.f}, float2{0.f, 0.f}};

    float2       &p0() { return points.front(); }
    float2       &p1() { return points.back(); }
    const float2 &p0() const { return points.front(); }
    const float2 &p1() const { return points.back(); }

    /// Outline of a shape, and the color of a Text annotation's glyphs.
    /** A subdued forest green: legible over most images, and not the white or orange a renderer uses. */
    float4 stroke_color{0.133f, 0.545f, 0.133f, 1.f};
    /// Interior of a closed shape. Alpha zero means unfilled, which is the default and the common case.
    float4 fill_color{0.f, 0.f, 0.f, 0.f};

    float stroke_width = 2.f;  ///< Screen pixels, so it neither thickens nor shrinks as the view is zoomed
    float font_size    = 16.f; ///< Shape::Text only, in image pixels, so it zooms with what it labels
    /// Which face a Shape::Text is drawn in, as a NanoVG face name; see annotation_font_faces().
    std::string font_face = "sans";
    /// VgCommand::TextAlign flags, saying where p0 sits relative to the text it anchors.
    int text_align = VgCommand::AlignLeft | VgCommand::AlignTop;

    std::string text;  ///< What a Shape::Text annotation says
    std::string label; ///< What the panel's row says; display_label() supplies one when this is empty

    /// Whether a Freehand path is curved through its points. The points are the same either way.
    bool smooth = false;

    bool visible = true; ///< Drawn, and hit-tested; an invisible annotation is neither
    bool locked  = false;

    /// Axis-aligned bounds of the geometry, in image pixel coordinates.
    /** A stroke straddles the outline, so what is drawn reaches past this; callers pad it themselves. */
    Box2f bounds() const;

    /// What the panel's row shows: the label, else the text, else the shape's name.
    std::string display_label() const;

    /// The most handles any shape shows: four corners and four edge midpoints.
    static constexpr int MaxHandles = 8;

    /// What a font size is kept between, in image pixels.
    static constexpr float MinFontSize = 1.f, MaxFontSize = 4096.f;

    /// Whether \p shape is resized by a box around it.
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

/// Where \p a's text sits, in image coordinates, or false when \p xform cannot measure it.
bool text_extent(const Annotation &a, const VgTransform &xform, float2 &lo, float2 &extent);

/// The faces a text annotation can be drawn in; the viewport falls back to the first for any other.
struct AnnotationFace
{
    const char *name;  ///< NanoVG face name, as stored and as the overlay interpreter expects it
    const char *label; ///< What the font popup calls it
};
const std::vector<AnnotationFace> &annotation_font_faces();

/// Human-readable name for \p shape, as the panel and the shape picker show it.
const char *annotation_shape_name(Annotation::Shape shape);

/// Append \p a to \p out as overlay drawing commands, visible or not; \p scale sizes an arrowhead.
void append_vg_commands(std::vector<VgCommand> &out, const Annotation &a, float scale);

/// Flatten every visible annotation of \p annotations, in order, so later ones draw over earlier ones.
std::vector<VgCommand> to_vg_commands(const std::vector<Annotation> &annotations, float scale);

/// Where \p a's handles sit, in image coordinates; returns how many were written to \p out.
/**
    A boxed shape reports four corners, in the order (lo, hi.x/lo.y, hi, lo.x/hi.y), then the midpoints of
    the edges leading away from each; Line and Arrow their two endpoints; Text its four corners, and none
    when \p xform cannot measure it. Drawing and hit testing both read this, so a handle is where it looks.
*/
int annotation_handles(const Annotation &a, float2 out[Annotation::MaxHandles], const VgTransform *xform = nullptr);

/// Index of the handle of \p a within \p radius of \p screen_pos, or -1 if none is.
int handle_at(const Annotation &a, float2 screen_pos, const VgTransform &xform, float radius);

/// The polyline \p a is drawn as: its points, or the curve sampled through them.
/** Hit testing reads this too, so what is clicked is what is drawn. */
std::vector<float2> annotation_path(const Annotation &a);

/// Drop the points of \p path that stray less than \p tolerance from the line their neighbors describe.
/** Ramer-Douglas-Peucker, run once on a captured scribble, which holds far more points than its shape. */
std::vector<float2> simplify_polyline(const std::vector<float2> &path, float tolerance);

/// Move \p a's handle \p index to \p to, both in image coordinates; returns that handle's index afterwards.
/**
    Dragged past its opposite, the shape turns inside out, which re-orders it and so renumbers its handles.
    The index comes back so a drag held across the crossing keeps hold of the same corner.

    Text has no geometry to stretch: its box scales the font size by how much taller the box became, and
    moves the anchor to hold the opposite corner still.
*/
int move_annotation_handle(Annotation &a, int index, float2 to, const VgTransform *xform = nullptr);

/// Index of the annotation under \p screen_pos, searched front to back so the topmost wins, or -1.
/** \p slop widens every shape by that many screen pixels. Invisible and locked ones are skipped. */
int annotation_at(const std::vector<Annotation> &annotations, float2 screen_pos, const VgTransform &xform, float slop);
