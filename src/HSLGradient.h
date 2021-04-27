//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "Common.h"
#include <nanogui/widget.h>

class HSLGradient : public nanogui::Widget
{
public:
	HSLGradient(Widget *parent);

	void set_hue_offset(float offset) { m_hue = offset; }
	float hue_offset() const         { return m_hue; }

	void set_saturation(float s)     { m_saturation = s; }
	float saturation() const        { return m_saturation; }

	void set_lightness(float l)      { m_lightness = l; }
	float lightness() const         { return m_lightness; }

	virtual nanogui::Vector2i preferred_size(NVGcontext *ctx) const override;
	virtual void draw(NVGcontext *ctx) override;

protected:
	float m_hue = 0.0f;
	float m_saturation = 0.5f;
	float m_lightness = 0.5f;
};
