//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "multigraph.h"
#include <nanogui/theme.h>
#include <nanogui/opengl.h>
#include <nanogui/serializer/core.h>
#include <spdlog/fmt/fmt.h>
#include "common.h"
#include "colorspace.h"

/*!
 * @param parent	The parent widget
 * @param caption 	Caption text
 * @param fg 		The foreground color of the first plot
 * @param v 		The value vector for the first plot
 */
MultiGraph::MultiGraph(Widget *parent, const std::string &caption,
                       const Color & fg, const VectorXf & v)
	: Widget(parent), mCaption(caption),
	  mBackgroundColor(20, 128), mTextColor(240, 192)
{
	mForegroundColors.push_back(fg);
	mValues.push_back(v);
}

Vector2i MultiGraph::preferredSize(NVGcontext *) const
{
	return Vector2i(256, 75);
}

void MultiGraph::draw(NVGcontext *ctx)
{
	Widget::draw(ctx);

	// draw a background well
	NVGpaint paint = nvgBoxGradient(ctx, mPos.x() + 1, mPos.y() + 1,
	                                mSize.x()-2, mSize.y()-2, 3, 4,
	                                Color(0, 32), Color(0, 92));
	nvgBeginPath(ctx);
	nvgRoundedRect(ctx, mPos.x(), mPos.y(), mSize.x(), mSize.y(), 2.5);
	nvgFillPaint(ctx, paint);
	nvgFill(ctx);

	const int hpad = 10;
	const int bpad = 12;
	const int tpad = 5;

	if (numPlots() && mValues[0].size() >= 2)
	{
		nvgSave(ctx);
		// Additive blending
		nvgGlobalCompositeBlendFunc(ctx, NVGblendFactor::NVG_SRC_ALPHA, NVGblendFactor::NVG_ONE);

		nvgLineJoin(ctx, NVG_BEVEL);
		for (int plot = 0; plot < numPlots(); ++plot)
		{
			const VectorXf &v = mValues[plot];
			if (v.size() < 2)
				return;

			nvgBeginPath(ctx);
			nvgMoveTo(ctx, mPos.x() + hpad, mPos.y() + mSize.y() - bpad);
			for (int i = 0; i < v.size(); ++i)
			{
				float value = v[i];
				float vx = mPos.x() + hpad + i * (mSize.x() - 2 * hpad) / (float) (v.size() - 1);
				float vy = mPos.y() + mSize.y() - clamp(value, 0.0f, 1.0f) * (mSize.y() - tpad - bpad) - bpad;
				nvgLineTo(ctx, vx, vy);
			}

			nvgLineTo(ctx, mPos.x() + mSize.x() - hpad, mPos.y() + mSize.y() - bpad);
			nvgFillColor(ctx, mForegroundColors[plot]);
			nvgFill(ctx);
			Color sColor = mForegroundColors[plot];
			sColor.w() = (sColor.w() + 1.0f) / 2.0f;
			nvgStrokeColor(ctx, sColor);
			nvgStroke(ctx);
		}

		nvgRestore(ctx);
	}

	nvgFontFace(ctx, "sans");

	Color axisColor = Color(0.8f, 0.8f);

	// draw horizontal axis, ticks, and labels
	nvgBeginPath(ctx);
	nvgStrokeColor(ctx, axisColor);
	nvgMoveTo(ctx, mPos.x() + hpad/2, mPos.y() + mSize.y() - bpad);
	nvgLineTo(ctx, mPos.x() + mSize.x() - hpad/2, mPos.y() + mSize.y() - bpad);
	nvgStroke(ctx);

	nvgFontSize(ctx, 9.0f);
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillColor(ctx, axisColor);
	nvgText(ctx, mPos.x() + 3, mPos.y() + mSize.y() - bpad + 2, "0.000", NULL);

	// tick
	nvgBeginPath(ctx);
	nvgMoveTo(ctx, mPos.x() + hpad, mPos.y() + mSize.y() - bpad - 3);
	nvgLineTo(ctx, mPos.x() + hpad, mPos.y() + mSize.y() - bpad + 3);
	nvgStroke(ctx);

	int numTicks = 4;
	for (int i = 1; i < numTicks; ++i)
	{
		if (!m_linear && i > numTicks/2)
			break;

		float frac = float(i) / numTicks;
		float locFrac = m_linear ? frac : LinearToSRGB(frac);
		nvgTextAlign(ctx, NVG_ALIGN_MIDDLE | NVG_ALIGN_TOP);
		nvgFillColor(ctx, axisColor);
		std::string midString = fmt::format("{:.3f}", m_displayMax * frac);
		float textWidth = nvgTextBounds(ctx, 0, 0, midString.c_str(), nullptr, nullptr);
		int xPos = mPos.x() + std::round(mSize.x() * locFrac - textWidth / 2.f);
		nvgText(ctx, xPos, mPos.y() + mSize.y() - bpad + 2, midString.c_str(), NULL);

		// tick
		xPos = mPos.x() + std::round(mSize.x() * locFrac);
		nvgBeginPath(ctx);
		nvgMoveTo(ctx, xPos, mPos.y() + mSize.y() - bpad - 3);
		nvgLineTo(ctx, xPos, mPos.y() + mSize.y() - bpad + 3);
		nvgStroke(ctx);
	}

	nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	nvgFillColor(ctx, axisColor);
	nvgText(ctx, mPos.x() + mSize.x() - 3, mPos.y() + mSize.y() - bpad + 2, fmt::format("{:.3f}", m_displayMax).c_str(), NULL);

	// tick
	nvgBeginPath(ctx);
	nvgMoveTo(ctx, mPos.x() + mSize.x() - hpad, mPos.y() + mSize.y() - bpad - 3);
	nvgLineTo(ctx, mPos.x() + mSize.x() - hpad, mPos.y() + mSize.y() - bpad + 3);
	nvgStroke(ctx);

	// show top stats
	nvgFontSize(ctx, 12.0f);
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillColor(ctx, mTextColor);
	nvgText(ctx, mPos.x() + 3, mPos.y() + 1, fmt::format("{:.3f}", m_minimum).c_str(), NULL);

	nvgTextAlign(ctx, NVG_ALIGN_MIDDLE | NVG_ALIGN_TOP);
	nvgFillColor(ctx, mTextColor);
	std::string avgString = fmt::format("{:.3f}", m_average);
	float textWidth = nvgTextBounds(ctx, 0, 0, avgString.c_str(), nullptr, nullptr);
	nvgText(ctx, mPos.x() + mSize.x() / 2 - textWidth / 2, mPos.y() + 1, avgString.c_str(), NULL);

	nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	nvgFillColor(ctx, mTextColor);
	nvgText(ctx, mPos.x() + mSize.x() - 3, mPos.y() + 1, fmt::format("{:.3f}", m_maximum).c_str(), NULL);

	nvgFontFace(ctx, "sans");

	if (!mCaption.empty())
	{
		nvgFontSize(ctx, 14.0f);
		nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFillColor(ctx, mTextColor);
		nvgText(ctx, mPos.x() + 3, mPos.y() + 1, mCaption.c_str(), NULL);
	}

	if (!mHeader.empty())
	{
		nvgFontSize(ctx, 18.0f);
		nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		nvgFillColor(ctx, mTextColor);
		nvgText(ctx, mPos.x() + mSize.x() - 3, mPos.y() + 1, mHeader.c_str(), NULL);
	}

	if (!mFooter.empty())
	{
		nvgFontSize(ctx, 15.0f);
		nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
		nvgFillColor(ctx, mTextColor);
		nvgText(ctx, mPos.x() + mSize.x() - 3, mPos.y() + mSize.y() - 1, mFooter.c_str(), NULL);
	}
}

void MultiGraph::save(Serializer &s) const
{
	Widget::save(s);
	s.set("caption", mCaption);
	s.set("header", mHeader);
	s.set("footer", mFooter);
	s.set("backgroundColor", mBackgroundColor);
	s.set("textColor", mTextColor);
	s.set("numPlots", (int) mValues.size());
	for (int i = 0; i < (int) mValues.size(); ++i)
	{
		s.set(std::string("foregroundColor[") + std::to_string(i) + "]", mForegroundColors[i]);
		s.set(std::string("values[") + std::to_string(i) + "]", mValues[i]);
	}
}

bool MultiGraph::load(Serializer &s)
{
	if (!Widget::load(s)) return false;
	if (!s.get("caption", mCaption)) return false;
	if (!s.get("header", mHeader)) return false;
	if (!s.get("footer", mFooter)) return false;
	if (!s.get("backgroundColor", mBackgroundColor)) return false;
	if (!s.get("textColor", mTextColor)) return false;
	
	int num = 1;
	if (!s.get("numPlots", num)) return false;
	
	mValues.resize(num);
	mForegroundColors.resize(num);
	for (int i = 0; i < num; ++i)
	{
		if (!s.get(std::string("foregroundColor[") + std::to_string(i) + "]", mForegroundColors[i])) return false;
		if (!s.get(std::string("values[") + std::to_string(i) + "]", mValues[i])) return false;
	}
	return true;
}