//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once
#ifdef HDRVIEW_ENABLE_LCMS2
#include <lcms2.h>
#include <memory>
#include <string>

//
// Some minimal wrappers around just the Little CMS 2 functionality we need
//

namespace cms
{

// Custom deleters for unique_ptr
struct ProfileDeleter
{
    void operator()(cmsHPROFILE p) const { cmsCloseProfile(p); }
};

struct TransformDeleter
{
    void operator()(cmsHTRANSFORM t) const { cmsDeleteTransform(t); }
};

struct ToneCurveDeleter
{
    void operator()(cmsToneCurve *t) const { cmsFreeToneCurve(t); }
};

// Safe auto-freeing wrappers to LCMS2's opaque types
using Profile   = std::unique_ptr<std::remove_pointer_t<cmsHPROFILE>, ProfileDeleter>;
using Transform = std::unique_ptr<std::remove_pointer_t<cmsHTRANSFORM>, TransformDeleter>;
using ToneCurve = std::unique_ptr<cmsToneCurve, ToneCurveDeleter>;

Profile open_profile_from_mem(const std::vector<uint8_t> &icc_profile);

Profile create_linear_RGB_profile(const cmsCIExyY &whitepoint, const cmsCIExyYTRIPLE &primaries);

Profile create_linear_sRGB_profile();

bool extract_chromaticities(const Profile &profile, cmsCIExyYTRIPLE &primaries, cmsCIExyY &whitepoint);

std::string profile_description(const Profile &profile);

} // namespace cms

#endif // HDRVIEW_ENABLE_LCMS2