//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "common.h"
#include <nanogui/widget.h>

class HSLGradient : public nanogui::Widget
{
public:
	HSLGradient(Widget *parent);

	void setHueOffset(float offset) { mHue = offset; }
	float hueOffset() const         { return mHue; }

	void setSaturation(float s)     { mSaturation = s; }
	float saturation() const        { return mSaturation; }

	void setLightness(float l)      { mLightness = l; }
	float lightness() const         { return mLightness; }

	virtual Eigen::Vector2i preferredSize(NVGcontext *ctx) const override;
	virtual void draw(NVGcontext *ctx) override;

protected:
	float mHue = 0.0f;
	float mSaturation = 0.5f;
	float mLightness = 0.5f;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};
