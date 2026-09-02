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

/**
    The square plot of a tone curve that the tonal dialogs draw above their sliders: what a level goes in
    as against what it comes out as, over [0,1]. Keeps state between frames, so it is a member of the
    command. Used between begin() and end(), like ImPlot itself:

        if (m_plot.begin("##Curve"))
        {
            m_plot.curve("gamma", ys, ImVec4(1, 1, 1, 0.85f));
            m_plot.end();
        }
*/
class ToneCurvePlot
{
public:
    /// Samples along the horizontal axis.
    static constexpr int N = 129;

    /// The input level at sample \p i, where every curve drawn here is evaluated.
    static float x(int i) { return float(i) / float(N - 1); }

    /// Open the plot. False when ImPlot declined it, in which case nothing else may be called.
    bool begin(const char *id)
    {
        // as wide as the sliders beneath it. The tick labels take more width at the left than height at
        // the bottom, so the height carries a correction measured from the last frame; see end().
        m_width = ImGui::CalcItemWidth();

        if (!ImPlot::BeginPlot(id, ImVec2(m_width, m_width + m_extra_height),
                               ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                                   ImPlotFlags_NoBoxSelect))
            return false;

        const ImPlotAxisFlags axes = ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_Lock;
        ImPlot::SetupAxes(nullptr, nullptr, axes, axes);

        // the same ticks both ways, since both axes carry a level
        ImPlot::SetupAxisTicks(ImAxis_X1, 0.0, 1.0, 5);
        ImPlot::SetupAxisTicks(ImAxis_Y1, 0.0, 1.0, 5);

        // fixed, so a steep curve is seen leaving the top and bottom instead of the frame following it
        ImPlot::SetupAxesLimits(0.0, 1.0, 0.0, 1.0, ImPlotCond_Always);
        return true;
    }

    /// One curve, \p ys being N outputs for the inputs x(0)..x(N-1).
    void curve(const char *name, const float *ys, ImVec4 color, float weight = 2.f)
    {
        float xs[N];
        for (int i = 0; i < N; ++i) xs[i] = x(i);

        ImPlotSpec spec;
        spec.LineWeight = weight;
        spec.LineColor  = color;
        ImPlot::PlotLine(name, xs, ys, N, spec);
    }

    /// A vertical line, for marking the input level a curve pivots about.
    void marker_x(const char *name, float value, ImVec4 color)
    {
        const float xs[2] = {value, value};
        const float ys[2] = {0.f, 1.f};

        ImPlotSpec spec;
        spec.LineWeight = 1.f;
        spec.LineColor  = color;
        ImPlot::PlotLine(name, xs, ys, 2, spec);
    }

    /// A small marker at \p at, for a point of the curve that can be taken hold of.
    void handle(float2 at, ImVec4 color)
    {
        const float xs[1] = {at.x};
        const float ys[1] = {at.y};

        ImPlotSpec spec;
        spec.Marker          = ImPlotMarker_Circle;
        spec.MarkerSize      = 4.f;
        spec.MarkerFillColor = color;
        spec.MarkerLineColor = color;
        ImPlot::PlotScatter("handle", xs, ys, 1, spec);
    }

    /// While the left button is dragging inside the plot, where it is, in plot coordinates.
    /**
        Not the widget's coordinates, which include the frame and tick labels. A drag that began inside is
        followed after it leaves, and \p pressed_at receives where it began, which says what is being
        dragged.
    */
    bool drag(float2 &position, float2 *pressed_at = nullptr)
    {
        auto here = []
        {
            const ImPlotPoint p = ImPlot::GetPlotMousePos();
            return float2{std::clamp(float(p.x), 0.f, 1.f), std::clamp(float(p.y), 0.f, 1.f)};
        };

        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_dragging = true;
            m_press    = here();
        }

        if (!m_dragging)
            return false;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_dragging = false;
            return false;
        }

        position = here();
        if (pressed_at)
            *pressed_at = m_press;
        return true;
    }

    void end()
    {
        // measured before the plot closes, and applied to the next frame's height; see begin()
        const ImVec2 area = ImPlot::GetPlotSize();
        if (area.x > 0.f && area.y > 0.f)
            m_extra_height = std::clamp(m_extra_height + (area.x - area.y), 0.f, m_width);

        ImPlot::EndPlot();
    }

private:
    float  m_width        = 0.f;
    float  m_extra_height = 0.f; ///< What the widget needs above its width for the plot area to be square
    bool   m_dragging     = false;
    float2 m_press{0.f, 0.f}; ///< Where the drag began
};
