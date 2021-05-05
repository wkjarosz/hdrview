//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hslgradient.h"
#include <nanogui/opengl.h>

using namespace nanogui;
using namespace std;


HSLGradient::HSLGradient(Widget *parent)
	: Widget(parent)
{

}

Vector2i HSLGradient::preferred_size(NVGcontext *) const
{
	return { 100, 10 };
}

void HSLGradient::draw(NVGcontext *ctx)
{
	Widget::draw(ctx);

	if (!m_visible)
		return;

	float offset = mod(m_hue/60.f, 6.f);
	float integer = floor(offset);
	float remainder = offset - integer;
	for (int i = -1; i < 6; i++)
	{
		float x0 = m_pos.x() + (i + remainder) / 6.f * m_size.x();
		float x1 = m_pos.x() + (i + remainder + 1.f) / 6.f * m_size.x();

		NVGpaint paint = nvgLinearGradient(ctx, x0, 0, x1, 0,
		                                   nvgHSL((i - integer) / 6.f, m_saturation, m_lightness),
		                                   nvgHSL((i - integer + 1.f) / 6.f, m_saturation, m_lightness));
		nvgBeginPath(ctx);
		nvgRect(ctx, std::floor(x0), m_pos.y(), std::ceil(m_size.x() / 6.f), m_size.y());
		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}
}