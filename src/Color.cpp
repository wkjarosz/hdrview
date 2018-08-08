//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "Color.h"
#include "Colorspace.h"


Color3 Color3::LinearSRGBToXYZ() const
{
    Color3 ret;
    ::LinearSRGBToXYZ(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::XYZToLinearSRGB() const
{
    Color3 ret;
    ::XYZToLinearSRGB(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::XYZToLab() const
{
    Color3 ret;
    ::XYZToLab(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::LabToXYZ() const
{
    Color3 ret;
    ::LabToXYZ(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::XYZToLuv() const
{
    Color3 ret;
    ::XYZToLuv(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::LuvToXYZ() const
{
    Color3 ret;
    ::LuvToXYZ(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
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
Color3 Color3::RGBToHSV() const
{
    Color3 ret;
    ::RGBToHSV(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::HSVToRGB() const
{
    Color3 ret;
    ::HSVToRGB(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::RGBToHLS() const
{
    Color3 ret;
    ::RGBToHSL(&ret[0], &ret[1], &ret[2], r, g, b);
    return ret;
}
Color3 Color3::HLSToRGB() const
{
    Color3 ret;
    ::HSLToRGB(&ret[0], &ret[1], &ret[2], r, g, b);
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