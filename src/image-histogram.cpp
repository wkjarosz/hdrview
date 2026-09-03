//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "fwd.h"

#include "app.h"
#include "fonts.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include <hello_imgui/dpi_aware.h>

#include <string>

using namespace std;
using namespace HelloImGui;

/// What place_clip_warning_toggles() hit-tested, for paint_clip_warning_toggles() to render afterwards.
struct ClipWarningToggles
{
    ImRect rect[2];
    bool   hovered[2], held[2];
};

/// Index of a group's alpha channel, or -1 if it has none. Alpha is always the group's last channel.
static int alpha_channel_index(const ChannelGroup &group)
{
    switch (group.type)
    {
    case ChannelGroup::RGBA_Channels:
    case ChannelGroup::XYZA_Channels:
    case ChannelGroup::YCA_Channels:
    case ChannelGroup::YA_Channels: return group.num_channels - 1;
    default: return -1;
    }
}

/**
    Hit-tests the two clip-warning toggles in the histogram's top corners -- shadows on the left, highlights
    on the right -- flipping \p warnings when one is clicked, and reports where they landed so
    paint_clip_warning_toggles() can render them later. \p ui_font_size is the app's normal text size,
    captured before the plot's reduced font was pushed, and is used for the tooltips.

    Call between BeginPlot and EndPlot, before the drag tools. ImPlot hit-tests the whole plot rect with
    ImGuiButtonFlags_AllowOverlap, so later items can still claim hover from it, but DragLineX's grab rect
    does not allow overlap and spans the full plot height, so whichever is submitted first owns the corner.
    These claim it, and a drag line under one stays grabbable over the rest of its height. Painting is
    deferred to the opposite end of that ordering, so the buttons sit above the lines.
*/
static ClipWarningToggles place_clip_warning_toggles(bool2 &warnings, float ui_font_size)
{
    const ImVec2 plot_pos  = ImPlot::GetPlotPos();
    const ImVec2 plot_size = ImPlot::GetPlotSize();
    const float  sz        = ImTrunc(ImGui::GetFrameHeight());
    const float  pad       = EmSize(0.25f);

    static const char *ids[2]      = {"##shadow clipping", "##highlight clipping"};
    static const char *tooltips[2] = {"Toggle zebra stripes on values below the low clip bound.",
                                      "Toggle zebra stripes on values above the high clip bound."};

    // BeginPlot() already advanced the layout cursor past the whole plot, so park it back once these
    // manually-positioned buttons are placed.
    const ImVec2       saved_cursor = ImGui::GetCursorScreenPos();
    ClipWarningToggles toggles;

    for (int e = 0; e < 2; ++e)
    {
        bool &on = e == 0 ? warnings.x : warnings.y;
        ImGui::SetCursorScreenPos(
            ImVec2{e == 0 ? plot_pos.x + pad : plot_pos.x + plot_size.x - pad - sz, plot_pos.y + pad});
        ImGui::InvisibleButton(ids[e], ImVec2{sz, sz});
        if (ImGui::IsItemClicked())
            on = !on;
        {
            // draw_histogram() runs under a reduced font so the plot's tick labels stay compact, so the
            // caller has to supply the app's normal size: style.FontSizeBase now reports the reduced one.
            ImGui::ScopedFont f{nullptr, ui_font_size};
            ImGui::SetItemTooltip("%s", tooltips[e]);
        }

        toggles.rect[e]    = ImRect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        toggles.hovered[e] = ImGui::IsItemHovered();
        toggles.held[e]    = ImGui::IsItemActive();
    }
    ImGui::SetCursorScreenPos(saved_cursor);
    return toggles;
}

/**
    Paints the clip-warning toggles placed by place_clip_warning_toggles(): a rounded square button holding
    an upward isosceles triangle, i.e. an inverted take on a combo box's dropdown arrow button.

    The button's background says whether that end's zebra striping is enabled (dark when off, light when on,
    lighter still under the cursor), while the triangle says what is clipping right now, so it stays a live
    indicator whether or not the striping is switched on. \p clip_colors carries that per-end color, with
    `w == 0` meaning nothing crosses that bound.

    Call between BeginPlot and EndPlot, after the drag tools, so the buttons cover the vertical lines.
*/
static void paint_clip_warning_toggles(const ClipWarningToggles &toggles, const bool2 &warnings,
                                       const float4 clip_colors[2])
{
    ImDrawList *draw_list = ImGui::GetWindowDrawList();

    for (int e = 0; e < 2; ++e)
    {
        const bool   on       = e == 0 ? warnings.x : warnings.y;
        const ImRect rect     = toggles.rect[e];
        const float  sz       = rect.GetWidth();
        const float  rounding = ImGui::GetStyle().FrameRounding;

        // ImGuiCol_FrameBgHovered is a translucent wash in both themes, so layering it over the base lifts
        // either state a further step rather than replacing it
        draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(on ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg),
                                 rounding);
        if (toggles.hovered[e] || toggles.held[e])
            draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), rounding);

        // apex above the center, base an equal distance below it, so the triangle sits centered in the square
        const float  cx = rect.Min.x + 0.5f * sz, cy = rect.Min.y + 0.5f * sz;
        const float  hw = ImTrunc(sz * 0.28f), hh = ImTrunc(sz * 0.20f);
        const ImVec2 apex{cx, cy - hh}, base_l{cx - hw, cy + hh}, base_r{cx + hw, cy + hh};

        const ImU32 tri = clip_colors[e].w > 0.f ? ImGui::ColorConvertFloat4ToU32(clip_colors[e])
                                                 : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw_list->AddTriangleFilled(base_r, base_l, apex, tri);
    }
}

/// Fraction of its usual alpha a display-range mark keeps while the range it describes is out of reach.
static constexpr float unreachable_alpha = 0.5f;

/**
    Names the two halves of what the display can show, the way Lightroom does, as tick labels on a second
    x axis along the top of the plot.

    Up to display value 1 is ordinary SDR; above that, up to \p ceiling_x, is the headroom the display
    currently has, which grows as the display is dimmed (see HDRViewApp::display_headroom()). Highlighting
    that whole span and dimming beyond it is the caller's DragRects; this only supplies the two names.

    ImPlot colors every tick on an axis alike, so a name that has to differ from its neighbor cannot be one
    of them: when the HDR band is out of reach its name is withheld and handed back for the caller to draw
    itself, in the row and at the offset ImPlot would have used.

    They are handed to ImPlot as ticks rather than drawn directly so that they get exactly the treatment
    the bottom axis's tick labels get: ImPlot reserves room for them above the plot and clips them to the
    widget. Drawing the text ourselves meant either covering the histogram or spilling past the window
    border, since the space above the plot belongs to ImPlot's layout, not to us.

    The axis has no gridlines, tick marks or interaction of its own -- a tick mark would point into the
    middle of a band as though it marked a value there. Its range and scale are copied from X1 so the two
    stay in lockstep as the exposure moves and the user pans.

    Call during the plot's setup phase, after X1's limits and scale are set, since it reads the range X1
    settled on. Being set up rather than drawn, the labels follow the exposure a frame behind a drag in
    progress, as X1's own ticks do.

    \param sdr_x       Plot-space x of display values 0 and 1
    \param ceiling_x   Plot-space x of the headroom ceiling
    \param has_hdr     Whether there is any headroom to name; when false only the SDR band is labeled
    \param hdr_dimmed  Whether the HDR band is out of reach, and so is to be named by the caller
    \param x_scale     The scale X1 was just given, needed to center each label within its band
    \return            Plot-space x of the withheld HDR name, or nullopt when the caller has none to draw
*/
static optional<double> setup_display_range_axis(const Box1d &sdr_x, double ceiling_x, bool has_hdr, bool hdr_dimmed,
                                                 AxisScale x_scale)
{
    const ImPlotPlot *plot  = ImPlot::GetCurrentPlot();
    const ImPlotRange range = plot->Axes[ImAxis_X1].Range;

    // Work in the axis's warped space, the one ImPlot lays the axis out linearly in, so that a fraction
    // of the range is also a fraction of the plot's width.
    const double lo = axis_scale_fwd(range.Min, x_scale), hi = axis_scale_fwd(range.Max, x_scale);
    const double span = hi - lo;

    double      values[2];
    const char *labels[2];
    int         n = 0;

    // Where a band's name centers, or nullopt if it has nowhere to go.
    auto name_pos = [&](double a, double b, const char *label) -> optional<double>
    {
        if (span <= 0.0)
            return {};

        // Clamp to what is on screen, so a band running off an edge still centers its label in the part
        // that can be read.
        const double wa = ImMax(axis_scale_fwd(a, x_scale), lo), wb = ImMin(axis_scale_fwd(b, x_scale), hi);

        // Leave a band too narrow for its name unlabeled: ImPlot draws every tick it is handed, so two that
        // no longer fit side by side would overlap. The plot rect is a frame stale here, and empty on the
        // very first frame, where assuming they fit keeps the axis from appearing late.
        const float plot_w = plot->PlotRect.GetWidth();
        if (plot_w > 0.f && (float)((wb - wa) / span) * plot_w < ImGui::CalcTextSize(label).x + EmSize(0.5f))
            return {};

        // Midway along the band's pixels, which is where the label centers; midway in value space would sit
        // visibly off-center on the nonlinear scales.
        return axis_scale_inv(0.5 * (wa + wb), x_scale);
    };

    auto add_tick = [&](optional<double> v, const char *label)
    {
        if (!v)
            return;
        values[n] = *v;
        labels[n] = label;
        ++n;
    };

    add_tick(name_pos(sdr_x.min.x, sdr_x.max.x, "SDR"), "SDR");

    optional<double> dimmed_hdr_name;
    if (has_hdr)
    {
        auto hdr_name = name_pos(sdr_x.max.x, ceiling_x, "HDR");
        if (hdr_dimmed)
            dimmed_hdr_name = hdr_name;
        else
            add_tick(hdr_name, "HDR");
    }

    // A second labeling of X1, not an independent axis, so it faces the way X1 faces and takes whichever
    // side X1 leaves free; X1's own context menu can invert it or send it opposite. Its flags survive from
    // frame to frame (ImPlotAxis::Reset() leaves them alone) and are read back here.
    //
    // It gets no menu of its own: ImPlot's axis menu writes Invert and Opposite straight into the flags with
    // no way to omit those entries, and SetupAxis only reapplies flags that have themselves changed, so a
    // toggle there would stick.
    const ImPlotAxisFlags x1_flags = plot->Axes[ImAxis_X1].Flags;
    ImPlotAxisFlags x2_flags = ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoMenus |
                               ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_Lock;
    if (!(x1_flags & ImPlotAxisFlags_Opposite))
        x2_flags |= ImPlotAxisFlags_Opposite;
    if (x1_flags & ImPlotAxisFlags_Invert)
        x2_flags |= ImPlotAxisFlags_Invert;

    ImPlot::SetupAxis(ImAxis_X2, nullptr, x2_flags);
    // The custom transform covers AxisScale_Linear too, where it is the identity, so X2 needs no
    // equivalent of the switch X1 is set up with.
    ImPlot::SetupAxisScale(ImAxis_X2, axis_scale_fwd_xform, axis_scale_inv_xform, &hdrview()->histogram_x_scale());
    ImPlot::SetupAxisLimits(ImAxis_X2, range.Min, range.Max, ImPlotCond_Always);
    ImPlot::SetupAxisTicks(ImAxis_X2, values, n, labels);

    return dimmed_hdr_name;
}

/**
    Draws each display range's extent into the top axis as a square bracket over the band: a run out from
    either side of the band's name, each turning back toward the axis at the boundary it reaches.

    ImPlot draws the names themselves, as that axis's tick labels (see setup_display_range_axis()), and
    does so after this runs, so they are laid over the gap the bracket leaves between its two runs.

    A boundary that has run off the plot gets no leg, and that side simply carries on to the edge, which
    is what says the band continues past it. Two quite different things land outside: display 0 misses by
    a hair, the axis starting fractionally above zero rather than at it, so a boundary all but inside is
    pulled in rather than dropped; the headroom ceiling misses by a mile on the linear and sRGB scales,
    which stop a fixed distance past white instead of widening to reach it, and so cannot show a ceiling
    at all once the display has real headroom.

    Call between BeginPlot and EndPlot. Takes plot-space x, and follows whichever side and direction the
    axis ended up on, so it rides the bottom axis's Invert and Opposite along with everything else.

    \param hdr_dimmed       Whether the HDR band is out of reach, and so drawn dimmed
    \param dimmed_hdr_name  Where to draw that band's name, when setup_display_range_axis() withheld it from
                            the axis rather than let it take the same color as its neighbor
*/
static void draw_display_range_extents(const Box1d &sdr_x, double ceiling_x, bool has_hdr, bool hdr_dimmed,
                                       optional<double> dimmed_hdr_name)
{
    const ImPlotPlot *plot = ImPlot::GetCurrentPlot();
    const ImPlotAxis &ax   = plot->Axes[ImAxis_X2];
    if (!ax.Enabled)
        return;

    ImDrawList        *draw_list = ImPlot::GetPlotDrawList();
    const ImPlotStyle &style     = ImPlot::GetStyle();
    const bool         opposite  = ax.IsOpposite();
    const float        txt_h     = ImGui::GetTextLineHeight();
    const float        x_lo = plot->PlotRect.Min.x, x_hi = plot->PlotRect.Max.x;

    // Down the middle of the row ImPlot puts this axis's tick labels in, mirroring its own axis rendering:
    // one LabelPadding out from the axis line, then half a line of text. Landed on a pixel center, so a
    // one-pixel stroke covers a single row exactly; every x below is snapped the same way.
    const float y_row =
        opposite ? ax.Datum1 - style.LabelPadding.y - 0.5f * txt_h : ax.Datum1 + style.LabelPadding.y + 0.5f * txt_h;
    const float y = ImFloor(y_row) + 0.5f;

    const float gap = EmSize(0.3f);
    // Below this the runs are too stubby to read as a bracket rather than as a pair of stray marks.
    const float min_run = EmSize(0.5f);
    // How far outside the plot a boundary may be and still be pulled in to the edge; see above.
    const float near_tol = 0.02f * (x_hi - x_lo);

    // A leg stops short of the boundary it marks, so neighboring brackets meeting at a shared boundary read
    // as separate spans, and short of the axis line, so the bracket is not welded to the plot's edge.
    const float leg_inset = 2.f;
    const float leg_end   = ax.Datum1 + (opposite ? -2.f : 2.f);

    auto boundary_px = [&](double v) { return (float)ImPlot::PlotToPixels(v, 0.0).x; };
    auto on_plot     = [&](float px) { return px >= x_lo - near_tol && px <= x_hi + near_tol; };
    auto snap        = [&](float px) { return ImFloor(ImClamp(px, x_lo, x_hi)) + 0.5f; };

    // Strokes are filled rects: no antialiased fringe, so they stay crisp on whole-pixel bounds. The color
    // is semitransparent, so the run stops against the leg; overlapping the two would darken the corner.
    auto stroke = [&](float x0, float y0, float x1, float y1, ImU32 col)
    { draw_list->AddRectFilled(ImVec2{ImMin(x0, x1), ImMin(y0, y1)}, ImVec2{ImMax(x0, x1), ImMax(y0, y1)}, col); };

    // One side of a bracket: the run out from the name, and the leg turning back towards the axis line at
    // the end of it. \p dir is +1 for the side running right. Without a leg the run carries on to the
    // boundary and off the plot.
    auto side = [&](float from, float edge, bool leg, float dir, ImU32 col)
    {
        const float x = edge - dir * leg_inset;
        stroke(from, y - 0.5f, leg ? x - dir * 0.5f : edge, y + 0.5f, col);
        if (leg)
            // Spans the run's whole row: reaching from y - 0.5 alone falls a row short where the axis is
            // below the plot and the leg climbs, leaving the corner unjoined.
            stroke(x - 0.5f, ImMin(y - 0.5f, leg_end), x + 0.5f, ImMax(y + 0.5f, leg_end), col);
    };

    auto bracket = [&](double va, double vb, const char *name, ImU32 col)
    {
        const float pa = boundary_px(va), pb = boundary_px(vb);
        const float a = snap(pa), b = snap(pb);
        const float w = ImGui::CalcTextSize(name).x;
        if (b - a < w + 2.f * (gap + min_run + leg_inset))
            return;

        // Both runs are placed by one offset either side of one center, so the two gaps around the name are
        // equal by construction. The center is the midpoint of the unsnapped boundaries, which is where
        // ImPlot centers the name: it puts the tick at the middle of the band in the axis's warped space,
        // and warped space maps to pixels linearly.
        const float center = IM_ROUND(0.5f * (ImClamp(pa, x_lo, x_hi) + ImClamp(pb, x_lo, x_hi)));
        const float reach  = IM_ROUND(0.5f * w + gap);
        side(center - reach, a, on_plot(pa), -1.f, col);
        side(center + reach, b, on_plot(pb), +1.f, col);
    };

    const ImU32 dim_col = ImGui::GetColorU32(ax.ColorTxt, unreachable_alpha);

    bracket(sdr_x.min.x, sdr_x.max.x, "SDR", ax.ColorTxt);
    if (has_hdr)
        bracket(sdr_x.max.x, ceiling_x, "HDR", hdr_dimmed ? dim_col : ax.ColorTxt);

    // The name ImPlot was not given, placed where it would have put it: centered on its tick, and one
    // LabelPadding out from the axis line -- the same row the runs above straddle the middle of.
    if (dimmed_hdr_name)
    {
        const float tx = (float)ImPlot::PlotToPixels(*dimmed_hdr_name, 0.0).x - 0.5f * ImGui::CalcTextSize("HDR").x;
        draw_list->AddText(ImVec2{tx, y_row - 0.5f * txt_h}, dim_col, "HDR");
    }
}

/**
    Draws the vertical line marking the ceiling of what the display can currently show.

    The ceiling is only ever as good as the peak the display reports, which on Wayland is whatever the
    compositor was told -- KDE writes a maxPeakBrightnessOverride from its HDR calibration wizard, and a
    careless run of it puts the ceiling several times further right than the panel can reach. Nothing in
    the reported numbers distinguishes that from an honest peak, so a ceiling that sits far from where
    values visibly clip is a reason to suspect the display's configuration, not this code.

    Call between BeginPlot and EndPlot. Takes plot-space x, so it follows the exposure and rides all three
    x-axis scales without knowing which is active.
*/
static void draw_display_ceiling_line(double ceiling_x, bool dimmed)
{
    // Raw ImDrawList calls aren't clipped to the data rectangle on their own, same as the additive fill
    // and the CIE diagram elsewhere in this file.
    ImPlot::PushPlotClipRect();
    const ImVec2 plot_pos  = ImPlot::GetPlotPos();
    const ImVec2 plot_size = ImPlot::GetPlotSize();
    const float  x         = ImPlot::PlotToPixels(ceiling_x, 0.0).x;
    ImPlot::GetPlotDrawList()->AddLine(ImVec2{x, plot_pos.y}, ImVec2{x, plot_pos.y + plot_size.y},
                                       ImGui::GetColorU32(ImGuiCol_Text, dimmed ? 0.5f * unreachable_alpha : 0.5f));
    ImPlot::PopPlotClipRect();
}

/**
    Draws the drag-to-resize grip below the histogram, modeled on the column-resize divider of a PE table:
    ImGui's TableUpdateBorders()/TableGetColumnBorderCol() rotated 90 degrees, reusing its 4px hit band, its
    delayed hover feedback, its Separator colors, and its double-click-to-restore gesture.
*/
static void draw_histogram_resize_grip()
{
    float &height = hdrview()->histogram_height();

    const float hit_half = ImTrunc(4.f * ImGui::GetCurrentContext()->CurrentDpiScale);
    const bool  pressed =
        ImGui::InvisibleButton("##histogram_resize", ImVec2{-FLT_MIN, 2.f * hit_half},
                               ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_PressedOnDoubleClick);
    const ImRect rect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};

    const bool hovered = ImGui::IsItemHovered();
    bool       held    = ImGui::IsItemActive();
    if (pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        height = HDRViewApp::default_histogram_height;
        ImGui::ClearActiveID();
        held = false;
    }
    if (held)
        height = ImClamp(height + ImGui::GetIO().MouseDelta.y / EmSize(1.f), 6.f, 40.f);

    // Hover feedback waits out a short timer, as a table's resize borders do, so brushing past the grip on the
    // way somewhere else doesn't flash the resize cursor.
    if ((hovered && ImGui::GetCurrentContext()->HoveredIdTimer > 0.06f) || held)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    // A short centered stub advertises the grip at rest; hovering or dragging extends it the full width, the
    // same way NoBordersInBodyUntilResize reveals a table's column border only while it is being addressed.
    const float y     = ImTrunc((rect.Min.y + rect.Max.y) * 0.5f);
    const float inset = (hovered || held) ? 0.f : 0.5f * (rect.GetWidth() - EmSize(3.f));
    ImGui::GetWindowDrawList()->AddLine(ImVec2{rect.Min.x + inset, y}, ImVec2{rect.Max.x - inset, y},
                                        ImGui::GetColorU32(held      ? ImGuiCol_SeparatorActive
                                                           : hovered ? ImGuiCol_SeparatorHovered
                                                                     : ImGuiCol_TableBorderStrong));
}

void Image::draw_histogram()
{
    static ImPlotCond plot_cond = ImPlotCond_Always;
    ImGui::SeparatorText("Histogram");

    // ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
    float combo_width = std::max(EmSize(5.f), 0.5f * (ImGui::GetContentRegionAvail().x - ImGui::IconButtonSize().x -
                                                      2.f * ImGui::GetStyle().ItemSpacing.x) -
                                                  (ImGui::CalcTextSize("X:").x + ImGui::GetStyle().ItemInnerSpacing.x));
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Y:");
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(combo_width);
    ImGui::Combo("##Y-axis type", &hdrview()->histogram_y_scale(), "Linear\0Log\0\0");
    ImGui::EndGroup();
    ImGui::Tooltip("Set the Y-axis scale type.\n\n"
                   "Linear: linear scale.\n"
                   "Log: logarithmic scale.");
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("X:");
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(combo_width);
    ImGui::Combo("##X-axis type", &hdrview()->histogram_x_scale(), "Linear\0sRGB\0Asinh\0\0");
    ImGui::EndGroup();
    ImGui::Tooltip("Set the X-axis scale type.\n\n"
                   "Linear: linear scale.\n"
                   "sRGB: sRGB gamma curve.\n"
                   "Asinh: a log-like scale that smoothly handles the transition from negative to "
                   "positive values. Useful for high dynamic range values.");
    ImGui::SameLine();

    if (ImGui::IconButton(plot_cond == ImPlotCond_Always ? ICON_MY_FIT_AXES : ICON_MY_MANUAL_AXES))
        plot_cond = (plot_cond == ImPlotCond_Always) ? ImPlotCond_Once : ImPlotCond_Always;
    ImGui::Tooltip((plot_cond == ImPlotCond_Always) ? "Click to allow manually panning/zooming in histogram"
                                                    : "Click to auto-fit histogram axes based on the exposure.");

    // ImGui::PopStyleVar();

    auto        hovered_pixel = int2{hdrview()->pixel_at_app_pos(ImGui::GetIO().MousePos)};
    float4      color32       = raw_pixel(hovered_pixel);
    auto       &group         = groups[selected_group];
    PixelStats *stats[4]      = {nullptr, nullptr, nullptr, nullptr};
    string      names[4];
    auto        colors = group.colors();

    // Each PixelStats holds one histogram per AxisScale; this picks the one the x axis is currently drawn in.
    const AxisScale x_scale = hdrview()->histogram_x_scale();

    float2 x_limits = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    float2 y_limits = x_limits;
    for (int c = 0; c < std::min(4, group.num_channels); ++c)
    {
        auto &channel = channels[group.channels[c]];
        channel.update_stats(c, hdrview()->current_image(), hdrview()->reference_image());
        stats[c]    = channel.get_stats();
        y_limits[0] = std::min(y_limits[0], stats[c]->hist_y_limits[x_scale][0]);
        y_limits[1] = std::max(y_limits[1], stats[c]->hist_y_limits[x_scale][1]);
        auto xl     = stats[c]->x_limits(hdrview()->exposure_live(), x_scale, hdrview()->display_headroom());
        x_limits[0] = std::min(x_limits[0], xl[0]);
        x_limits[1] = std::max(x_limits[1], xl[1]);
        names[c]    = Channel::tail(channel.name);
    }

    ImPlot::GetStyle().PlotMinSize = {100, 100};

    // PushFont() overwrites style.FontSizeBase with the size it pushes (see UpdateCurrentFontSize), so
    // capture the app's normal size here, before the plot's reduced font below.
    const float ui_font_size = ImGui::GetStyle().FontSizeBase;

    // Display values map into plot space through the live exposure and offset. Needed both to set up the
    // SDR/HDR axis, during the plot's setup phase, and to place the drag handles once it is open.
    auto display_to_plot = [](double d)
    { return (d - hdrview()->offset_live()) * pow(2.f, -hdrview()->exposure_live()); };

    ImGui::PushFont(hdrview()->font("sans regular"), ui_font_size * 10.f / 14.f);
    ImPlot::PushStyleVar(ImPlotStyleVar_AnnotationPadding, ImVec2{2.0, 0.0});
    // float4 plot_bg{0.35f, 0.35f, 0.35f, 1.f};
    // ImGui::PushStyleColor(ImGuiCol_WindowBg, plot_bg);
    if (ImPlot::BeginPlot("##Histogram", ImVec2(-1, EmSize(hdrview()->histogram_height()))))
    {
        ImPlot::GetInputMap().ZoomRate = 0.03f;
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoTickLabels);
        ImPlot::SetupAxisScale(ImAxis_Y1, hdrview()->histogram_y_scale() == AxisScale_Linear ? ImPlotScale_Linear
                                                                                             : ImPlotScale_SymLog);

        if (x_limits[0] == 0)
            x_limits[0] = 1e-14f;

        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);
        ImPlot::SetupAxesLimits(x_limits[0], x_limits[1], y_limits[0], y_limits[1], plot_cond);

        ImPlot::SetupMouseText(ImPlotLocation_SouthEast, ImPlotMouseTextFlags_NoFormat);
        // ImPlot only assigns these when they differ from what was passed last frame, so this positions the
        // legend once and still lets the user relocate it via the legend's own right-click menu.
        ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Horizontal);
        switch (hdrview()->histogram_x_scale())
        {
        case AxisScale_Linear: ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear); break;
        case AxisScale_SRGB:
        {
            ImPlot::SetupAxisScale(ImAxis_X1, axis_scale_fwd_xform, axis_scale_inv_xform,
                                   &hdrview()->histogram_x_scale());
            break;
        }
        case AxisScale_Asinh:
        case AxisScale_SymLog:
        {
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_SymLog);
            // ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, -INFINITY, INFINITY);
            ImPlot::SetupAxisScale(ImAxis_X1, axis_scale_fwd_xform, axis_scale_inv_xform,
                                   &hdrview()->histogram_x_scale());
            break;
        }
        }

        // "Clamp to LDR" caps the output at display white, so the highlighted region ends there and
        // everything naming the range beyond -- the HDR name and bracket, the ceiling line and its tag --
        // is dimmed with it.
        const bool hdr_dimmed = hdrview()->clamp_to_LDR();
        // Set during the plot's setup phase, drawn once it is open; see setup_display_range_axis().
        optional<double> dimmed_hdr_name;
        {
            // 0 means the display never told us its ceiling, which is not the same as having none.
            const float headroom = hdrview()->display_headroom();
            const bool  has_hdr  = headroom > 1.f;
            dimmed_hdr_name =
                setup_display_range_axis(Box1d{display_to_plot(0.0), display_to_plot(1.0)},
                                         display_to_plot(has_hdr ? headroom : 1.0), has_hdr, hdr_dimmed, x_scale);
        }

        //
        // now do the actual plotting
        //

        {
            // ImPlot/Dear ImGui only composite with straight-alpha "over", so overlapping translucent fills
            // cannot reproduce additive blending (red+green should read yellow, red+green+blue white). So
            // rasterize the fill here, one screen pixel column at a time: sample every channel's histogram
            // height at that column, sum the colors of whichever channels reach into each height band, and
            // hand ImGui one already-blended rectangle per band, leaving a single alpha-over to do.
            const int   n_channels = std::min(4, group.num_channels);
            const float fill_alpha = 0.75f;
            ImVec2      plot_pos   = ImPlot::GetPlotPos();
            ImVec2      plot_size  = ImPlot::GetPlotSize();
            int         px0        = (int)std::floor(plot_pos.x);
            int         px1        = (int)std::ceil(plot_pos.x + plot_size.x);
            ImDrawList *draw_list  = ImPlot::GetPlotDrawList();

            // The histogram is piecewise-constant (one count per bin), so a screen column shows the tallest
            // bin it covers; reducing by the maximum keeps a bin narrower than a pixel from falling between
            // samples, so a surviving gap is a range of values the source has no code for. The column's
            // edges go through value_to_bin(), since the plot's x range follows exposure while the binned
            // range follows the data.
            auto column_height = [x_scale](const PixelStats *s, double xa, double xb) -> float
            {
                int i0 = s->value_to_bin(std::min(xa, xb), x_scale);
                int i1 = s->value_to_bin(std::max(xa, xb), x_scale);
                if (i1 < 0 || i0 >= s->num_bins)
                    return 0.f;

                float h = 0.f;
                for (int i = std::max(i0, 0); i <= std::min(i1, s->num_bins - 1); ++i)
                    h = std::max(h, s->hist_ys[x_scale][i]);
                return h;
            };

            // Raw ImDrawList calls, unlike ImPlot's own Plot* items, aren't clipped to the plot's data
            // rectangle, so scope them explicitly.
            ImPlot::PushPlotClipRect();

            // This fill isn't a real ImPlot item, so it takes no part in ImPlot's legend-driven show/hide.
            // Mirror each channel's outline item, registered under the same label by PlotStairs below, so a
            // channel hidden via the legend drops out of the fill too; one not yet registered counts as
            // shown.
            std::array<bool, 4> shown{true, true, true, true};
            for (int c = 0; c < n_channels; ++c)
            {
                auto *item = ImPlot::GetItem(names[c].c_str());
                shown[c]   = !item || item->Show;
            }

            std::array<int, 4>   order;
            std::array<float, 4> heights;
            for (int px = px0; px < px1; ++px)
            {
                double xa = ImPlot::PixelsToPlot(ImVec2((float)px, plot_pos.y)).x;
                double xb = ImPlot::PixelsToPlot(ImVec2((float)px + 1.f, plot_pos.y)).x;
                double x  = 0.5 * (xa + xb);

                for (int c = 0; c < n_channels; ++c)
                {
                    heights[c] = shown[c] ? std::max(0.f, column_height(stats[c], xa, xb)) : 0.f;
                    order[c]   = c;
                }
                // Insertion sort: at most four elements, once per pixel column.
                for (int i = 1; i < n_channels; ++i)
                    for (int j = i; j > 0 && heights[order[j]] < heights[order[j - 1]]; --j)
                        std::swap(order[j], order[j - 1]);

                // Sweep bands from the baseline upward; the channels active in a band are exactly those
                // whose height reaches into it, so their colors sum additively.
                float prev_h = 0.f;
                for (int k = 0; k < n_channels; ++k)
                {
                    float h = heights[order[k]];
                    if (h <= prev_h)
                        continue;

                    float3 col{0.f};
                    for (int j = k; j < n_channels; ++j) col += colors[order[j]].xyz();
                    col = clamp(col, 0.f, 1.f);

                    ImVec2 py0 = ImPlot::PlotToPixels(x, prev_h);
                    ImVec2 py1 = ImPlot::PlotToPixels(x, h);
                    draw_list->AddRectFilled(ImVec2((float)px, std::min(py0.y, py1.y)),
                                             ImVec2((float)px + 1.f, std::max(py0.y, py1.y)),
                                             ImGui::GetColorU32(float4{col, fill_alpha}));

                    prev_h = h;
                }
            }

            ImPlot::PopPlotClipRect();
        }
        for (int c = 0; c < std::min(4, group.num_channels); ++c)
        {
            // PlotStairs holds each x[i]'s y-value constant across [x[i], x[i+1)), so hist_xs (each bin's
            // left edge) is what aligns the steps with the true bin boundaries. The stairs are invisible
            // (ImPlot skips line rendering at zero alpha) and the visible curve is the additive fill above,
            // but the item still owns the legend entry whose Show flag gates that fill.
            ImPlotSpec spec;
            spec.LineColor = float4{colors[c].xyz(), 0.25f};
            spec.FillColor = float4{0.f};
            ImPlot::PlotStairs(names[c].c_str(), stats[c]->hist_xs[x_scale].data(), stats[c]->hist_ys[x_scale].data(),
                               stats[c]->num_bins, spec);

            // Re-register the same label opaque to recolor its legend swatch: PlotDummy adopts the first
            // non-auto color in its spec as the legend icon color, and the legend isn't rendered until
            // EndPlot, so this geometry-free item wins.
            ImPlotSpec legend_spec;
            legend_spec.LineColor = float4{colors[c].xyz(), 1.f};
            ImPlot::PlotDummy(names[c].c_str(), legend_spec);
        }

        if (contains(hovered_pixel) && hdrview()->mouse_over_viewport())
        {
            for (int c = 0; c < std::min(4, group.num_channels); ++c)
            {
                ImPlotSpec spec;
                spec.LineColor  = float4{colors[c].xyz(), 1.0f};
                spec.FillColor  = float4{0.f};
                spec.Marker     = ImPlotMarker_Circle;
                spec.MarkerSize = 2.f;
                ImPlot::PlotStems(fmt::format("##hover_{}", c).c_str(), &color32[c],
                                  &stats[c]->bin_y(stats[c]->value_to_bin(color32[c], x_scale), x_scale), 1, 0, spec);

                ImPlot::TagX(color32[c], float4{colors[c].xyz(), 1.0f}, "%s", "");
            }
        }

        auto &warnings = hdrview()->clip_warnings();
        auto  toggles  = place_clip_warning_toggles(warnings, ui_font_size);

        Box1d xrange{-hdrview()->offset_live() * pow(2.f, -hdrview()->exposure_live()),
                     (1.0 - hdrview()->offset_live()) * pow(2.f, -hdrview()->exposure_live())};

        // 0 means the display never told us its ceiling, which is not the same as having none -- in that
        // case fall back to dimming above display 1, the pre-headroom behavior.
        const float headroom  = hdrview()->display_headroom();
        const bool  has_hdr   = headroom > 1.f;
        double      ceiling_x = has_hdr ? display_to_plot(headroom) : xrange.max.x;
        // Non-const: DragRect takes a mutable pointer, though NoInputs keeps it from ever writing back.
        double dim_from_x = hdr_dimmed ? xrange.max.x : ceiling_x;

        auto plt_range = ImPlot::GetPlotLimits(ImAxis_X1);
        ImPlot::DragRect(0, &plt_range.X.Min, &plt_range.Y.Min, &xrange.min.x, &plt_range.Y.Max,
                         ImVec4(0.0, 0.0, 0.0, 1.5), ImPlotDragToolFlags_NoInputs | ImPlotDragToolFlags_NoFit);
        // Dim from the display's ceiling, not from white: the range between the two is headroom this display
        // can show, so it belongs to the highlighted region until clamping puts it out of reach.
        ImPlot::DragRect(0, &dim_from_x, &plt_range.Y.Min, &plt_range.X.Max, &plt_range.Y.Max,
                         ImVec4(0.0, 0.0, 0.0, 1.5), ImPlotDragToolFlags_NoInputs | ImPlotDragToolFlags_NoFit);

        // Displayed values (d) are related to stored values (p) via the exposure and offset:
        // d = p * (2 ^ e) + o;
        // White is d = 1, and black is d = 0.
        // When dragging the white and black point handles we solve the 2x2 linear system for e and o
        bool2 released{false, false};
        if (ImPlot::DragLineX(0, &xrange.min.x, ImVec4(0, 0, 0, 1), 2,
                              ImPlotDragToolFlags_NoFit | ImPlotDragToolFlags_Delayed, &released.x))
        {
            float range                = max((float)xrange.size().x, 1e-10f);
            hdrview()->exposure_live() = -log2((float)range);
            // if invalid, drag white handle with black handle
            hdrview()->offset_live() = -(float)xrange.min.x / range;
        }
        if (ImPlot::DragLineX(1, &xrange.max.x, ImVec4(1, 1, 1, 1), 2,
                              ImPlotDragToolFlags_NoFit | ImPlotDragToolFlags_Delayed, &released.y))
        {
            float range                = max((float)xrange.size().x, 1e-10f);
            hdrview()->exposure_live() = -log2((float)range);
            // if invalid, drag black handle with white handle
            hdrview()->offset_live() = -((float)xrange.max.x - range) / range;
        }
        if (la::any(released))
            hdrview()->exposure() = hdrview()->exposure_live();

        xrange = Box1d{-hdrview()->offset_live() * pow(2.f, -hdrview()->exposure_live()),
                       (1.0 - hdrview()->offset_live()) * pow(2.f, -hdrview()->exposure_live())};

        ImPlot::TagX(xrange.min.x, ImVec4(0, 0, 0, 1), "0");
        ImPlot::TagX(xrange.max.x, ImVec4(1, 1, 1, 1), "1");

        // Re-derived from the exposure the drags just settled on, so the ceiling tracks the handles
        // within the same frame rather than lagging them by one.
        if (has_hdr)
        {
            ceiling_x = display_to_plot(headroom);
            draw_display_ceiling_line(ceiling_x, hdr_dimmed);
            // Gray rather than white: this is the display telling us where it stops, not a control the
            // user set, and it should not read as a third handle alongside the black and white points.
            ImPlot::TagX(ceiling_x, ImVec4(0.6f, 0.6f, 0.6f, hdr_dimmed ? unreachable_alpha : 1.f), "%.3gx", headroom);
        }
        draw_display_range_extents(xrange, ceiling_x, has_hdr, hdr_dimmed, dimmed_hdr_name);

        // The shader compares clip_range against display values (d = p * gain + offset), so map it into plot
        // space -- which holds stored values p -- the same way the black/white points above do.
        auto gain       = pow(2.f, hdrview()->exposure_live());
        auto offset     = hdrview()->offset_live();
        auto clip_range = double2((hdrview()->clip_range() - offset) / gain);
        if (warnings.x)
        {
            if (ImPlot::DragLineX(2, &clip_range.x, ImVec4(0, 0, 0, 1), 1, ImPlotDragToolFlags_Delayed))
                hdrview()->clip_range().x = (float)clip_range.x * gain + offset;
            ImPlot::TagX(clip_range.x, ImVec4(0, 0, 0, 1), "clip");
        }
        if (warnings.y)
        {
            if (ImPlot::DragLineX(3, &clip_range.y, ImVec4(1, 1, 1, 1), 1, ImPlotDragToolFlags_Delayed))
                hdrview()->clip_range().y = (float)clip_range.y * gain + offset;
            ImPlot::TagX(clip_range.y, ImVec4(1, 1, 1, 1), "clip");
        }

        // Color each triangle by which channels cross its bound, summing their canonical colors additively
        // as the histogram fill does: red plus blue reads magenta, all three read white. Alpha is left out,
        // matching the shader, which only tests rgb. Both bounds are in plot space already, and compare
        // against the extremes taking in infinities and markers, which the viewport stripes too.
        float4 clip_colors[2] = {float4{0.f}, float4{0.f}};
        for (int e = 0; e < 2; ++e)
        {
            float3 col{0.f};
            for (int c = 0; c < std::min(4, group.num_channels); ++c)
            {
                if (c == alpha_channel_index(group) || !stats[c])
                    continue;
                if (e == 0 ? stats[c]->summary.extreme_minimum < clip_range.x
                           : stats[c]->summary.extreme_maximum > clip_range.y)
                    col += colors[c].xyz();
            }
            if (col != float3{0.f})
                clip_colors[e] = float4{la::min(col, float3{1.f}), 1.f};
        }

        paint_clip_warning_toggles(toggles, warnings, clip_colors);

        ImPlot::EndPlot();
    }

    draw_histogram_resize_grip();
    // ImGui::PopStyleColor();
    ImPlot::PopStyleVar();
    ImGui::PopFont();
}
