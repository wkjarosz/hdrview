//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "multigraph.h"
#include <nanogui/theme.h>
#include <nanogui/opengl.h>
#include <spdlog/fmt/fmt.h>
#include "common.h"
#include "colorspace.h"
#include <iostream>

using std::string;
using std::vector;

namespace
{
const int hpad = 10;
const int textPad = 4;
}

/*!
 * @param parent	The parent widget
 * @param fg 		The foreground color of the first plot
 * @param v 		The value vector for the first plot
 */
MultiGraph::MultiGraph(Widget *parent, const Color & fg, const std::vector<float> & v)
	: Widget(parent), m_background_color(20, 128), m_text_color(240, 192)
{
	m_foreground_colors.push_back(fg);
	m_values.push_back(v);
}

Vector2i MultiGraph::preferred_size(NVGcontext *) const
{
	return Vector2i(256, 75);
}

Vector2f MultiGraph::graph_coordinate_at(const Vector2f& position) const
{
	Vector2f topLeft(x_position(0), y_position(0));
	Vector2f bottomRight(x_position(1), y_position(1));
	Vector2f graphSize = bottomRight-topLeft;
	return (position-topLeft) / (graphSize);
}

void MultiGraph::set_xticks(const std::vector<float> & ticks,
                           const vector<string> & labels)
{
	if (ticks.size() == labels.size())
	{
		m_xticks = ticks;
		m_xtick_labels = labels;
	}
}

float MultiGraph::x_position(float xfrac) const
{
	return m_pos.x() + hpad + xfrac * (m_size.x() - 2 * hpad);
}

float MultiGraph::y_position(float value) const
{
	bool hasHeaders = (m_left_header.size() + m_center_header.size() + m_right_header.size()) != 0;
	bool hasFooters = m_xticks.size() >= 2;

	int bpad = hasFooters ? 12 : 5;
	int tpad = hasHeaders ? 15 : 5;

	return m_pos.y() + m_size.y() - clamp01(value) * (m_size.y() - tpad - bpad) - bpad;
}


void MultiGraph::draw(NVGcontext *ctx)
{
	Widget::draw(ctx);

	bool hasFooters = m_xticks.size() >= 2;

	float y0 = y_position(0.0f);
	float y1 = y_position(1.0f);
	float x0 = x_position(0.0f);
	float x1 = x_position(1.0f);

	nvgStrokeWidth(ctx, 1.0f);

	if (m_in_well)
	{
		// draw a background well
		NVGpaint paint = nvgBoxGradient(ctx, m_pos.x() + 1, m_pos.y() + 1,
		                                m_size.x() - 2, m_size.y() - 2, 3, 4,
		                                Color(0, 32), Color(0, 92));
		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), 2.5);
		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}

	if (num_plots() && m_values[0].size() >= 2)
	{
		nvgSave(ctx);
		// Additive blending
		nvgGlobalCompositeBlendFunc(ctx, NVGblendFactor::NVG_SRC_ALPHA, NVGblendFactor::NVG_ONE);

		nvgLineJoin(ctx, NVG_BEVEL);
		for (int plot = 0; plot < num_plots(); ++plot)
		{
			const std::vector<float> &v = m_values[plot];
			if (v.size() < 2)
				return;

			float invSize = 1.f / (v.size()-1);
			nvgBeginPath(ctx);
			if (m_filled)
			{
				nvgMoveTo(ctx, x0, y0);
				nvgLineTo(ctx, x0, y_position(v[0]));
			}
			else
				nvgMoveTo(ctx, x0, y_position(v[0]));

			for (size_t i = 1; i < (size_t) v.size(); ++i)
				nvgLineTo(ctx, x_position(i*invSize), y_position(v[i]));

			if (m_filled)
			{
				nvgLineTo(ctx, x1, y0);
				nvgFillColor(ctx, m_foreground_colors[plot]);
				nvgFill(ctx);
			}
			Color sColor = m_foreground_colors[plot];
			sColor.w() = 1.0f;//(sColor.w() + 1.0f) / 2.0f;
			nvgStrokeColor(ctx, sColor);
			nvgStroke(ctx);
		}

		nvgRestore(ctx);
	}

	nvgFontFace(ctx, "sans");

	Color axisColor = Color(0.8f, 0.8f);

	float prevTextBound = 0;
	float lastTextBound = 0;
	float xPos = 0;
	float yPos = 0;
	float textWidth = 0.0f;

	if (hasFooters)
	{
		// draw horizontal axis
		nvgBeginPath(ctx);
		nvgStrokeColor(ctx, axisColor);
		nvgMoveTo(ctx, x0, y0);
		nvgLineTo(ctx, x1, y0);
		nvgStroke(ctx);

		nvgFontSize(ctx, 9.0f);
		nvgTextAlign(ctx, NVG_ALIGN_MIDDLE | NVG_ALIGN_TOP);
		nvgFillColor(ctx, axisColor);

		// tick and label at 0
		xPos = x_position(m_xticks[0]);
		nvgBeginPath(ctx);
		nvgMoveTo(ctx, xPos, y0 - 3);
		nvgLineTo(ctx, xPos, y0 + 3);
		nvgStroke(ctx);

		textWidth = nvgTextBounds(ctx, 0, 0, m_xtick_labels.front().c_str(), nullptr, nullptr);
		xPos -= textWidth / 2;
		nvgText(ctx, xPos, y0 + 2, m_xtick_labels.front().c_str(), NULL);
		prevTextBound = xPos + textWidth;

		// tick and label at max
		xPos = x_position(m_xticks[m_xticks.size()-1]);
		nvgBeginPath(ctx);
		nvgMoveTo(ctx, xPos, y0 - 3);
		nvgLineTo(ctx, xPos, y0 + 3);
		nvgStroke(ctx);

		textWidth = nvgTextBounds(ctx, 0, 0, m_xtick_labels.back().c_str(), nullptr, nullptr);
		xPos -= textWidth / 2;
		nvgText(ctx, xPos, y0 + 2, m_xtick_labels.back().c_str(), NULL);
		lastTextBound = xPos;

		int numTicks = m_xticks.size();
		for (int i = 1; i < numTicks; ++i)
		{
			// tick
			xPos = x_position(m_xticks[i]);
			nvgBeginPath(ctx);
			nvgMoveTo(ctx, xPos, y0 - 2);
			nvgLineTo(ctx, xPos, y0 + 2);
			nvgStroke(ctx);

			// tick label
			textWidth = nvgTextBounds(ctx, 0, 0, m_xtick_labels[i].c_str(), nullptr, nullptr);
			xPos -= textWidth / 2;

			// only draw the label if it doesn't overlap with the previous one
			// and the last one
			if (xPos > prevTextBound + textPad &&
				xPos + textWidth < lastTextBound - textPad)
			{
				nvgText(ctx, xPos, y0 + 2, m_xtick_labels[i].c_str(), NULL);
				prevTextBound = xPos + textWidth;
			}
		}
	}

	if (m_yticks.size() >= 2)
	{
		// draw vertical axis
		nvgBeginPath(ctx);
		nvgStrokeColor(ctx, axisColor);
		nvgMoveTo(ctx, x0, y0);
		nvgLineTo(ctx, x0, y1);
		nvgStroke(ctx);

		nvgFillColor(ctx, axisColor);

		int numTicks = m_yticks.size();
		for (int i = 0; i < numTicks; ++i)
		{
			// tick
			yPos = y_position(m_yticks[i]);
			nvgBeginPath(ctx);
			int w2 = (i == 0 || i == numTicks-1) ? 3 : 2;
			nvgMoveTo(ctx, x0 - w2, yPos);
			nvgLineTo(ctx, x0 + w2, yPos);
			nvgStroke(ctx);
		}
	}

	// show the headers
	nvgFontSize(ctx, 12.0f);
	nvgFillColor(ctx, m_text_color);

	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgText(ctx, m_pos.x() + 3, m_pos.y() + 1, m_left_header.c_str(), NULL);

	nvgTextAlign(ctx, NVG_ALIGN_MIDDLE | NVG_ALIGN_TOP);
	textWidth = nvgTextBounds(ctx, 0, 0, m_center_header.c_str(), nullptr, nullptr);
	nvgText(ctx, m_pos.x() + m_size.x() / 2 - textWidth / 2, m_pos.y() + 1, m_center_header.c_str(), NULL);

	nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	nvgText(ctx, m_pos.x() + m_size.x() - 3, m_pos.y() + 1, m_right_header.c_str(), NULL);

	nvgFontFace(ctx, "sans");
}

bool MultiGraph::mouse_drag_event(const Vector2i &p, const Vector2i & /* rel */,
                                int /* button */, int /* modifiers */)
{
	if (!m_enabled)
		return false;

	if (m_drag_callback)
		m_drag_callback(graph_coordinate_at(Vector2f(p)));

	return true;
}

bool MultiGraph::mouse_button_event(const Vector2i &p, int /* button */, bool down, int /* modifiers */)
{
	if (!m_enabled)
		return false;

	if (m_drag_callback)
		m_drag_callback(graph_coordinate_at(Vector2f(p)));

	return true;
}