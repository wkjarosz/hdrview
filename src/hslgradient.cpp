//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hslgradient.h"
#include <nanogui/opengl.h>

using namespace std;

NAMESPACE_BEGIN(nanogui)

HSLGradient::HSLGradient(Widget *parent)
	: Widget(parent)
{

}

Vector2i HSLGradient::preferred_size(NVGcontext *) const
{
	return Vector2i(70, 16);
}

void HSLGradient::draw(NVGcontext *ctx)
{
	Widget::draw(ctx);

	if (!m_visible)
		return;

	Vector2f center = Vector2f(m_pos) + Vector2f(m_size) * 0.5f;
    float bar_radius = floor((m_size.y()-1) * 0.5f) - 1.f;

	float cap = 2*bar_radius + 1;
    float start_x = m_pos.x() + cap;
    float width_x = m_size.x() - 2*cap;
	float end_x = start_x + width_x;

	float offset = mod(m_hue/60.f, 6.f);
	float integer = floor(offset);
	float remainder = offset - integer;
	for (int i = -1; i < 6; i++)
	{
		float x0 = start_x + (i + remainder) / 6.f * width_x;
		float x1 = start_x + (i + remainder + 1.f) / 6.f * width_x;

		NVGpaint paint = nvgLinearGradient(ctx, x0, 0, x1, 0,
		                                   nvgHSL((i - integer) / 6.f, m_saturation, m_lightness),
		                                   nvgHSL((i - integer + 1.f) / 6.f, m_saturation, m_lightness));
		nvgBeginPath(ctx);

		x0 = std::max(x0, start_x);
		x1 = std::min(x1, end_x);
		nvgRect(ctx, std::floor(x0), center.y() - bar_radius, std::ceil(x1 - x0), cap);

		nvgFillPaint(ctx, paint);
		nvgFill(ctx);
	}

	// draw the two rounded caps
	nvgBeginPath(ctx);
	nvgRoundedRectVarying(ctx, m_pos.x()+1, center.y() - bar_radius, 2*bar_radius, cap, bar_radius, 0.f, 0.f, bar_radius);
	nvgFillColor(ctx, nvgHSL(-offset / 6.f, m_saturation, m_lightness));
	nvgFill(ctx);

	nvgBeginPath(ctx);
	nvgRoundedRectVarying(ctx, end_x, center.y() - bar_radius, 2*bar_radius, cap, 0.f, bar_radius, bar_radius, 0.f);
	nvgFillColor(ctx, nvgHSL(-offset / 6.f, m_saturation, m_lightness));
	nvgFill(ctx);

	// draw stroke around entire bar
	nvgBeginPath(ctx);
	nvgRoundedRectVarying(ctx, m_pos.x()+1, center.y() - bar_radius, m_size.x()-2, cap, bar_radius, bar_radius, bar_radius, bar_radius);
	nvgStrokeColor(ctx, Color(0, m_enabled ? 255 : 128));
    nvgStrokeWidth(ctx, 1.f);
    nvgStroke(ctx);



    // float knob_radius = (int) (m_size.y() * 0.4f);
    // const float knob_shadow = 3;
	// std::pair<float,float> m_range(0.f, 1.f);
	// float m_value = 0.5f;

    // // draw the knob
    // Vector2f knob_pos(start_x + (m_value - m_range.first) / (m_range.second - m_range.first) * (width_x), center.y() + 0.5f);
    // NVGpaint shadow_paint =
    //     nvgRadialGradient(ctx, knob_pos.x(), knob_pos.y(), knob_radius - knob_shadow,
    //                       knob_radius + knob_shadow, Color(0, 64), m_theme->m_transparent);

    // nvgBeginPath(ctx);
    // nvgRect(ctx, knob_pos.x() - knob_radius - 5, knob_pos.y() - knob_radius - 5, knob_radius * 2 + 10, knob_radius * 2 + 10 + knob_shadow);
    // nvgCircle(ctx, knob_pos.x(), knob_pos.y(), knob_radius);
    // nvgPathWinding(ctx, NVG_HOLE);
    // nvgFillPaint(ctx, shadow_paint);
    // nvgFill(ctx);

    // nvgBeginPath(ctx);
    // nvgCircle(ctx, knob_pos.x(), knob_pos.y(), knob_radius-1);
    // nvgFillColor(ctx, nvgHSL(3.f / 6.f, m_saturation, m_lightness));
    // nvgFill(ctx);
    // nvgStrokeColor(ctx, Color(m_enabled ? 0 : 64, 255));
    // nvgStrokeWidth(ctx, 2.5f);
    // nvgStroke(ctx);
    // nvgStrokeColor(ctx, Color(m_enabled ? 255 : 128, 255));
    // nvgStrokeWidth(ctx, 1.5f);
    // nvgStroke(ctx);
}

NAMESPACE_END(nanogui)