// adapted from tev
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include "common.h"

#include <nanogui/widget.h>

#include <string>

class ImageButton : public nanogui::Widget
{
public:
	ImageButton(nanogui::Widget *parent, const std::string &caption);

	Eigen::Vector2i preferredSize(NVGcontext *ctx) const override;
	bool mouseButtonEvent(const Eigen::Vector2i &p, int button, bool down, int modifiers) override;
	void draw(NVGcontext *ctx) override;

	const std::string & caption() const     { return m_caption; }
	void setId(size_t id)                   { m_id = id; }
	void setIsModified(bool b)              { m_isModified = b; }
	bool isModified() const                 { return m_isModified; }
	bool isSelected() const                 { return m_isSelected; }
	void setIsSelected(bool isSelected)     { m_isSelected = isSelected; }

	void setSelectedCallback(const std::function<void()> & callback)
	{
		m_selectedCallback = callback;
	}

private:
	std::string m_caption;

	bool m_isModified = false;
	bool m_isSelected = false;
	std::function<void()> m_selectedCallback;

	size_t m_id = 0;
	size_t m_cutoff = 0;
	Eigen::Vector2i m_sizeForWhichCutoffWasComputed = Eigen::Vector2i::Constant(0);
};
