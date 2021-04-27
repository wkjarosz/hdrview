//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This file is adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include "Common.h"

#include <nanogui/widget.h>

#include <string>

class ImageButton : public nanogui::Widget
{
public:
	ImageButton(nanogui::Widget *parent, const std::string &caption);

	nanogui::Vector2i preferred_size(NVGcontext *ctx) const override;
	bool mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
	void draw(NVGcontext *ctx) override;

	float progress()                        	{ return m_progress; }
	void set_progress(float progress)        	{ m_progress = progress; }

	/// Set the button's text caption/filename
	void set_caption(const std::string &caption){ m_caption = caption; recompute_string_clipping(); }
	const std::string & caption() const         { return m_caption; }
	void set_image_id(size_t id)                { m_id = id; }
	size_t image_id() const                     { return m_id; }
	void set_is_modified(bool b)                { m_is_modified = b; }
	bool is_modified() const                    { return m_is_modified; }
	bool is_selected() const                    { return m_is_selected; }
	void set_is_selected(bool is_selected)      { m_is_selected = is_selected; }
	bool is_reference() const                   { return m_is_reference; }
	void set_is_reference(bool is_reference)    { m_is_reference = is_reference; }


	std::string highlighted() const;
	void set_highlight_range(size_t begin, size_t end);

	void recompute_string_clipping();

	void set_selected_callback(const std::function<void(int)> & callback)
	{
		m_selected_callback = callback;
	}

	void set_reference_callback(const std::function<void(int)> & callback)
	{
		m_reference_callback = callback;
	}


	void swap_with(ImageButton & other)
	{
		std::swap(m_caption, other.m_caption);
		std::swap(m_is_modified, other.m_is_modified);
//		std::swap(m_is_selected, other.m_is_selected);
//		std::swap(m_is_reference, other.m_is_reference);
//		std::swap(m_selected_callback, other.m_selected_callback);
//		std::swap(m_reference_callback, other.m_reference_callback);
//		std::swap(m_id, other.m_id);
		std::swap(m_progress, other.m_progress);
		std::swap(m_highlight_begin, other.m_highlight_begin);
		std::swap(m_highlight_end, other.m_highlight_end);
		std::swap(m_tooltip, other.m_tooltip);

		// swapping may need to recompute trimming
		m_cutoff = 0;
		m_size_for_computed_cutoff = nanogui::Vector2i(0);
		other.m_cutoff = 0;
		other.m_size_for_computed_cutoff = nanogui::Vector2i(0);
	}

private:
	std::string m_caption;

	bool m_is_modified = false;
	bool m_is_selected = false;
	bool m_is_reference = false;
	std::function<void(int)> m_selected_callback;
	std::function<void(int)> m_reference_callback;

	size_t m_id = 0;
	size_t m_cutoff = 0;
	nanogui::Vector2i m_size_for_computed_cutoff = nanogui::Vector2i(0);

	size_t m_highlight_begin = 0;
	size_t m_highlight_end = 0;

	float m_progress = -1.f;
};
