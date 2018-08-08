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
	MultiGraph(Widget *parent, const Color & fg = Color(255, 192, 0, 128),
	           const VectorXf & v = VectorXf());

	const Color &backgroundColor() const { return mBackgroundColor; }
	void setBackgroundColor(const Color &backgroundColor) { mBackgroundColor = backgroundColor; }

	const Color &textColor() const { return mTextColor; }
	void setTextColor(const Color &textColor) { mTextColor = textColor; }

	int numPlots() const {return mValues.size();}
	void addPlot(const Color & fg = Color(), const VectorXf & v = VectorXf()) {mValues.push_back(v); mForegroundColors.push_back(fg);}
	void popPlot() {mValues.pop_back(); mForegroundColors.pop_back();}

	bool well() const       { return mInWell; }
	void setWell(bool b)    { mInWell = b; }

	bool filled() const     { return mFilled; }
	void setFilled(bool b)  { mFilled = b; }

	const Color &foregroundColor(int plot = 0) const { return mForegroundColors[plot]; }
	void setForegroundColor(const Color &foregroundColor, int plot = 0) { mForegroundColors[plot] = foregroundColor; }

	const VectorXf &values(int plot = 0) const { return mValues[plot]; }
	VectorXf &values(int plot = 0) { return mValues[plot]; }
	void setValues(const VectorXf &values, int plot = 0) { mValues[plot] = values; }

	void setXTicks(const VectorXf & ticks, const std::vector<std::string> & labels);
	void setYTicks(const VectorXf & ticks)      { mYTicks = ticks; }
	void setLeftHeader(const std::string & s)   { mLeftHeader = s; }
	void setCenterHeader(const std::string & s) { mCenterHeader = s; }
	void setRightHeader(const std::string & s)  { mRightHeader = s; }

	std::function<void(const Vector2f &)> dragCallback() const { return mDragCallback; }
	void setDragCallback(const std::function<void(const Vector2f &)> &callback) { mDragCallback = callback; }

	virtual Vector2i preferredSize(NVGcontext *ctx) const override;
	virtual void draw(NVGcontext *ctx) override;
	virtual bool mouseDragEvent(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
	virtual bool mouseButtonEvent(const Vector2i &p, int button, bool down, int modifiers) override;

	virtual void save(Serializer &s) const override;
	virtual bool load(Serializer &s) override;

protected:
	Vector2f graphCoordinateAt(const Vector2f& position) const;
	float xPosition(float xfrac) const;
	float yPosition(float yfrac) const;

	Color mBackgroundColor, mTextColor;
	std::vector<Color> mForegroundColors;
	std::vector<VectorXf> mValues;
	bool mFilled = true, mInWell = true;
	std::string mLeftHeader, mCenterHeader, mRightHeader;
	VectorXf mXTicks, mYTicks;
	std::vector<std::string> mXTickLabels;

	std::function<void(const Vector2f &)> mDragCallback;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};