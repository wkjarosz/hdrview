//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This file is adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#define _USE_MATH_DEFINES
#include "imagebutton.h"
#include <nanogui/opengl.h>
#include <nanogui/entypo.h>
#include <nanogui/theme.h>
#include <iostream>

using namespace nanogui;
using namespace std;

ImageButton::ImageButton(Widget *parent, const string &caption)
	: Widget (parent), m_caption(caption), m_canBeReference(true)
{
	mFontSize = 15;
}

Vector2i ImageButton::preferredSize(NVGcontext *ctx) const
{
	// calculate size of the image iD number
	nvgFontFace(ctx, "sans-bold");
	nvgFontSize(ctx, mFontSize);
	string idString = to_string(m_id);
	float idSize = nvgTextBounds(ctx, 0, 0, idString.c_str(), nullptr, nullptr);

	// calculate space for isModified icon
	nvgFontFace(ctx, "icons");
	nvgFontSize(ctx, mFontSize * 1.5f);
	float iw = nvgTextBounds(ctx, 0, 0, utf8(ENTYPO_ICON_PENCIL).data(), nullptr, nullptr);

	// calculate size of the filename
	nvgFontFace(ctx, "sans");
	nvgFontSize(ctx, mFontSize);
	float tw = nvgTextBounds(ctx, 0, 0, m_caption.c_str(), nullptr, nullptr);

	return Vector2i(static_cast<int>(tw + iw + idSize) + 15, mFontSize + 6);
}

bool ImageButton::mouseButtonEvent(const Vector2i &p, int button, bool down, int modifiers)
{
	Widget::mouseButtonEvent(p, button, down, modifiers);

	if (!mEnabled || !down)
	{
		return false;
	}

	if (m_canBeReference && (button == GLFW_MOUSE_BUTTON_2 ||
		(button == GLFW_MOUSE_BUTTON_1 && modifiers & GLFW_MOD_SHIFT)))
	{
		// If we already were the reference, then let's disable using us a reference.
		m_isReference = !m_isReference;

		// If we newly became the reference, then we need to disable the existing reference
		// if it exists.
		if (m_isReference)
		{
			for (auto widget : parent()->children())
			{
				ImageButton* b = dynamic_cast<ImageButton*>(widget);
				if (b && b != this)
					b->m_isReference = false;
			}
		}

		// Invoke the callback in any case, such that the surrounding code can
		// react to new references or a loss of a reference image.
		if (m_referenceCallback)
			m_referenceCallback(m_isReference ? m_id : -1);

		return true;
	}
	else if (button == GLFW_MOUSE_BUTTON_1)
	{
		if (!m_isSelected)
		{
			// Unselect the other, currently selected image.
			for (auto widget : parent()->children())
			{
				ImageButton *b = dynamic_cast<ImageButton *>(widget);
				if (b && b != this)
					b->m_isSelected = false;
			}

			m_isSelected = true;
			if (m_selectedCallback)
				m_selectedCallback(m_id);
		}
		return true;
	}

	return false;
}


static float triangleWave(float t, float period = 1.f)
{
	float a = period/2.f;
	return fabs(2 * (t/a - floor(t/a + 0.5f)));
}

void ImageButton::draw(NVGcontext *ctx)
{
	Widget::draw(ctx);

	int extraBorder = 0;
	if (m_isReference)
	{
		extraBorder = 2;
		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, mPos.x(), mPos.y(), mSize.x(), mSize.y(), 3+1);
		nvgFillColor(ctx, Color(0.7f, 0.4f, 0.4f, 1.0f));
		nvgFill(ctx);
	}

	// Fill the button with color.
	if (m_isSelected || mMouseFocus)
	{
		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, mPos.x() + extraBorder, mPos.y() + extraBorder,
		        mSize.x() - 2*extraBorder, mSize.y() - 2*extraBorder, 3);
		nvgFillColor(ctx, m_isSelected ? mTheme->mButtonGradientBotPushed : mTheme->mBorderMedium);
		nvgFill(ctx);
	}

	// percent progress bar
	if (m_progress >= 0.f && m_progress < 1.f)
	{
		int barPos = (int) std::round((mSize.x() - 4) * m_progress);

		auto paint = nvgBoxGradient(
			ctx, mPos.x() + 2 - 1, mPos.y() + 2 -1,
			barPos + 1.5f, mSize.y() - 2*extraBorder + 1, 3, 4,
			Color(.14f, .31f, .5f, .95f), Color(.045f, .05f, .141f, .95f));

		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, mPos.x() + 2, mPos.y() + 2,
		               barPos, mSize.y() - 2*2, 3);
		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}
	// busy progress bar
	else if (m_progress < 0.f)
	{
		int leftEdge  = mPos.x() + 2;
		float time = glfwGetTime();
		float anim1 = smoothStep(0.0f, 1.0f, smoothStep(0.0f, 1.0f, smoothStep(0.0f, 1.0f, triangleWave(time/4.f))));
		float anim2 = smoothStep(0.0f, 1.0f, triangleWave(time/4.f*2.f));

		int barSize = (int) std::round(lerp(float(mSize.x() - 4) * 0.05f, float(mSize.x() - 4) * 0.25f, anim2));
		int left = (int) std::round(lerp((float)leftEdge, float(mSize.x() - 2 - barSize), anim1));

		auto paint = nvgBoxGradient(
			ctx, left - 1, mPos.y() + 2 -1,
			barSize + 1.5f, mSize.y() - 2*extraBorder + 1, 3, 4,
			Color(.14f, .31f, .5f, .95f), Color(.045f, .05f, .141f, .95f));

		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, left, mPos.y() + 2,
		               barSize, mSize.y() - 2*2, 3);
		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}

	nvgFontSize(ctx, mFontSize);
	nvgFontFace(ctx, "sans-bold");
	string idString = to_string(m_id);
	float idSize = nvgTextBounds(ctx, 0, 0, idString.c_str(), nullptr, nullptr);

	nvgFontSize(ctx, mFontSize * 1.5f);
	nvgFontFace(ctx, "icons");
	float iconSize = nvgTextBounds(ctx, 0, 0, utf8(ENTYPO_ICON_PENCIL).data(), nullptr, nullptr);

	nvgFontSize(ctx, mFontSize);
	nvgFontFace(ctx, m_isSelected ? "sans-bold" : "sans");

	if (mSize.x() == preferredSize(ctx).x())
		m_cutoff = 0;
	else if (mSize != m_sizeForWhichCutoffWasComputed)
	{
		m_cutoff = 0;
		while (nvgTextBounds(ctx, 0, 0, m_caption.substr(m_cutoff).c_str(), nullptr, nullptr) > mSize.x() - 25 - idSize - iconSize)
			++m_cutoff;

		m_sizeForWhichCutoffWasComputed = mSize;
	}

	string caption = m_caption.substr(m_cutoff);
	if (m_cutoff > 0)
		caption = string("…") + caption;

	Vector2f center = mPos.cast<float>() + mSize.cast<float>() * 0.5f;
	Vector2f bottomRight = mPos.cast<float>() + mSize.cast<float>();
	Vector2f textPos(bottomRight.x() - 5, center.y());
	NVGcolor textColor = (m_isSelected || m_isReference || mMouseFocus) ? mTheme->mTextColor : Color(180, 255);

	// Image name
	nvgFontSize(ctx, mFontSize);
	nvgFontFace(ctx, m_isSelected ? "sans-bold" : "sans");
	nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
	nvgFillColor(ctx, textColor);
	nvgText(ctx, textPos.x(), textPos.y(), caption.c_str(), nullptr);

	// isModified icon
	auto icon = utf8(m_isModified ? ENTYPO_ICON_PENCIL : ENTYPO_ICON_SAVE);
	nvgFontSize(ctx, mFontSize * 1.5f);
	nvgFontFace(ctx, "icons");
	nvgFillColor(ctx, textColor);
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgText(ctx, mPos.x() + 5, textPos.y(), icon.data(), nullptr);

	// Image number
	nvgFontSize(ctx, mFontSize);
	nvgFontFace(ctx, "sans-bold");
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgFillColor(ctx, textColor);
	nvgText(ctx, mPos.x() + 20, textPos.y(), idString.c_str(), nullptr);
}
