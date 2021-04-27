//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This file is adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.


#include "ImageButton.h"
#include <nanogui/opengl.h>
#include <nanogui/theme.h>
#include <nanogui/icons.h>
#include <iostream>
#include <spdlog/fmt/ostr.h>

using namespace nanogui;
using namespace std;

ImageButton::ImageButton(Widget *parent, const string &caption)
	: Widget (parent), m_caption(caption)
{
	m_font_size = 15;
}

void ImageButton::recompute_string_clipping()
{
	m_cutoff = 0;
	m_size_for_computed_cutoff = Vector2i(0);
}

Vector2i ImageButton::preferred_size(NVGcontext *ctx) const
{
	// calculate size of the image iD number
	nvgFontFace(ctx, "sans-bold");
	nvgFontSize(ctx, m_font_size);
	string id_string = to_string(m_id);
	float id_size = nvgTextBounds(ctx, 0, 0, id_string.c_str(), nullptr, nullptr);

	// calculate space for is_modified icon
	nvgFontFace(ctx, "icons");
	nvgFontSize(ctx, m_font_size * 1.5f);
	float iw = nvgTextBounds(ctx, 0, 0, utf8(FA_PENCIL_ALT).data(), nullptr, nullptr);

	// calculate size of the filename
	nvgFontFace(ctx, "sans");
	nvgFontSize(ctx, m_font_size);
	float tw = nvgTextBounds(ctx, 0, 0, m_caption.c_str(), nullptr, nullptr);

	return Vector2i(static_cast<int>(tw + iw + id_size) + 15, m_font_size + 6);
}

bool ImageButton::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers)
{
	Widget::mouse_button_event(p, button, down, modifiers);

	if (!m_enabled || !down)
	{
		return false;
	}

	if (button == GLFW_MOUSE_BUTTON_2 ||
		(button == GLFW_MOUSE_BUTTON_1 && modifiers & GLFW_MOD_SHIFT))
	{
		// If we already were the reference, then let's disable using us a reference.
		m_is_reference = !m_is_reference;

		// If we newly became the reference, then we need to disable the existing reference
		// if it exists.
		if (m_is_reference)
		{
			for (auto widget : parent()->children())
			{
				ImageButton* b = dynamic_cast<ImageButton*>(widget);
				if (b && b != this)
					b->m_is_reference = false;
			}
		}

		// Invoke the callback in any case, such that the surrounding code can
		// react to new references or a loss of a reference image.
		if (m_reference_callback)
			m_reference_callback(m_is_reference ? m_id : -1);

		return true;
	}
	else if (button == GLFW_MOUSE_BUTTON_1)
	{
		if (!m_is_selected)
		{
			// Unselect the other, currently selected image.
			for (auto widget : parent()->children())
			{
				ImageButton *b = dynamic_cast<ImageButton *>(widget);
				if (b && b != this)
					b->m_is_selected = false;
			}

			m_is_selected = true;
			if (m_selected_callback)
				m_selected_callback(m_id);
		}
		return true;
	}

	return false;
}


string ImageButton::highlighted() const
{
	vector<string> pieces;
	if (m_highlight_begin <= 0)
	{
		if (m_highlight_end <= 0)
			pieces.emplace_back(m_caption);
		else
		{
			size_t offset = m_highlight_end;
			pieces.emplace_back(m_caption.substr(offset));
			pieces.emplace_back(m_caption.substr(0, offset));
		}
	}
	else
	{
		size_t beginOffset = m_highlight_begin;
		size_t endOffset = m_highlight_end;
		pieces.emplace_back(m_caption.substr(endOffset));
		pieces.emplace_back(m_caption.substr(beginOffset, endOffset - beginOffset));
		pieces.emplace_back(m_caption.substr(0, beginOffset));
	}

	return pieces.size() > 1 ? pieces[1] : "";
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
	if (m_is_reference)
	{
		extraBorder = 2;
		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), 3+1);
		nvgFillColor(ctx, Color(0.7f, 0.4f, 0.4f, 1.0f));
		nvgFill(ctx);
	}

	// Fill the button with color.
	if (m_is_selected || m_mouse_focus)
	{
		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, m_pos.x() + extraBorder, m_pos.y() + extraBorder,
		        m_size.x() - 2*extraBorder, m_size.y() - 2*extraBorder, 3);
		nvgFillColor(ctx, m_is_selected ? m_theme->m_button_gradient_bot_pushed : m_theme->m_border_medium);
		nvgFill(ctx);
	}

	// percent progress bar
	if (m_progress >= 0.f && m_progress < 1.f)
	{
		int barPos = (int) std::round((m_size.x() - 4) * m_progress);

		auto paint = nvgBoxGradient(
			ctx, m_pos.x() + 2 - 1, m_pos.y() + 2 -1,
			barPos + 1.5f, m_size.y() - 2*extraBorder + 1, 3, 4,
			Color(.14f, .31f, .5f, .95f), Color(.045f, .05f, .141f, .95f));

		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, m_pos.x() + 2, m_pos.y() + 2,
		               barPos, m_size.y() - 2*2, 3);
		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}
	// busy progress bar
	else if (m_progress < 0.f)
	{
		int leftEdge  = m_pos.x() + 2;
		float time = glfwGetTime();
		float anim1 = smoothStep(0.0f, 1.0f, smoothStep(0.0f, 1.0f, smoothStep(0.0f, 1.0f, triangleWave(time/4.f))));
		float anim2 = smoothStep(0.0f, 1.0f, triangleWave(time/4.f*2.f));

		int barSize = (int) std::round(lerp(float(m_size.x() - 4) * 0.05f, float(m_size.x() - 4) * 0.25f, anim2));
		int left = (int) std::round(lerp((float)leftEdge, float(m_size.x() - 2 - barSize), anim1));

		auto paint = nvgBoxGradient(
			ctx, left - 1, m_pos.y() + 2 -1,
			barSize + 1.5f, m_size.y() - 2*extraBorder + 1, 3, 4,
			Color(.14f, .31f, .5f, .95f), Color(.045f, .05f, .141f, .95f));

		nvgBeginPath(ctx);
		nvgRoundedRect(ctx, left, m_pos.y() + 2,
		               barSize, m_size.y() - 2*2, 3);
		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}

	nvgFontSize(ctx, m_font_size);
	nvgFontFace(ctx, "sans-bold");
	string id_string = to_string(m_id);
	float id_size = nvgTextBounds(ctx, 0, 0, id_string.c_str(), nullptr, nullptr);

	nvgFontSize(ctx, m_font_size * 1.5f);
	nvgFontFace(ctx, "icons");
	float iconSize = nvgTextBounds(ctx, 0, 0, utf8(FA_PENCIL_ALT).data(), nullptr, nullptr);

	nvgFontSize(ctx, m_font_size);
	nvgFontFace(ctx, m_is_selected ? "sans-bold" : "sans");

	// trim caption to available space
	if (m_size.x() == preferred_size(ctx).x())
		m_cutoff = 0;
	else if (m_size != m_size_for_computed_cutoff)
	{
		m_cutoff = 0;
		while (nvgTextBounds(ctx, 0, 0, m_caption.substr(m_cutoff).c_str(), nullptr, nullptr) > m_size.x() - 15 - id_size - iconSize)
			++m_cutoff;

		m_size_for_computed_cutoff = m_size;
	}

	// Image name
	string trimmedCaption = m_caption.substr(m_cutoff);


	vector<string> pieces;
	if (m_highlight_begin <= m_cutoff)
	{
		if (m_highlight_end <= m_cutoff)
			pieces.emplace_back(trimmedCaption);
		else
		{
			size_t offset = m_highlight_end - m_cutoff;
			pieces.emplace_back(trimmedCaption.substr(offset));
			pieces.emplace_back(trimmedCaption.substr(0, offset));
		}
	}
	else
	{
		size_t beginOffset = m_highlight_begin - m_cutoff;
		size_t endOffset = m_highlight_end - m_cutoff;
		pieces.emplace_back(trimmedCaption.substr(endOffset));
		pieces.emplace_back(trimmedCaption.substr(beginOffset, endOffset - beginOffset));
		pieces.emplace_back(trimmedCaption.substr(0, beginOffset));
	}

	if (m_cutoff > 0 && m_cutoff < m_caption.size())
		pieces.back() = string{"…"} + pieces.back();

	Vector2f center = Vector2f(m_pos) + Vector2f(m_size) * 0.5f;
	Vector2f bottom_right = Vector2f(m_pos) + Vector2f(m_size);
	Vector2f text_pos(bottom_right.x() - 5, center.y());
	NVGcolor regular_text_color = (m_is_selected || m_is_reference || m_mouse_focus) ? m_theme->m_text_color : Color(190, 100);
	NVGcolor highlighted_text_color = Color(190, 255);

	nvgFontSize(ctx, m_font_size);
	nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

	for (size_t i = 0; i < pieces.size(); ++i)
	{
		nvgFontFace(ctx, i == 1 ? "sans-bold" : "sans");
		nvgFillColor(ctx, i == 1 ? highlighted_text_color : regular_text_color);
		nvgText(ctx, text_pos.x(), text_pos.y(), pieces[i].c_str(), nullptr);
		text_pos.x() -= nvgTextBounds(ctx, 0, 0, pieces[i].c_str(), nullptr, nullptr);
	}

	// is_modified icon
	auto icon = utf8(m_is_modified ? FA_PENCIL_ALT : FA_SAVE);
	nvgFontSize(ctx, m_font_size * 0.8f);
	nvgFontFace(ctx, "icons");
	nvgFillColor(ctx, m_theme->m_text_color);
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgText(ctx, m_pos.x() + 5, text_pos.y(), icon.data(), nullptr);

	// Image number
	nvgFontSize(ctx, m_font_size);
	nvgFontFace(ctx, "sans-bold");
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgFillColor(ctx, m_theme->m_text_color);
	nvgText(ctx, m_pos.x() + 20, text_pos.y(), id_string.c_str(), nullptr);
}


void ImageButton::set_highlight_range(size_t begin, size_t end)
{
	size_t beginIndex = begin;
	if (end > m_caption.size())
	{
		throw std::invalid_argument{fmt::format(
			"end ({:d}) must not be larger than m_caption.size() ({:d})",
			end, m_caption.size())};
	}

	m_highlight_begin = beginIndex;
	m_highlight_end = max(m_caption.size() - end, beginIndex);

	if (m_highlight_begin == m_highlight_end || m_caption.empty())
		return;

	// Extend beginning and ending of highlighted region to entire word/number
	if (isalnum(m_caption[m_highlight_begin]))
		while (m_highlight_begin > 0 && isalnum(m_caption[m_highlight_begin - 1]))
			--m_highlight_begin;

	if (isalnum(m_caption[m_highlight_end - 1]))
		while (m_highlight_end < m_caption.size() && isalnum(m_caption[m_highlight_end]))
			++m_highlight_end;
}
