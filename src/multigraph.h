/*!
    \file multigraph.h
    \author Wojciech Jarosz
*/
#pragma once

#include <nanogui/widget.h>

NAMESPACE_BEGIN(nanogui)

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

	const std::string &caption() const { return mCaption; }
	void setCaption(const std::string &caption) { mCaption = caption; }

	const std::string &header() const { return mHeader; }
	void setHeader(const std::string &header) { mHeader = header; }

	const std::string &footer() const { return mFooter; }
	void setFooter(const std::string &footer) { mFooter = footer; }

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

	virtual Vector2i preferredSize(NVGcontext *ctx) const override;
	virtual void draw(NVGcontext *ctx) override;

	virtual void save(Serializer &s) const override;
	virtual bool load(Serializer &s) override;

protected:
	std::string mCaption, mHeader, mFooter;
	Color mBackgroundColor, mTextColor;
	std::vector<Color> mForegroundColors;
	std::vector<VectorXf> mValues;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

NAMESPACE_END(nanogui)