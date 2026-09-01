//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include <hello_imgui/hello_imgui.h>
#include <implot.h>

#include <algorithm>

/*!
    The square plot of a tone curve that the tonal dialogs draw above their sliders.

    Two commands want the same picture -- what a level goes in as against what it comes out as, over
    [0,1] -- and the fiddly parts are the same for both: matching the sliders' width, keeping the plot
    area square rather than the widget, and reading a drag in the plot's coordinates instead of the
    widget's. It keeps a little state between frames for the first and last of those, so it is a member
    of the command rather than a free function.

    Used between begin() and end(), like ImPlot itself:

        if (m_plot.begin("##Curve"))
        {
            m_plot.curve("gamma", ys, ImVec4(1, 1, 1, 0.85f));
            m_plot.end();
        }
*/
class ToneCurvePlot
{
public:
    //! Samples along the horizontal axis. Enough that the steepest curve here still reads as a curve.
    static constexpr int N = 129;

    //! The input level at sample \p i, which is what every curve drawn here is evaluated at.
    static float x(int i) { return float(i) / float(N - 1); }

    //! Open the plot. False when ImPlot declined it, in which case nothing else may be called.
    bool begin(const char *id)
    {
        // As wide as the sliders beneath it. The widget also holds the tick labels, which are wider down
        // the left side than they are tall along the bottom, so a square widget leaves a plot area that is
        // not square -- the height carries a correction measured from the last frame, which settles after
        // one and then stays put.
        m_width = ImGui::CalcItemWidth();

        if (!ImPlot::BeginPlot(id, ImVec2(m_width, m_width + m_extra_height),
                               ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                                   ImPlotFlags_NoBoxSelect))
            return false;

        const ImPlotAxisFlags axes = ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_Lock;
        ImPlot::SetupAxes(nullptr, nullptr, axes, axes);

        // The same ticks both ways, since the two axes carry the same quantity: a level in and the level
        // it maps to.
        ImPlot::SetupAxisTicks(ImAxis_X1, 0.0, 1.0, 5);
        ImPlot::SetupAxisTicks(ImAxis_Y1, 0.0, 1.0, 5);

        // Fixed, so the curve moves against the frame instead of the frame following the curve. A steep
        // curve leaves the top and bottom, and seeing it leave is the point of drawing it.
        ImPlot::SetupAxesLimits(0.0, 1.0, 0.0, 1.0, ImPlotCond_Always);
        return true;
    }

    //! One curve, \p ys being N outputs for the inputs x(0)..x(N-1).
    void curve(const char *name, const float *ys, ImVec4 color, float weight = 2.f)
    {
        float xs[N];
        for (int i = 0; i < N; ++i) xs[i] = x(i);

        ImPlotSpec spec;
        spec.LineWeight = weight;
        spec.LineColor  = color;
        ImPlot::PlotLine(name, xs, ys, N, spec);
    }

    //! A vertical line, for marking the input level a curve pivots about.
    void marker_x(const char *name, float value, ImVec4 color)
    {
        const float xs[2] = {value, value};
        const float ys[2] = {0.f, 1.f};

        ImPlotSpec spec;
        spec.LineWeight = 1.f;
        spec.LineColor  = color;
        ImPlot::PlotLine(name, xs, ys, 2, spec);
    }

    //! While the left button is dragging inside the plot, where it is, in plot coordinates.
    /*!
        In the plot's coordinates rather than the widget's: the widget rectangle includes the frame and the
        tick labels around the plot, so a fraction taken across it is not the fraction across the axes --
        whatever the drag sets lands beside the cursor and drifts further the nearer the edge it gets.

        A drag that began inside goes on being followed after it leaves, so a slide to an extreme does not
        stop short of one.
    */
    bool drag(float2 &position)
    {
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_dragging = true;

        if (!m_dragging)
            return false;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_dragging = false;
            return false;
        }

        const ImPlotPoint p = ImPlot::GetPlotMousePos();
        position            = float2{std::clamp(float(p.x), 0.f, 1.f), std::clamp(float(p.y), 0.f, 1.f)};
        return true;
    }

    void end()
    {
        // Measured before the plot closes, and applied to the next frame's height; see begin().
        const ImVec2 area = ImPlot::GetPlotSize();
        if (area.x > 0.f && area.y > 0.f)
            m_extra_height = std::clamp(m_extra_height + (area.x - area.y), 0.f, m_width);

        ImPlot::EndPlot();
    }

private:
    float m_width        = 0.f;
    float m_extra_height = 0.f; //!< What the widget needs above its width for the plot area to be square
    bool  m_dragging     = false;
};
