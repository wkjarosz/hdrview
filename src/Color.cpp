//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "Color.h"
#include "Colorspace.h"


#define COLORSPACE_FUNCTION_WRAPPER(FUNC) \
    Color3 Color3::FUNC() const \
    { \
        Color3 ret; \
        ::FUNC(&ret.r, &ret.g, &ret.b, r, g, b); \
        return ret; \
    } \

COLORSPACE_FUNCTION_WRAPPER(LinearSRGBToXYZ)
COLORSPACE_FUNCTION_WRAPPER(XYZToLinearSRGB)
COLORSPACE_FUNCTION_WRAPPER(LinearAdobeRGBToXYZ)
COLORSPACE_FUNCTION_WRAPPER(XYZToLinearAdobeRGB)
COLORSPACE_FUNCTION_WRAPPER(XYZToLab)
COLORSPACE_FUNCTION_WRAPPER(LabToXYZ)
COLORSPACE_FUNCTION_WRAPPER(XYZToLuv)
COLORSPACE_FUNCTION_WRAPPER(LuvToXYZ)
COLORSPACE_FUNCTION_WRAPPER(RGBToHSV)
COLORSPACE_FUNCTION_WRAPPER(HSVToRGB)
COLORSPACE_FUNCTION_WRAPPER(RGBToHSL)
COLORSPACE_FUNCTION_WRAPPER(HSLToRGB)

Color3 Color3::xyYToXYZ() const
{
    Color3 ret;
    ::xyYToXZ(&ret[0], &ret[2], r, g, b);
	ret.g = g;
    return ret;
}
Color3 Color3::XYZToxyY() const
{
    Color3 ret;
    ::XYZToxy(&ret[0], &ret[1], r, g, b);
	ret.b = b;
    return ret;
}
Color3 Color3::HSIAdjust(float h, float s, float i) const
{
    Color3 ret(r,g,b);
    ::HSIAdjust(&ret[0], &ret[1], &ret[2], h, s, i);
    return ret;
}
Color3 Color3::HSLAdjust(float h, float s, float l) const
{
    Color3 ret(r,g,b);
    ::HSLAdjust(&ret[0], &ret[1], &ret[2], h, s, l);
    return ret;
}
Color3 Color3::convert(EColorSpace dst, EColorSpace src) const
{
	Color3 ret;
	convertColorSpace(dst, &ret[0], &ret[1], &ret[2], src, r, g, b);
	return ret;
}