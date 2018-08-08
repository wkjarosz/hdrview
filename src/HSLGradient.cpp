//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "HSLGradient.h"
#include <nanogui/opengl.h>

using namespace nanogui;
using namespace std;


HSLGradient::HSLGradient(Widget *parent)
	: Widget(parent)
{

}

Vector2i HSLGradient::preferredSize(NVGcontext *) const
{
	return { 100, 10. };
}

void HSLGradient::draw(NVGcontext *ctx)
{
	Widget::draw(ctx);

	if (!mVisible)
		return;

	float offset = mod(mHue/60.f, 6.f);
	float integer = floor(offset);
	float remainder = offset - integer;
	for (int i = -1; i < 6; i++)
	{
		float x0 = mPos.x() + (i + remainder) / 6.f * mSize.x();
		float x1 = mPos.x() + (i + remainder + 1.f) / 6.f * mSize.x();

		NVGpaint paint = nvgLinearGradient(ctx, x0, 0, x1, 0,
		                                   nvgHSL((i - integer) / 6.f, mSaturation, mLightness),
		                                   nvgHSL((i - integer + 1.f) / 6.f, mSaturation, mLightness));
		nvgBeginPath(ctx);
		nvgRect(ctx, std::floor(x0), mPos.y(), std::ceil(mSize.x() / 6.f), mSize.y());
		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}
}