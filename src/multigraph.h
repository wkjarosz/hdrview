//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>

using namespace nanogui;

/**
 * \class MultiGraph multigraph.h
 *
 * \brief A generalization of nanogui's graph widget which can plot multiple graphs on top of each other
 */
class MultiGraph : public Widget
{
public:
	MultiGraph(Widget *parent, const std::string &caption = "Untitled",
	           const Color & fg = Color(255, 192, 0, 128),
	           const VectorXf & v = VectorXf());

	const Color &backgroundColor() const { return mBackgroundColor; }
	void setBackgroundColor(const Color &backgroundColor) { mBackgroundColor = backgroundColor; }

	const Color &textColor() const { return mTextColor; }
	void setTextColor(const Color &textColor) { mTextColor = textColor; }

	int numPlots() const {return mValues.size();}
	void addPlot(const Color & fg = Color(), const VectorXf & v = VectorXf()) {mValues.push_back(v); mForegroundColors.push_back(fg);}
	void popPlot() {mValues.pop_back(); mForegroundColors.pop_back();}

	const Color &foregroundColor(int plot = 0) const { return mForegroundColors[plot]; }
	void setForegroundColor(const Color &foregroundColor, int plot = 0) { mForegroundColors[plot] = foregroundColor; }

	const VectorXf &values(int plot = 0) const { return mValues[plot]; }
	VectorXf &values(int plot = 0) { return mValues[plot]; }
	void setValues(const VectorXf &values, int plot = 0) { mValues[plot] = values; }

	void setXTicks(const VectorXf & ticks, const std::vector<std::string> & labels);
	void setMinimum(float minimum)  { m_minimum = minimum; }
	void setAverage(float average)  { m_average = average; }
	void setMaximum(float maximum)  { m_maximum = maximum; }

	virtual Vector2i preferredSize(NVGcontext *ctx) const override;
	virtual void draw(NVGcontext *ctx) override;

	virtual void save(Serializer &s) const override;
	virtual bool load(Serializer &s) override;

protected:
	Color mBackgroundColor, mTextColor;
	std::vector<Color> mForegroundColors;
	std::vector<VectorXf> mValues;
	float m_minimum = 0, m_average = 0, m_maximum = 0;
	VectorXf mXTicks;
	std::vector<std::string> mXTickLabels;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};