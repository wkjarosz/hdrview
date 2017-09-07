#include "multigraph.h"
#include <nanogui/theme.h>
#include <nanogui/opengl.h>
#include <nanogui/serializer/core.h>

NAMESPACE_BEGIN(nanogui)

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
	return Vector2i(256, 45);
}

void MultiGraph::draw(NVGcontext *ctx)
{
	Widget::draw(ctx);

	nvgBeginPath(ctx);
	nvgRect(ctx, mPos.x(), mPos.y(), mSize.x(), mSize.y());
	nvgFillColor(ctx, mBackgroundColor);
	nvgFill(ctx);

	for (int plot = 0; plot < numPlots(); ++plot)
	{
		const VectorXf & v = mValues[plot];
		if (v.size() < 2)
			return;

		nvgBeginPath(ctx);
		nvgMoveTo(ctx, mPos.x(), mPos.y() + mSize.y());
		for (int i = 0; i < v.size(); ++i)
		{
			float value = v[i];
			float vx = mPos.x() + i * mSize.x() / (float) (v.size() - 1);
			float vy = mPos.y() + (1 - value) * mSize.y();
			nvgLineTo(ctx, vx, vy);
		}

		nvgLineTo(ctx, mPos.x() + mSize.x(), mPos.y() + mSize.y());
		nvgFillColor(ctx, mForegroundColors[plot]);
		nvgFill(ctx);
		Color sColor = mForegroundColors[plot];
		sColor.w() = (sColor.w() + 1.0f)/2.0f;
		nvgStrokeColor(ctx, sColor);
		nvgStroke(ctx);
	}

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

	nvgBeginPath(ctx);
	nvgRect(ctx, mPos.x(), mPos.y(), mSize.x(), mSize.y());
	nvgStrokeColor(ctx, Color(100, 255));
	nvgStroke(ctx);
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

NAMESPACE_END(nanogui)