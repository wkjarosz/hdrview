//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "fwd.h"

#include "app.h"
#include "common.h"
#include "fonts.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "shader.h"
#include "texture.h"
#include <hello_imgui/dpi_aware.h>

#include <spdlog/fmt/chrono.h>
#include <string>

using namespace std;
using namespace HelloImGui;

static std::chrono::system_clock::time_point to_system_clock(std::filesystem::file_time_type ftime)
{
    using namespace std::chrono;
    return time_point_cast<system_clock::duration>(ftime - std::filesystem::file_time_type::clock::now() +
                                                   system_clock::now());
}

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

/*!
    Hit-tests the two clip-warning toggles in the histogram's top corners -- shadows on the left, highlights
    on the right -- flipping \p warnings when one is clicked, and reports where they landed so
    paint_clip_warning_toggles() can render them later. \p ui_font_size is the app's normal text size,
    captured before the plot's reduced font was pushed, and is used for the tooltips.

    Call between BeginPlot and EndPlot, *before* the drag tools. ImPlot hit-tests the whole plot rect with
    ImGuiButtonFlags_AllowOverlap, so later items can still claim hover from it, but DragLineX's grab rect
    does not allow overlap and spans the full plot height -- so whichever of the two is submitted first owns
    the corner. These claim it; a drag line under one stays grabbable over the rest of its height. Painting
    is deliberately deferred to the opposite end of that ordering, so the buttons sit *above* the lines.
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

    // BeginPlot() already advanced the layout cursor past the whole plot, so park it back where it was once
    // these manually-positioned buttons are placed -- otherwise whatever is drawn after EndPlot() starts at
    // the last button's corner instead of below the plot.
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
            // draw_histogram() runs under a reduced font so the plot's own tick labels stay compact; the
            // tooltip is ordinary UI text and should read at the app's normal size, which the caller has to
            // supply -- by now style.FontSizeBase reports the reduced size, not the app's.
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

/*!
    Paints the clip-warning toggles placed by place_clip_warning_toggles(): a rounded square button holding
    an upward isosceles triangle, i.e. an inverted take on a combo box's dropdown arrow button.

    The button's background says whether that end's zebra striping is enabled (dark when off, light when on,
    lighter still under the cursor), while the triangle says what is *currently* clipping, so it stays a live
    indicator whether or not the striping is switched on. \p clip_colors carries that per-end color, with
    `w == 0` meaning nothing crosses that bound.

    Call between BeginPlot and EndPlot, *after* the drag tools, so the buttons cover the vertical lines
    rather than the other way around.
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

/*!
    Names the two halves of what the display can show, the way Lightroom does, as tick labels on a second
    x axis along the top of the plot.

    Up to display value 1 is ordinary SDR; above that, up to \p ceiling_x, is the headroom the display
    currently has, which grows as the display is dimmed (see HDRViewApp::display_headroom()). Highlighting
    that whole span and dimming beyond it is the caller's DragRects; this only supplies the two names.

    They are handed to ImPlot as ticks rather than drawn directly so that they get exactly the treatment
    the bottom axis's tick labels get: ImPlot reserves room for them above the plot and clips them to the
    widget. Drawing the text ourselves meant either covering the histogram or spilling past the window
    border, since the space above the plot belongs to ImPlot's layout, not to us.

    The axis has no gridlines, tick marks or interaction of its own -- a tick mark would point into the
    middle of a band as though it marked a value there. Its range and scale are copied from X1 so the two
    stay in lockstep as the exposure moves and the user pans.

    Call during the plot's setup phase, *after* X1's limits and scale are set, since it reads the range X1
    settled on. Being set up rather than drawn, the labels follow the exposure a frame behind a drag in
    progress -- the same frame behind as X1's own ticks, which are fixed at setup for the same reason.

    \param sdr_x      Plot-space x of display values 0 and 1
    \param ceiling_x  Plot-space x of the headroom ceiling
    \param has_hdr    Whether there is any headroom to name; when false only the SDR band is labeled
    \param x_scale    The scale X1 was just given, needed to center each label within its band
*/
static void setup_display_range_axis(const Box1d &sdr_x, double ceiling_x, bool has_hdr, AxisScale x_scale)
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

    auto name_band = [&](double a, double b, const char *label)
    {
        if (span <= 0.0)
            return;

        // Clamp to what is on screen, so a band running off an edge still centers its label within the
        // part that can be read.
        const double wa = ImMax(axis_scale_fwd(a, x_scale), lo), wb = ImMin(axis_scale_fwd(b, x_scale), hi);

        // Leave a band too narrow for its name unlabeled. ImPlot draws every tick it is handed, so two
        // that no longer fit side by side would overlap rather than drop out. The plot rect is a frame
        // stale here, which is plenty for deciding whether three characters fit -- but it is empty on the
        // very first frame, where assuming they fit keeps the axis from appearing late and jogging the
        // layout.
        const float plot_w = plot->PlotRect.GetWidth();
        if (plot_w > 0.f && (float)((wb - wa) / span) * plot_w < ImGui::CalcTextSize(label).x + EmSize(0.5f))
            return;

        // Midway along the band's *pixels*, which is where the label centers. Midway in value space would
        // sit visibly off-center on the nonlinear scales.
        values[n] = axis_scale_inv(0.5 * (wa + wb), x_scale);
        labels[n] = label;
        ++n;
    };

    name_band(sdr_x.min.x, sdr_x.max.x, "SDR");
    if (has_hdr)
        name_band(sdr_x.max.x, ceiling_x, "HDR");

    // Face the way X1 faces and take whichever side it leaves free, rather than being fixed to the top
    // pointing right: X1's context menu lets the user invert it or send it to the opposite side, and this
    // is a second labeling of that axis, not an independent one. Its flags survive from frame to frame
    // (ImPlotAxis::Reset() leaves them alone), so they can simply be read back here.
    //
    // It gets no menu of its own for the same reason. ImPlot's axis menu writes Invert and Opposite
    // straight into the axis flags with no way to omit just those entries, and a toggle there would stick
    // rather than be corrected on the next frame -- SetupAxis only reapplies flags that have themselves
    // changed since it was last called.
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
}

/*!
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
*/
static void draw_display_range_extents(const Box1d &sdr_x, double ceiling_x, bool has_hdr)
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

    // Down the middle of the row ImPlot puts this axis's tick labels in, mirroring the placement in its
    // own axis rendering: one LabelPadding out from the axis line, then half a line of text.
    //
    // Landed on a pixel center rather than a boundary, so a one-pixel stroke covers a single row exactly
    // instead of half of each row either side of it. Every x below is snapped the same way.
    const float y_row =
        opposite ? ax.Datum1 - style.LabelPadding.y - 0.5f * txt_h : ax.Datum1 + style.LabelPadding.y + 0.5f * txt_h;
    const float y = ImFloor(y_row) + 0.5f;

    const float gap = EmSize(0.3f);
    // Below this the runs are too stubby to read as a bracket rather than as a pair of stray marks.
    const float min_run = EmSize(0.5f);
    // How far outside the plot a boundary may be and still be pulled in to the edge; see above.
    const float near_tol = 0.02f * (x_hi - x_lo);

    auto boundary_px = [&](double v) { return (float)ImPlot::PlotToPixels(v, 0.0).x; };
    auto on_plot     = [&](float px) { return px >= x_lo - near_tol && px <= x_hi + near_tol; };
    auto snap        = [&](float px) { return ImFloor(ImClamp(px, x_lo, x_hi)) + 0.5f; };

    // Strokes are rects rather than lines, since a filled rect carries no antialiased fringe and so stays
    // crisp on whole-pixel bounds. The color is semitransparent, so the corner belongs to the leg alone
    // and the run stops against it: overlapping the two would print a darker square there.
    auto stroke = [&](float x0, float y0, float x1, float y1)
    {
        draw_list->AddRectFilled(ImVec2{ImMin(x0, x1), ImMin(y0, y1)}, ImVec2{ImMax(x0, x1), ImMax(y0, y1)},
                                 ax.ColorTxt);
    };

    // One side of a bracket: the run out from the name, and the leg turning back to the axis line at the
    // end of it. \p dir is +1 for the side running right, so the run stops half a pixel short on the side
    // the leg occupies.
    auto side = [&](float from, float edge, bool leg, float dir)
    {
        stroke(IM_ROUND(from), y - 0.5f, leg ? edge - dir * 0.5f : edge, y + 0.5f);
        if (leg)
            stroke(edge - 0.5f, y - 0.5f, edge + 0.5f, ax.Datum1);
    };

    auto bracket = [&](double va, double vb, const char *name)
    {
        const float pa = boundary_px(va), pb = boundary_px(vb);
        const float a = snap(pa), b = snap(pb);
        const float w = ImGui::CalcTextSize(name).x;
        if (b - a < w + 2.f * (gap + min_run))
            return;

        const float center = 0.5f * (a + b);
        side(center - 0.5f * w - gap, a, on_plot(pa), -1.f);
        side(center + 0.5f * w + gap, b, on_plot(pb), +1.f);
    };

    bracket(sdr_x.min.x, sdr_x.max.x, "SDR");
    if (has_hdr)
        bracket(sdr_x.max.x, ceiling_x, "HDR");
}

/*!
    Draws the vertical line marking the ceiling of what the display can currently show.

    The ceiling is only ever as good as the peak the display reports, which on Wayland is whatever the
    compositor was told -- KDE writes a maxPeakBrightnessOverride from its HDR calibration wizard, and a
    careless run of it puts the ceiling several times further right than the panel can reach. Nothing in
    the reported numbers distinguishes that from an honest peak, so a ceiling that sits far from where
    values visibly clip is a reason to suspect the display's configuration, not this code.

    Call between BeginPlot and EndPlot. Takes plot-space x, so it follows the exposure and rides all three
    x-axis scales without knowing which is active.
*/
static void draw_display_ceiling_line(double ceiling_x)
{
    // Raw ImDrawList calls aren't clipped to the data rectangle on their own, same as the additive fill
    // and the CIE diagram elsewhere in this file.
    ImPlot::PushPlotClipRect();
    const ImVec2 plot_pos  = ImPlot::GetPlotPos();
    const ImVec2 plot_size = ImPlot::GetPlotSize();
    const float  x         = ImPlot::PlotToPixels(ceiling_x, 0.0).x;
    ImPlot::GetPlotDrawList()->AddLine(ImVec2{x, plot_pos.y}, ImVec2{x, plot_pos.y + plot_size.y},
                                       ImGui::GetColorU32(ImGuiCol_Text, 0.5f));
    ImPlot::PopPlotClipRect();
}

/*!
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

    // PushFont() overwrites style.FontSizeBase with whatever size it pushes (see UpdateCurrentFontSize), so
    // the plot's reduced font below makes the style's value unusable as "the app's normal size" from here on.
    // Capture it first, for the widgets inside the plot that want ordinary UI text.
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

        {
            // 0 means the display never told us its ceiling, which is not the same as having none, so
            // there is no HDR band to name in that case.
            const float headroom = hdrview()->display_headroom();
            const bool  has_hdr  = headroom > 1.f;
            setup_display_range_axis(Box1d{display_to_plot(0.0), display_to_plot(1.0)},
                                     display_to_plot(has_hdr ? headroom : 1.0), has_hdr, x_scale);
        }

        //
        // now do the actual plotting
        //

        {
            // ImPlot/Dear ImGui only ever composites with straight-alpha "over", so overlapping translucent
            // fills can't reproduce true additive blending (e.g. red+green overlap should read as yellow,
            // and red+green+blue as white). Instead, rasterize the fill ourselves one screen pixel column at
            // a time: sample every channel's histogram height at that column, sum the colors of whichever
            // channels reach into each height band, and hand ImGui a single already-blended rectangle per
            // band. That way only one ordinary alpha-over (against the plot background) is ever needed,
            // which is exact rather than an approximation.
            const int   n_channels = std::min(4, group.num_channels);
            const float fill_alpha = 0.75f;
            ImVec2      plot_pos   = ImPlot::GetPlotPos();
            ImVec2      plot_size  = ImPlot::GetPlotSize();
            int         px0        = (int)std::floor(plot_pos.x);
            int         px1        = (int)std::ceil(plot_pos.x + plot_size.x);
            ImDrawList *draw_list  = ImPlot::GetPlotDrawList();

            // The histogram is piecewise-constant (one count per bin), so a screen column shows the tallest
            // bin it covers. Reducing by the maximum keeps a bin narrower than a pixel from falling between
            // samples, so a gap that survives is a range of values the source has no code for. The column's
            // edges have to be resolved through value_to_bin() rather than scaled by their position, since
            // the plot's x range follows exposure while the binned range follows the data.
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

            // Raw ImDrawList calls (unlike ImPlot's own Plot* items, e.g. PlotStairs below) aren't
            // automatically clipped to the plot's data rectangle, so scope them explicitly -- same as the
            // CIE-diagram code further below in this file does around its own manual AddPolyline calls.
            ImPlot::PushPlotClipRect();

            // This fill isn't a real ImPlot item, so it doesn't participate in ImPlot's legend-driven
            // show/hide on its own. Mirror each channel's outline item (registered under the same label by
            // PlotStairs below) so a channel hidden via the legend also drops out of the fill; an item that
            // hasn't been registered yet (e.g. the very first frame) defaults to shown.
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
                std::sort(order.begin(), order.begin() + n_channels,
                          [&](int a, int b) { return heights[a] < heights[b]; });

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
            // left edge) rather than a bin center is what aligns the steps with the true bin boundaries.
            // The stairs themselves are invisible (ImPlot skips line rendering entirely at zero alpha) --
            // the visible curve is the additive fill rasterized above. The item still has to be plotted,
            // since it owns the legend entry whose Show flag gates that fill.
            ImPlotSpec spec;
            spec.LineColor = float4{colors[c].xyz(), 0.25f};
            spec.FillColor = float4{0.f};
            ImPlot::PlotStairs(names[c].c_str(), stats[c]->hist_xs[x_scale].data(), stats[c]->hist_ys[x_scale].data(),
                               stats[c]->num_bins, spec);

            // Re-register the same label opaque to recolor its legend swatch: PlotDummy adopts the first
            // non-auto color in its spec as the legend icon color, and the legend isn't rendered until
            // EndPlot, so this second, geometry-free item wins. Without it the swatch would inherit the
            // transparent line color above and the legend would be invisible.
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
        const float headroom = hdrview()->display_headroom();
        const bool  has_hdr  = headroom > 1.f;
        // Non-const: DragRect takes a mutable pointer, though NoInputs keeps it from ever writing back.
        double ceiling_x = has_hdr ? display_to_plot(headroom) : xrange.max.x;

        auto plt_range = ImPlot::GetPlotLimits(ImAxis_X1);
        ImPlot::DragRect(0, &plt_range.X.Min, &plt_range.Y.Min, &xrange.min.x, &plt_range.Y.Max,
                         ImVec4(0.0, 0.0, 0.0, 1.5), ImPlotDragToolFlags_NoInputs | ImPlotDragToolFlags_NoFit);
        // Dim from the display's ceiling rather than from white: the range between the two is real
        // headroom this display can show, so it belongs to the highlighted region.
        ImPlot::DragRect(0, &ceiling_x, &plt_range.Y.Min, &plt_range.X.Max, &plt_range.Y.Max,
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
            draw_display_ceiling_line(ceiling_x);
            // Grey rather than white: this is the display telling us where it stops, not a control the
            // user set, and it should not read as a third handle alongside the black and white points.
            ImPlot::TagX(ceiling_x, ImVec4(0.6f, 0.6f, 0.6f, 1.f), "%.3gx", headroom);
        }
        draw_display_range_extents(xrange, ceiling_x, has_hdr);

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
        // the way the histogram fill does -- red plus blue reads magenta, all three read white. Alpha is
        // left out, matching the shader, which only tests rgb. Both bounds are already in plot space, so
        // they compare directly against the stored per-channel extremes.
        float4 clip_colors[2] = {float4{0.f}, float4{0.f}};
        for (int e = 0; e < 2; ++e)
        {
            float3 col{0.f};
            for (int c = 0; c < std::min(4, group.num_channels); ++c)
            {
                if (c == alpha_channel_index(group) || !stats[c])
                    continue;
                if (e == 0 ? stats[c]->summary.minimum < clip_range.x : stats[c]->summary.maximum > clip_range.y)
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

void Image::draw_layer_groups(const Layer &layer, int img_idx, int &id_, bool is_current, bool is_reference,
                              bool short_names, int &visible_group, float &scroll_to)
{
    static constexpr ImGuiTreeNodeFlags tree_node_flags =
        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf |
        ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_Bullet;
    for (size_t g = 0; g < layer.groups.size(); ++g)
    {
        auto  &group      = groups[layer.groups[g]];
        string group_name = group.num_channels == 1 ? group.name : "(" + group.name + ")";
        string name       = string(ICON_MY_CHANNEL_GROUP) + " " + (short_names ? group_name : layer.name + group_name);

        // check if any of the contained channels pass the channel filter
        if (!group.visible)
            continue;

        bool is_selected_channel  = is_current && selected_group == layer.groups[g];
        bool is_reference_channel = is_reference && reference_group == layer.groups[g];

        ImGuiTreeNodeFlags flags =
            tree_node_flags |
            (is_selected_channel || is_reference_channel ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
        ImGui::TreeRow((void *)(intptr_t)id_++, flags, name.c_str(),
                       [&]
                       {
                           string shortcut = is_current && visible_group < 10
                                                 ? fmt::format(ICON_MY_KEY_CONTROL "{}", mod(visible_group + 1, 10))
                                                 : "";
                           ImGui::TextAligned2(0.0f, -FLT_MIN, shortcut.c_str());
                       },
                       [&]
                       { ImGui::PushRowColors(is_selected_channel, is_reference_channel, ImGui::GetIO().KeyShift); });

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            if (ImGui::GetIO().KeyShift)
            {
                spdlog::trace("Shift-clicked on {}", name);
                // check if we are already the reference channel group
                if (is_reference_channel)
                {
                    spdlog::trace("Clearing reference image");
                    hdrview()->set_reference_image_index(-1, true);
                    reference_group = -1;
                }
                else
                {
                    spdlog::trace("Setting reference image to {}", img_idx);
                    hdrview()->set_reference_image_index(img_idx);
                    reference_group = layer.groups[g];
                }
                set_as_texture(Target_Secondary);
            }
            else
            {
                hdrview()->set_current_image_index(img_idx);
                selected_group = layer.groups[g];
                set_as_texture(Target_Primary);
            }
        }
        else if (is_selected_channel && scroll_to >= -0.5f)
        {
            if (!ImGui::IsItemVisible())
                ImGui::SetScrollHereY(scroll_to);
            scroll_to = -1.f;
        }

        ++visible_group;
    }
}

/*!

*/
void Image::draw_layer_node(const LayerTreeNode &node, int img_idx, int &id_, bool is_current, bool is_reference,
                            int &visible_group, float &scroll_to)
{
    static constexpr ImGuiTreeNodeFlags tree_node_flags =
        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull;

    if (node.leaf_layer >= 0)
        // draw this node's leaf channel groups
        draw_layer_groups(layers[node.leaf_layer], img_idx, id_, is_current, is_reference, true, visible_group,
                          scroll_to);

    for (auto &c : node.children)
    {
        const LayerTreeNode &child_node = c.second;
        if (child_node.visible_groups == 0)
            continue;

        bool open =
            ImGui::TreeRow((void *)(intptr_t)id_++, tree_node_flags,
                           fmt::format("{} {}", ICON_MY_OPEN_IMAGE, child_node.name).c_str(), nullptr,
                           [&]
                           {
                               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                               ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGuiCol_Header);
                               ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGuiCol_Header);
                           });
        if (open)
        {
            draw_layer_node(child_node, img_idx, id_, is_current, is_reference, visible_group, scroll_to);
            ImGui::TreePop();
        }
        else
        {
            // still account for visible groups within the closed tree node
            visible_group += child_node.visible_groups;
        }
    }
}

int Image::draw_channel_rows(int img_idx, int &id_, bool is_current, bool is_reference, float &scroll_to)
{
    int visible_group = 0;
    for (size_t l = 0; l < layers.size(); ++l)
        draw_layer_groups(layers[l], img_idx, id_, is_current, is_reference, false, visible_group, scroll_to);

    return visible_group;
}

void Image::draw_info()
{
    std::locale loc("en_US.UTF-8");
    auto        bold_font = hdrview()->font("sans bold");

    static ImGuiTextFilter filter;
    const ImVec2           button_size   = ImGui::IconButtonSize();
    bool                   filter_active = filter.IsActive(); // save here to avoid flicker

    auto draw_filter_input = [&]()
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InputTextWithHint("##metadata filter",
                                     ICON_MY_FILTER "Filter (format: [include|-exclude][,...]; e.g. "
                                                    "\"include_this,-but_not_this,also_include_this\")",
                                     filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf)))
            filter.Build();
        if (filter_active)
        {
            ImGui::SameLine(0.f, 0.f);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
            if (ImGui::IconButton(ICON_MY_DELETE))
                filter.Clear();
        }
    };

    auto filtered_property = [&](const string &property_name, const string &value, const string &tooltip = "")
    {
        if (filter.PassFilter((property_name + " " + value).c_str()))
            ImGui::PE::WrappedText(property_name, value, tooltip, bold_font);
    };

    auto get_tooltip = [](const json &field_obj)
    {
        std::string tt;
        if (field_obj.contains("description") && field_obj["description"].is_string())
            tt += field_obj["description"].get<std::string>() + "\n\n";

        if (field_obj.contains("ifd") && field_obj["ifd"].is_number())
            tt += fmt::format("IFD: {}\n", field_obj["ifd"].get<int>());

        if (field_obj.contains("tag") && field_obj["tag"].is_number())
            tt += fmt::format("Tag: {}\n", field_obj["tag"].get<int>());

        if (field_obj.contains("type") && field_obj["type"].is_string())
            tt += fmt::format("Type: {}\n", field_obj["type"].get<std::string>());

        if (field_obj.contains("value"))
        {
            const auto &v = field_obj["value"];
            if (!v.is_object() && !v.is_string() &&
                (!v.is_array() || (v.is_array() && v.size() > 0 && v.size() <= 5 && v[0].is_number())))
                tt += fmt::format("Value: {}", v.dump());
        }
        return tt;
    };

    // Flat field drawer used for header/exif and other simple metadata sections.
    auto add_fields = [&](const json &fields)
    {
        for (auto &field : fields.items())
        {
            const std::string &key       = field.key();
            const auto        &field_obj = field.value();
            if (!field_obj.is_object() || !field_obj.contains("string"))
                continue;

            auto value  = field_obj["string"].get<std::string>();
            auto concat = key + " " + value;
            if (!filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                continue;

            ImGui::PE::WrappedText(key, value, get_tooltip(field_obj), bold_font);
        }
    };
    // Recursive drawer specifically for XMP nested structures.
    std::function<void(const json &, int, const string &)> add_xmp_fields =
        [&](const json &fields, int depth, const string &prefix)
    {
        for (const auto &[key, field_val] : fields.items())
        {
            // Determine display value
            std::string disp;
            if (field_val.is_string())
                disp = field_val.get<std::string>();
            else if (field_val.is_number())
                disp = field_val.dump();
            else if (field_val.is_boolean())
                disp = field_val.get<bool>() ? "true" : "false";

            auto concat = prefix + ":" + key + " " + disp;
            if (!filter.PassFilter(concat.c_str(), concat.c_str() + concat.size()))
                continue;

            // Handle objects (nested structures)
            if (field_val.is_object())
            {
                if (ImGui::PE::TreeNode(key.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull))
                {
                    add_xmp_fields(field_val, depth + 1, prefix + ":" + key);
                    ImGui::PE::TreePop();
                }
                continue;
            }

            // Handle arrays
            if (field_val.is_array())
            {
                const auto &arr = field_val;

                // If the array is all basic scalar types (string/number/bool), render it as a single
                // wrapped text entry containing the dumped JSON for readability.
                // bool all_basic = true;
                // for (const auto &e : arr)
                // {
                //     if (e.is_object())
                //     {
                //         all_basic = false;
                //         break;
                //     }
                // }

                // if (all_basic)
                // {
                //     ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                //     ImGui::PE::WrappedText(key, field_val.dump(), "", bold_font);
                //     ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                //     continue;
                // }

                // Otherwise, handle mixed or object arrays element-by-element as before.

                // we special case arrays with one element to avoid an unnecessary nesting
                bool open = true;
                if (arr.size() > 1)
                {
                    open = ImGui::PE::TreeNode(key.c_str(),
                                               ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::TextUnformatted(fmt::format("[{} item{}]", arr.size(), arr.size() == 1 ? "" : "s").c_str());
                }
                if (open)
                {
                    for (size_t i = 0; i < arr.size(); ++i)
                    {
                        std::string idx = arr.size() > 1 ? fmt::format("#{}", i + 1) : key;
                        if (arr[i].is_object())
                        {
                            bool open2 = ImGui::PE::TreeNode(idx.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                                                                              ImGuiTreeNodeFlags_DrawLinesFull);
                            if (arr.size() <= 1)
                            {
                                ImGui::TableNextColumn();
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                ImGui::TextUnformatted("[1 item]");
                            }
                            if (open2)
                            {
                                add_xmp_fields(arr[i], depth + 1, prefix + ":" + key);
                                ImGui::PE::TreePop();
                            }
                        }
                        else if (arr[i].is_string())
                        {
                            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                            ImGui::PE::WrappedText(idx, arr[i].get<std::string>(), "", bold_font);
                            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                        }
                        else
                        {
                            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                            ImGui::PE::Entry(idx, arr[i].dump());
                            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                        }
                    }
                    if (arr.size() > 1)
                        ImGui::PE::TreePop();
                }
                continue;
            }

            // Scalar values
            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
            ImGui::PE::WrappedText(key, disp, "", bold_font);
            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
        }
    };

    auto draw_no_metadata_message = [&](const char *message)
    {
        ImGui::Indent(ImGui::GetStyle().CellPadding.x);
        ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase);
        ImGui::BeginDisabled();
        ImGui::TextUnformatted(message);
        ImGui::EndDisabled();
        ImGui::PopFont();
        ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
    };

    bool has_exif   = exif.valid() && metadata.contains("exif") && metadata["exif"].is_object();
    bool has_xmp    = !xmp_data.empty() && metadata.contains("xmp") && metadata["xmp"].is_object();
    bool has_header = metadata.contains("header") && metadata["header"].is_object();

    bool has_view[] = {true, has_header, has_exif, has_xmp, has_xmp};

    static int         selected_view    = 0;
    static const char *views[]          = {"General", "Header", "EXIF", "XMP", "Raw XMP data"};
    static const char *views_disabled[] = {"General", "Header", "EXIF (not present)", "XMP (not present)",
                                           "Raw XMP data (not present)"};

    float w = ImGui::GetContentRegionAvail().x - 1.f * (button_size.x + ImGui::GetStyle().ItemSpacing.x);

    static bool expand_to_listbox = false;

    auto show_view_options = [&]()
    {
        for (int n = 0; n < IM_ARRAYSIZE(views); n++)
        {
            const bool is_selected = (selected_view == n);
            ImGui::PushStyleColor(ImGuiCol_Text, has_view[n] ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(has_view[n] ? views[n] : views_disabled[n], is_selected))
                selected_view = n;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopStyleColor();
        }
    };

    if (!expand_to_listbox || ImGui::GetContentRegionAvail().y < EmSize(20.f))
    {
        ImGui::SetNextItemWidth(w);
        if (ImGui::BeginCombo("##Which view combo",
                              has_view[selected_view] ? views[selected_view] : views_disabled[selected_view]))
        {
            show_view_options();
            ImGui::EndCombo();
        }
    }
    else
    {
        // Custom size: use all width, 5 items tall
        if (ImGui::BeginListBox("##Which view listbox", ImVec2(w, 5.0f * ImGui::GetTextLineHeightWithSpacing() +
                                                                      ImGui::GetStyle().FramePadding.y)))
        {
            show_view_options();
            ImGui::EndListBox();
        }
    }
    ImGui::SameLine();
    ImGui::IconButton(expand_to_listbox ? ICON_MY_EXPAND_ALL : ICON_MY_COLLAPSE_ALL, &expand_to_listbox);
    ImGui::Tooltip(expand_to_listbox ? "Click to collapse info sections to a combobox."
                                     : "Click to expand info sections to a listbox.");

    if (selected_view == 0)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
        if (ImGui::PE::Begin("Image info", ImGuiTableFlags_ScrollY))
        {
            // calculate left column based on the longest property name
            ImGui::PushFont(hdrview()->font("sans bold"), ImGui::GetStyle().FontSizeBase);
            float col_width = ImGui::CalcTextSize("Channel selector").x + ImGui::GetStyle().CellPadding.x;
            ImGui::PopFont();

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, col_width);

            ImGui::Indent(ImGui::GetStyle().CellPadding.x);
            filtered_property("File name", filename);
            filtered_property(
                "File size",
                fmt::format(std::locale("en_US.UTF-8"), "{:.1h} ({:L} bytes)", human_readible{size_bytes}, size_bytes),
                "This is the size of the image file on disk. If the image consists of multiple parts, "
                "this is the size of the entire file.");
            filtered_property("Last modified", fmt::format("{:%b %d, %Y at %I:%M %p}", to_system_clock(last_modified)));
            filtered_property("Part name", partname.empty() ? "<none>" : partname.c_str());
            filtered_property("Channel selector", channel_selector.empty() ? "<none>" : channel_selector.c_str());
            filtered_property("Loader", metadata.value<string>("loader", "unknown"));
            filtered_property("Pixel format", metadata.value<string>("pixel format", "unknown"));
            filtered_property("Resolution", fmt::format("{} {} {}", size().x, ICON_MY_TIMES, size().y));
            filtered_property("Data window", fmt::format("[{}, {}) {} [{}, {})", data_window.min.x, data_window.max.x,
                                                         ICON_MY_TIMES, data_window.min.y, data_window.max.y));
            filtered_property("Display window",
                              fmt::format("[{}, {}) {} [{}, {})", display_window.min.x, display_window.max.x,
                                          ICON_MY_TIMES, display_window.min.y, display_window.max.y));
            filtered_property("Alpha", alpha_type_name(alpha_type),
                              "Type of alpha channel stored in the file. When treated as transparency, HDRView "
                              "converts it to premultiplied alpha upon load.");
            // Only offered when the file declares an alpha channel: a file that states its extra channel
            // is data (e.g. TIFF's EXTRASAMPLE_UNSPECIFIED) isn't overridden from here.
            if (alpha_type != AlphaType_None)
            {
                if (filter.PassFilter("Alpha is transparency"))
                    ImGui::PE::Entry(
                        "Is transparency",
                        [this]
                        {
                            bool value = alpha_is_transparency;
                            if (!ImGui::Checkbox("##Alpha is transparency", &value))
                                return false;
                            // The premultiply happens in-place on load, so switching interpretation means
                            // re-reading the file; reload_image() carries the new setting through.
                            alpha_is_transparency = value;
                            // draw_info() is only ever drawn for the current image (see the Info window)
                            hdrview()->reload_image(hdrview()->current_image());
                            return true;
                        },
                        "Whether the alpha channel means transparency. Turn this off for files whose alpha is "
                        "really a mask or other data: it is then shown as an ordinary channel of its own and "
                        "nothing is premultiplied by it.\n\nChanging this re-reads the file from disk.");
            }
            if (exif.valid())
                filtered_property("EXIF data", fmt::format("{:.0h}", human_readible{exif.size()}),
                                  "Size of the EXIF metadata block embedded in the image file.");
            if (!xmp_data.empty())
                filtered_property("XMP data", fmt::format("{:.0h}", human_readible{xmp_data.size()}),
                                  "Size of the XMP metadata block embedded in the image file.");
            if (!icc_data.empty())
                filtered_property("ICC data", fmt::format("{:.0h}", human_readible{icc_data.size()}),
                                  "Size of the ICC profile embedded in the image file.");
            ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
            ImGui::PE::End();
        }

        ImGui::PopStyleColor();
    }
    else if (selected_view == 1)
    {
        if (has_header)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
            if (ImGui::PE::Begin("Header", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize |
                                               ImGuiTableFlags_ScrollY))
            {
                ImGui::Indent(ImGui::GetStyle().CellPadding.x);
                add_fields(metadata["header"]);
                ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
                ImGui::PE::End();
            }
            ImGui::PopStyleColor();
        }
        else
            draw_no_metadata_message("No additional header data present in this image.");
    }
    else if (selected_view == 2)
    {
        if (has_exif)
        {
            draw_filter_input();

            if (ImGui::PE::Begin("EXIF info", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize |
                                                  ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Key");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
                ImGui::TableHeadersRow();

                ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0);
                for (auto &exif_entry : metadata["exif"].items())
                {
                    const auto &table_obj = exif_entry.value();
                    if (!table_obj.is_object())
                        continue;

                    ImGui::PushFont(bold_font, 0.f);
                    auto open = ImGui::PE::TreeNode(exif_entry.key().c_str(), ImGuiTreeNodeFlags_SpanFullWidth |
                                                                                  ImGuiTreeNodeFlags_SpanAllColumns |
                                                                                  ImGuiTreeNodeFlags_DefaultOpen |
                                                                                  ImGuiTreeNodeFlags_DrawLinesFull);
                    ImGui::PopFont();
                    if (open)
                    {
                        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                        add_fields(table_obj);
                        ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                        ImGui::PE::TreePop();
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PE::End();
            }
        }
        else
            draw_no_metadata_message("No XMP data present in this image.");
    }
    else if (selected_view == 3)
    {
        if (has_xmp)
        {
            draw_filter_input();

            json xmlns = metadata["xmp"]["xmlns"];

            if (ImGui::PE::Begin("XMP info", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize |
                                                 ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Key");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
                ImGui::TableHeadersRow();

                ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0);

                // get the namespaces in display order
                std::vector<std::string> namespaces_to_display;
                if (metadata["xmp"].contains("display_order") && metadata["xmp"]["display_order"].is_array())
                {
                    for (const auto &key_json : metadata["xmp"]["display_order"])
                    {
                        if (key_json.is_string())
                        {
                            std::string key = key_json.get<std::string>();
                            if (metadata["xmp"].contains(key) && metadata["xmp"][key].is_object())
                                namespaces_to_display.push_back(key);
                        }
                    }
                }
                else
                {
                    for (const auto &xmp_entry : metadata["xmp"].items())
                    {
                        if (xmp_entry.key() != "xmlns" && xmp_entry.value().is_object())
                            namespaces_to_display.push_back(xmp_entry.key());
                    }
                }

                for (const auto &ns : namespaces_to_display)
                {
                    const auto &table_obj = metadata["xmp"][ns];
                    string      name      = xmlns[ns]["name"];
                    string      uri       = xmlns[ns]["uri"];

                    ImGui::PushFont(bold_font, 0.f);
                    auto open = ImGui::PE::TreeNode(
                        name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_SpanAllColumns |
                                          ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull);
                    ImGui::PopFont();
                    if (open)
                    {
                        add_xmp_fields(table_obj, 0, name + " " + ns);
                        ImGui::PE::TreePop();
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PE::End();
            }
        }
        else
            draw_no_metadata_message("No XMP data present in this image.");
    }
    else if (selected_view == 4)
    {
        if (has_xmp)
        {
            ImGui::BeginChild("##xmp_scroll", ImVec2(-1, -1), ImGuiChildFlags_FrameStyle,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushFont(hdrview()->font("mono regular"), ImGui::GetStyle().FontSizeBase);
            ImGui::TextUnformatted(reinterpret_cast<const char *>(xmp_data.data()),
                                   reinterpret_cast<const char *>(xmp_data.data()) + xmp_data.size());
            ImGui::PopFont();
            ImGui::Tooltip("Click to copy to clipboard.");
            if (ImGui::IsItemClicked())
                ImGui::SetClipboardText(
                    string(reinterpret_cast<const char *>(xmp_data.data()), xmp_data.size()).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::EndChild();
        }
        else
            draw_no_metadata_message("No XMP data present in this image.");
    }
}

void Image::draw_chromaticity_diagram(float width)
{
    static float2 vMin{-0.05f, -0.05f};
    static float2 vMax{0.75f, 0.9f};
    static float2 vSize  = vMax - vMin;
    static float  aspect = vSize.x / vSize.y;

    float const size = ImMax(width, EmSize(8.f));

    ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase);

    float4 plot_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); //{0.35f, 0.35f, 0.35f, 1.f};
    ImGui::PushStyleColor(ImGuiCol_WindowBg, plot_bg);
    if (ImPlot::BeginPlot("##Chromaticity diagram", ImVec2(size, size / aspect * 0.95f),
                          ImPlotFlags_Crosshairs | ImPlotFlags_Equal | ImPlotFlags_NoLegend | ImPlotFlags_NoTitle))
    {
        static constexpr float lambda_min   = 400.f;
        static constexpr float lambda_max   = 680.f;
        static constexpr int   sample_count = 200;

        auto text_color_f  = float4{0.f, 0.f, 0.f, 1.f};
        auto text_color_fc = contrasting_color(text_color_f);

        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImGui::GetColorU32(text_color_fc));

        ImPlot::GetInputMap().ZoomRate = 0.03f;
        ImPlot::SetupAxis(ImAxis_X1, "x", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_Foreground);
        ImPlot::SetupAxis(ImAxis_Y1, "y", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_Foreground);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
        ImPlot::SetupAxesLimits(vMin.x, vMax.x, vMin.y, vMax.y);
        ImPlot::SetupMouseText(ImPlotLocation_NorthEast, ImPlotMouseTextFlags_NoFormat);

        ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
        ImPlot::SetupFinish();
        ImGui::PopFont();

        ImPlot::PushPlotClipRect();

        //
        // plot background texture
        //
        ImPlot::PlotImage("##chromaticity_image", (ImTextureID)chromaticity_texture()->texture_handle(),
                          ImPlotPoint(0.0, 0.0), ImPlotPoint(.73f, .83f), {0.f, .83f}, {.73f, 0.f});

        auto normal_to_plot_tangent = [](const float2 &tangent, float pixel_length) -> float2
        {
            float2 p0            = ImPlot::PlotToPixels(0, 0);
            float2 tangent_px    = float2(ImPlot::PlotToPixels(tangent.x, tangent.y)) - p0;
            float2 normal_px     = pixel_length * normalize(float2{-tangent_px.y, tangent_px.x});
            auto   plot_tick_end = ImPlot::PixelsToPlot(p0 + normal_px);
            return float2((float)plot_tick_end.x, (float)plot_tick_end.y);
        };

        //
        // draw the spectral locus
        //
        float pixels_per_texel     = 1.f;
        float pixels_per_plot_unit = 1.f;
        float scale_factor         = 1.f;
        {
            float2 plot_size     = ImPlot::GetPlotSize();
            auto   plot_rect     = ImPlot::GetPlotLimits();
            pixels_per_plot_unit = length(plot_size / float2((float)plot_rect.X.Max - (float)plot_rect.X.Min,
                                                             (float)plot_rect.Y.Max - (float)plot_rect.Y.Min));
            // compute width in pixels of a chromaticity texture texel
            pixels_per_texel = 1.f / 256.f * pixels_per_plot_unit;
            scale_factor     = std::clamp(pixels_per_texel * 1.2f, 1.f, 4.f);

            // ImPlot::PlotLine draws ugly, unrounded, line segments, so we use AddPolyline ourselves
            ImVector<float2> poly;
            poly.resize(sample_count);
            // Iterate over all entries in poly and map them to pixel coordinates
            for (int i = 0; i < poly.Size; ++i)
            {
                float wavelength = lerp(lambda_min, lambda_max, ((float)i) / ((float)(sample_count - 1)));
                auto  pos        = wavelength_to_xy(wavelength);
                poly[i]          = ImPlot::PlotToPixels(pos.x, pos.y);
            }

            ImPlot::GetPlotDrawList()->AddPolyline((ImVec2 *)&poly[0], poly.Size, ImGui::GetColorU32(text_color_f),
                                                   std::max(1.f, 1.2f * pixels_per_texel), ImDrawFlags_Closed);
        }

        //
        // draw wavelength tick marks
        //
        {
            ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
            // Draw tick marks stored in tick_marks
            const float minor_tick_pixel_length = std::max(1.f, 2.f * pixels_per_texel);
            const float major_tick_pixel_length = std::max(1.f, 3.f * pixels_per_texel);

            static const float first_tick = (ImFloor(lambda_min / 10.f)) * 10.f;
            int                tick_count = (int)ImFloor((lambda_max - lambda_min) / 10.0f) + 1;
            for (int i = 0; i < tick_count; ++i)
            {
                // Compute wavelength for this tick
                float nm = first_tick + i * 10.0f;
                // Check if tick is at a 100 nm multiple
                bool is_major = (static_cast<int>(nm) % 100 == 0);

                float lambda = first_tick + i * 10.0f;

                // Compute chromaticity at this wavelength
                float2 pos     = wavelength_to_xy(lambda);
                float2 tangent = wavelength_to_xy(lambda + 1) - wavelength_to_xy(lambda - 1);
                float2 normal =
                    -normal_to_plot_tangent(tangent, is_major ? major_tick_pixel_length : minor_tick_pixel_length);

                // Tick mark parameters
                float2 tick[2] = {pos, pos + normal};

                ImPlotSpec tick_spec;
                tick_spec.LineColor  = ImVec4{0.f, 0.f, 0.f, 1.f};
                tick_spec.LineWeight = 0.5f * scale_factor;
                tick_spec.Stride     = sizeof(float2);
                ImPlot::PlotLine("##CCT_tick", &tick[0].x, &tick[0].y, 2, tick_spec);

                // Add text label for major ticks (100 nm multiples)
                if (is_major)
                {
                    static char label[8];
                    ImFormatString(label, sizeof(label), "%d nm", static_cast<int>(nm));

                    float4 bg = contrasting_color(contrasting_color(plot_bg));
                    bg.w      = 0.5f;

                    ImPlot::Annotation(tick[1].x, tick[1].y, bg, float2{1.f, -1.f} * round(normalize(normal)), false,
                                       "%s", label);
                }
            }
            ImGui::PopFont();
        }

        //
        // draw the locus of D (daylight) CCTs
        //
        {
            auto create_CCT_locus = []()
            {
                ImVector<float2> poly;
                poly.resize(sample_count);

                for (int i = 0; i < sample_count; ++i)
                {
                    float T = lerp(1668.f, 25000.f, ((float)i) / ((float)(sample_count - 1)));
                    poly[i] = Kelvin_to_xy(T);
                }

                return poly;
            };
            const static ImVector<float2> cct_locus = create_CCT_locus();

            // ImPlot::PlotLine draws ugly, unrounded, line segments, so we use AddPolyline ourselves
            ImVector<float2> poly = cct_locus;
            // Iterate over all entries in poly and map them to pixel coordinates
            for (int i = 0; i < poly.Size; ++i) poly[i] = ImPlot::PlotToPixels({poly[i].x, poly[i].y});

            ImPlot::GetPlotDrawList()->AddPolyline((ImVec2 *)&poly[0], poly.Size, ImGui::GetColorU32(text_color_f),
                                                   scale_factor, ImDrawFlags_None);

            const float label_font_scale = 1.0f;
            const float scale            = 1.f;
            ImGui::PushFont(hdrview()->font("sans regular"),
                            ImGui::GetStyle().FontSizeBase * label_font_scale * scale / 2.f);

            constexpr int temp_step = 1000;
            // Tracks the last tick actually drawn, so labels that would overlap it are skipped. Starts far
            // off-plot so the first tick always draws.
            float2 prev_tick_end = {-100000.f, -100000.f};
            for (int temp = 2000; temp <= 25000; temp += temp_step)
            {
                float2 xy = Kelvin_to_xy((float)temp);
                char   label[8];
                snprintf(label, sizeof(label), "%dK", temp);
                float2 text_size = ImGui::CalcTextSize(label);

                // Compute tangent and normal
                float2 tangent = normalize(Kelvin_to_xy((float)(temp - 1)) - Kelvin_to_xy((float)(temp + 1)));
                float2 normal  = normal_to_plot_tangent(tangent, scale_factor * 2.f);

                // Tick mark parameters
                float2 tick[2] = {xy, xy + normal};

                // Only draw this tick if it doesn't overlap with the previous tick
                const float min_dist = 5.0f; // minimum pixel distance between ticks

                float2 tick_end_px      = ImPlot::PlotToPixels(ImPlotPoint(tick[1].x, tick[1].y));
                float2 prev_tick_end_px = ImPlot::PlotToPixels(ImPlotPoint(prev_tick_end.x, prev_tick_end.y));
                bool   draw             = length(tick_end_px - prev_tick_end_px) > min_dist &&
                            (2.f * text_size.y < abs(tick_end_px.y - prev_tick_end_px.y) ||
                             1.5f * text_size.x < abs(tick_end_px.x - prev_tick_end_px.x));

                if (draw)
                {
                    ImPlotSpec tick_spec;
                    tick_spec.LineColor  = text_color_f;
                    tick_spec.LineWeight = 0.5f * scale_factor;
                    tick_spec.Stride     = sizeof(float2);
                    ImPlot::PlotLine("##CCT_tick", &tick[0].x, &tick[0].y, 2, tick_spec);
                    prev_tick_end = tick[1];

                    ImPlot::Annotation(tick[1].x, tick[1].y, ImVec4(1, 1, 1, 0.5), ImVec2(1, 1), false, "%s", label);
                }
            }

            ImGui::PopFont();
        }

        //
        // draw the primaries, gamut triangle, whitepoint, and text labels
        //
        {
            Chromaticities gamut_chr{chromaticities.value_or(Chromaticities{})};
            ImVec4         colors[] = {
                {0.8f, 0.f, 0.f, 1.f}, {0.f, 0.8f, 0.f, 1.f}, {0.f, 0.f, 0.8f, 1.f}, {0.5f, 0.5f, 0.5f, 1.f}};
            const char *names[]     = {"R", "G", "B", "W"};
            double2     primaries[] = {double2(gamut_chr.red), double2(gamut_chr.green), double2(gamut_chr.blue),
                                       double2(gamut_chr.red)};
            static bool clicked[4]  = {false, false, false, false};
            static bool hovered[4]  = {false, false, false, false};
            static bool held[4]     = {false, false, false, false};

            primaries[3] = double2(gamut_chr.red);

            ImPlotSpec triangle_spec;
            triangle_spec.LineColor  = text_color_fc;
            triangle_spec.LineWeight = scale_factor;
            triangle_spec.Stride     = sizeof(double2);
            ImPlot::PlotLine("##gamut_triangle", &primaries[0].x, &primaries[0].y, 4, triangle_spec);

            primaries[3] = double2(gamut_chr.white);

            // ImPlot::PlotScatter draws ugly, unrounded, circles, so we use AddPolyline ourselves
            // Iterate over all entries in poly and map them to pixel coordinates
            std::array<ImVec2, 4> poly = {ImPlot::PlotToPixels(ImPlotPoint(primaries[0].x, primaries[0].y)),
                                          ImPlot::PlotToPixels(ImPlotPoint(primaries[1].x, primaries[1].y)),
                                          ImPlot::PlotToPixels(ImPlotPoint(primaries[2].x, primaries[2].y)),
                                          ImPlot::PlotToPixels(ImPlotPoint(primaries[3].x, primaries[3].y))};
            for (int i = 0; i < 4; ++i)
                ImPlot::GetPlotDrawList()->AddCircleFilled(
                    poly[i], 2.5f * scale_factor,
                    ImGui::GetColorU32(clicked[i] || hovered[i] || held[i] ? text_color_f : text_color_fc), 0);

            ImGui::PushFont(hdrview()->font("sans bold"), ImGui::GetStyle().FontSizeBase);
            for (int i = 0; i < 4; ++i)
            {
                if (ImPlot::DragPoint(i, &primaries[i].x, &primaries[i].y, colors[i], 1.5f * scale_factor,
                                      ImPlotDragToolFlags_Delayed, &clicked[i], &hovered[i], &held[i]))
                {
                    gamut_chr.red   = float2((float)primaries[0].x, (float)primaries[0].y);
                    gamut_chr.green = float2((float)primaries[1].x, (float)primaries[1].y);
                    gamut_chr.blue  = float2((float)primaries[2].x, (float)primaries[2].y);
                    gamut_chr.white = float2((float)primaries[3].x, (float)primaries[3].y);
                    chromaticities  = gamut_chr;
                    compute_color_transform();
                }

                // draw text label shadow
                ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_f));
                float2 offset{4.f * scale_factor, -4.f * scale_factor};
                ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                ImPlot::PopStyleColor();

                // draw text label
                ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_fc));
                offset -= 1.f;
                ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                ImPlot::PopStyleColor();

                // ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_f));
                // ImVec2 offset{4.f * scale, -4.f * scale};
                // ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                // ImPlot::PopStyleColor();
            }

            ImGui::PopFont();
        }

        //
        // draw the hovered pixel in the chromaticity diagram
        //
        {
            auto &io      = ImGui::GetIO();
            auto  rgb2xyz = mul(M_RGB_to_XYZ, inverse(M_to_sRGB));
            if (hdrview()->mouse_over_viewport())
            {
                auto   hovered_pixel = int2{hdrview()->pixel_at_app_pos(io.MousePos)};
                float4 color32       = hdrview()->pixel_value(hovered_pixel, false, 0);

                float3 XYZ = mul(rgb2xyz, color32.xyz());
                float2 xy  = XYZ.xy() / (XYZ.x + XYZ.y + XYZ.z);

                ImPlotSpec spec;
                spec.LineColor  = ImVec4{0.f, 0.f, 0.f, 1.0f};
                spec.MarkerSize = 2.f;
                ImPlot::PlotScatter("##HoveredPixel", &xy.x, &xy.y, 1, spec);
            }
        }
        ImPlot::PopPlotClipRect();

        ImPlot::PopStyleColor();

        ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
        ImPlot::EndPlot();
        ImGui::PopFont();
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Image::draw_colorspace()
{
    auto bold_font = hdrview()->font("sans bold");

    static const ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize;
    // The enclosing window scrolls this panel (see the ImGuiWindowFlags_AlwaysVerticalScrollbar the Colorspace
    // DockableWindow is created with), so the table itself neither scrolls nor sits in a scrolling child: the
    // window's padding is what separates it from the scrollbar.
    if (ImGui::PE::Begin("Colorspace", table_flags))
    {
        // The diagram gets its own full-width row when the value column alone is too narrow to render it legibly.
        const bool diagram_fits_in_value_column = ImGui::PE::ColumnWidth(1) > HelloImGui::EmSize(12.f);
        ImGui::Indent(ImGui::GetStyle().CellPadding.x);
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);

        ImGui::PE::WrappedText(
            "Profile name",
            metadata.value<string>("color profile", color_profile_name(ColorGamut_sRGB_BT709, TransferFunction::Linear))
                .c_str(),
            "The color profile (primaries and transfer function) applied at load time to make the values linear. This "
            "might come from various sources (ICC profiles, CICP tags, structured metadata provided by the image "
            "loading library). If no color profile is found, HDRView assumes BT.709/sRGB primaries with a D65 "
            "whitepoint, and an sRGB transfer function for SDR images.",
            bold_font, FLT_MAX);

        ImGui::PE::Entry("Color gamut",
                         [&]
                         {
                             bool modified = false;
                             auto csn      = color_gamut_names();
                             auto open_combo =
                                 ImGui::BeginCombo("##Color gamut", color_gamut_name((ColorGamut_)color_space),
                                                   ImGuiComboFlags_HeightLargest);
                             if (open_combo)
                             {
                                 for (ColorGamut n = ColorGamut_FirstNamed; n <= ColorGamut_LastNamed; ++n)
                                 {
                                     auto       cg          = (ColorGamut_)n;
                                     const bool is_selected = (color_space == n);
                                     if (ImGui::Selectable(csn[n], is_selected))
                                     {
                                         color_space = cg;
                                         spdlog::debug("Switching to color space {}.", n);
                                         chromaticities = ::gamut_chromaticities(cg);
                                         compute_color_transform();
                                         modified = true;
                                     }

                                     // Set the initial focus when opening the combo (scrolling + keyboard
                                     // navigation focus)
                                     if (is_selected)
                                         ImGui::SetItemDefaultFocus();
                                 }
                                 ImGui::EndCombo();
                             }
                             return modified;
                         });
        ImGui::Tooltip("Interpret the values stored in the file using the chromaticities of a common color profile.");

        ImGui::PE::Entry("White point",
                         [&]
                         {
                             bool modified = false;
                             auto wpn      = white_point_names();
                             ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x < EmSize(8.f)
                                                         ? EmSize(8.f)
                                                         : -FLT_MIN); // use the full width of the column
                             auto open_combo = ImGui::BeginCombo("##White point", white_point_name(white_point),
                                                                 ImGuiComboFlags_HeightLargest);
                             if (open_combo)
                             {
                                 for (WhitePoint n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                                 {
                                     auto       wp          = (WhitePoint_)n;
                                     const bool is_selected = (white_point == n);
                                     if (ImGui::Selectable(wpn[n], is_selected))
                                     {
                                         white_point = wp;
                                         spdlog::debug("Switching to white point {}.", n);
                                         if (!chromaticities)
                                             chromaticities = Chromaticities{};
                                         chromaticities->white = ::white_point(wp);
                                         compute_color_transform();
                                     }

                                     // Set the initial focus when opening the combo (scrolling + keyboard
                                     // navigation focus)
                                     if (is_selected)
                                         ImGui::SetItemDefaultFocus();
                                 }
                                 ImGui::EndCombo();
                             }
                             return modified;
                         });

        const Chromaticities chr_orig{chromaticities.value_or(Chromaticities{})};
        Chromaticities       chr{chr_orig};
        bool                 edited = false;

        edited |= ImGui::PE::SliderFloat2("Red", &chr.red.x, 0.f, 1.f, "%.4f");
        edited |= ImGui::PE::SliderFloat2("Green", &chr.green.x, 0.f, 1.f, "%.4f");
        edited |= ImGui::PE::SliderFloat2("Blue", &chr.blue.x, 0.f, 1.f, "%.4f");

        if (chr_orig != chr || edited)
        {
            spdlog::debug("Setting chromaticities to ({}, {}), ({}, {}), ({}, {}), ({}, {})", chr.red.x, chr.red.y,
                          chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x, chr.white.y);
            chromaticities = chr;
            compute_color_transform();
        }

        chr       = chromaticities.value_or(Chromaticities{});
        float2 wp = chr.white;

        if (ImGui::PE::SliderFloat2("White point", &wp.x, 0.f, 1.f, "%.4f") || wp != chr.white)
        {
            chr.white = wp;
            spdlog::info("Setting chromaticities to ({}, {}), ({}, {}), ({}, {}), ({}, {})", chr.red.x, chr.red.y,
                         chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x, chr.white.y);
            chromaticities = chr;
            compute_color_transform();
        }

        ImGui::PE::Entry(
            "Adopted neutral",
            [&]
            {
                bool has_an = adopted_neutral.has_value();
                if (ImGui::Checkbox("##hidden", &has_an))
                {
                    if (has_an)
                        adopted_neutral = wp;
                    else
                        adopted_neutral.reset();
                    compute_color_transform();
                }

                ImGui::SetNextItemWidth(-FLT_MIN);

                if (has_an && ImGui::SliderFloat2("##hidden", &adopted_neutral->x, 0.f, 1.f, "%.4f"))
                    compute_color_transform();

                return false;
            },
            "Specifies the CIE (x,y) coordinates that should be considered neutral during "
            "color rendering. Pixels in the image file whose (x,y) coordinates match the "
            "adoptedNeutral value should be mapped to neutral values on the display.");

        ImGui::PE::Entry(
            "Adaptation",
            [&]
            {
                const char *wan[] = {"None", "XYZ scaling", "Bradford", "Von Kries", nullptr};

                bool modified   = false;
                auto open_combo = ImGui::BeginCombo("##Adaptation",
                                                    adaptation_method <= AdaptationMethod_Identity ||
                                                            adaptation_method >= AdaptationMethod_Count
                                                        ? "None"
                                                        : wan[adaptation_method],
                                                    ImGuiComboFlags_HeightLargest);
                if (open_combo)
                {
                    for (AdaptationMethod_ n = 0; wan[n]; ++n)
                    {
                        auto       am          = (AdaptationMethod)n;
                        const bool is_selected = (adaptation_method == am);
                        if (ImGui::Selectable(wan[n], is_selected))
                        {
                            adaptation_method = am;
                            spdlog::debug("Switching to adaptation method {}.", n);
                            compute_color_transform();
                            modified = true;
                        }

                        // Set the initial focus when opening the combo (scrolling + keyboard navigation
                        // focus)
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return modified;
            },
            "Method for chromatic adaptation transform.");

        ImGui::PE::InputFloat3("Yw", &luminance_weights.x, "%+8.2e",
                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly,
                               "Channel weights to compute the luminance (Y) of a pixel.");

        ImGui::PE::Entry(
            "Color matrix",
            [&]
            {
                bool modified = false;
                modified |= ImGui::InputFloat3("##M0", &M_to_sRGB[0][0], "%+8.2e",
                                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                // ImGui::NewLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                modified |= ImGui::InputFloat3("##M1", &M_to_sRGB[1][0], "%+8.2e",
                                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                // ImGui::NewLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                modified |= ImGui::InputFloat3("##M2", &M_to_sRGB[2][0], "%+8.2e",
                                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                return modified;
            });

        if (diagram_fits_in_value_column)
            ImGui::PE::Entry("Diagram",
                             [this]()
                             {
                                 draw_chromaticity_diagram(ImGui::GetContentRegionAvail().x);
                                 return false;
                             });
        else
            ImGui::PE::FullWidthEntry("Diagram",
                                      [this](float width)
                                      {
                                          draw_chromaticity_diagram(width);
                                          return false;
                                      });

        ImGui::PopStyleVar();
        ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
        ImGui::PE::End();
    }
}

void Image::draw_channel_stats()
{
    auto &group      = groups[selected_group];
    int   components = group.num_channels;
    bool  is_color   = group.type == ChannelGroup::RGBA_Channels || group.type == ChannelGroup::RGB_Channels;

    PixelStats *channel_stats[4] = {nullptr, nullptr, nullptr, nullptr};
    string      channel_names[4];
    for (int c = 0; c < components; ++c)
    {
        auto &channel = channels[group.channels[c]];
        channel.update_stats(c, hdrview()->current_image(), hdrview()->reference_image());
        channel_stats[c] = channel.get_stats();
        channel_names[c] = Channel::tail(channel.name);
    }

    float exposure_gain = pow(2.f, hdrview()->exposure_live());

    // Persisted per-row display mode. A module-static (rather than a per-Image field) is a simplification
    // carried over from the pre-existing `value_mode` this replaces -- shared across all images, which is
    // fine since only one image's stats are ever shown at a time.
    static int mode_min = ImGui::ChannelDisplayMode_Raw, mode_avg = ImGui::ChannelDisplayMode_Raw,
               mode_max = ImGui::ChannelDisplayMode_Raw, mode_stddev = ImGui::ChannelDisplayMode_Raw,
               mode_nan = ImGui::ChannelDisplayMode_Raw, mode_inf = ImGui::ChannelDisplayMode_Raw;

    auto stat_row = [&](auto &&accessor, ImGuiDataType data_type, const char *format, bool show_swatch,
                        ImGui::ChannelDisplayModeMask enabled_modes, int *mode, const string &label)
    {
        float raw[4] = {0.f, 0.f, 0.f, 1.f};
        for (int c = 0; c < components; ++c) raw[c] = (float)accessor(c);

        float4 displayed{0.f, 0.f, 0.f, 1.f};
        if (show_swatch)
        {
            // The swatch goes through the tonemap and, in false-color mode, a colormap lookup that indexes
            // with an integer -- a NaN sample survives neither. The numbers printed beside the swatch come
            // from `raw`, so they still report the value the file holds.
            float4 finite{raw[0], raw[1], raw[2], raw[3]};
            for (int c = 0; c < 4; ++c)
                if (!std::isfinite(finite[c]))
                    finite[c] = 0.f;
            displayed = linear_to_sRGB(hdrview()->tonemap_value(finite));
        }

        ImGui::PE::Entry(label,
                         [&]
                         {
                             ImGui::ChannelValuesRow(label.c_str(), raw, show_swatch ? &displayed.x : nullptr,
                                                     components, data_type, format, exposure_gain, mode, enabled_modes,
                                                     /*allow_copy=*/true, show_swatch,
                                                     ImVec4{displayed.x, displayed.y, displayed.z, displayed.w},
                                                     /*label=*/{}, ImGui::PE::ColumnWidth(1));
                             return false;
                         });
    };

    // Channel names as a row of their own, positioned via the PE table's actual value-column width -- a PE
    // table has no shared header row to put them in otherwise. No left-column label: "Statistics" already
    // lives in the SeparatorText above the table.
    ImGui::PE::Entry("",
                     [&]
                     {
                         ImGui::ChannelValuesRowHeader(channel_names, components, ImGui::PE::ColumnWidth(1),
                                                       /*reserve_swatch_gap=*/true);
                         return false;
                     });

    stat_row([&](int c) { return channel_stats[c]->summary.minimum; }, ImGuiDataType_Float, "%g", is_color,
             is_color ? ImGui::ChannelDisplayMode_AllMask : ImGui::ChannelDisplayMode_NoDisplayMask, &mode_min,
             "Minimum");
    stat_row([&](int c) { return channel_stats[c]->summary.average; }, ImGuiDataType_Float, "%g", is_color,
             is_color ? ImGui::ChannelDisplayMode_AllMask : ImGui::ChannelDisplayMode_NoDisplayMask, &mode_avg,
             "Average");
    stat_row([&](int c) { return channel_stats[c]->summary.maximum; }, ImGuiDataType_Float, "%g", is_color,
             is_color ? ImGui::ChannelDisplayMode_AllMask : ImGui::ChannelDisplayMode_NoDisplayMask, &mode_max,
             "Maximum");
    stat_row([&](int c) { return channel_stats[c]->summary.stddev; }, ImGuiDataType_Float, "%g", false,
             ImGui::ChannelDisplayMode_NoDisplayMask, &mode_stddev, "Std. Dev.");
    stat_row([&](int c) { return channel_stats[c]->summary.nan_pixels; }, ImGuiDataType_S32, "%d", false,
             ImGui::ChannelDisplayMode_RawOnlyMask, &mode_nan, "# NaNs");
    stat_row([&](int c) { return channel_stats[c]->summary.inf_pixels; }, ImGuiDataType_S32, "%d", false,
             ImGui::ChannelDisplayMode_RawOnlyMask, &mode_inf, "# Infs");
}
