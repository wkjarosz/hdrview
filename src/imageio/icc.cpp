#include "icc.h"
#include "scheduler.h"
#include <spdlog/spdlog.h>
#include <vector>

using std::string;
using std::vector;

#ifdef HDRVIEW_ENABLE_LCMS2
#include <lcms2.h>
#include <memory>

//
// Some minimal wrappers around just the Little CMS 2 functionality we need
//

namespace icc
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

Profile open_profile_from_mem(const std::vector<uint8_t> &icc_profile)
{
    return Profile{cmsOpenProfileFromMem(icc_profile.data(), icc_profile.size())};
}

Profile create_linear_RGB_profile(const cmsCIExyY &whitepoint, const cmsCIExyYTRIPLE &primaries)
{
    // Create linear transfer curves
    ToneCurve linear_curve(cmsBuildGamma(nullptr, 1.0));
    if (!linear_curve)
    {
        spdlog::error("Failed to create linear tone curve.");
        return nullptr;
    }

    cmsToneCurve *linear_curves[3] = {linear_curve.get(), linear_curve.get(), linear_curve.get()};
    return Profile{cmsCreateRGBProfile(&whitepoint, &primaries, linear_curves)};
}

Profile create_linear_sRGB_profile()
{
    cmsCIExyY       D65             = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE Rec709Primaries = {{0.6400, 0.3300, 1.0}, {0.3000, 0.6000, 1.0}, {0.1500, 0.0600, 1.0}};
    return create_linear_RGB_profile(D65, Rec709Primaries);
}

// Returns white point that was specified when creating the profile.
// NOTE: we can't just use cmsSigMediaWhitePointTag because its interpretation differs between ICC versions.
cmsCIEXYZ unadapted_white(const Profile &profile)
{
    auto white_point = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(profile.get(), cmsSigMediaWhitePointTag));
    if (white_point && !cmsReadTag(profile.get(), cmsSigChromaticAdaptationTag))
    {
        // No chromatic adaptation matrix: the white point is already unadapted.
        return *white_point;
    }

    cmsCIEXYZ XYZ = {1.0, 1.0, 1.0};
    Profile   profile_xyz{cmsCreateXYZProfile()};
    if (!profile_xyz)
        return XYZ;

    // Array arguments are one per profile.
    cmsHPROFILE profiles[2] = {profile.get(), profile_xyz.get()};
    // Leave white point unchanged - that is what we're trying to extract.
    cmsUInt32Number  intents[2]            = {INTENT_ABSOLUTE_COLORIMETRIC, INTENT_ABSOLUTE_COLORIMETRIC};
    cmsBool          black_compensation[2] = {0, 0};
    cmsFloat64Number adaption[2]           = {0.0, 0.0};
    // Only transforming a single pixel, so skip expensive optimizations.
    cmsUInt32Number flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC;
    Transform xform{cmsCreateExtendedTransform(nullptr, 2, profiles, black_compensation, intents, adaption, nullptr, 0,
                                               TYPE_RGB_DBL, TYPE_XYZ_DBL, flags)};
    if (!xform)
        return XYZ;

    // xy are relative, so magnitude does not matter if we ignore output Y.
    const cmsFloat64Number in[3] = {1.0, 1.0, 1.0};
    cmsDoTransform(xform.get(), in, &XYZ.X, 1);
    return XYZ;
}

bool extract_chromaticities(const Profile &profile, cmsCIExyYTRIPLE *primaries, cmsCIExyY *whitepoint)
{
    // This code is adapted from libjxl
    // Copyright (c) the JPEG XL Project Authors. All rights reserved.
    //
    // Use of this source code is governed by a BSD-style
    // license that can be found in the LICENSE file.

    // These were adapted to the profile illuminant before storing in the profile.
    const cmsCIEXYZ *adapted_r = nullptr, *adapted_g = nullptr, *adapted_b = nullptr;
    adapted_r = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(profile.get(), cmsSigRedColorantTag));
    adapted_g = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(profile.get(), cmsSigGreenColorantTag));
    adapted_b = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(profile.get(), cmsSigBlueColorantTag));

    cmsCIEXYZ converted_rgb[3];
    if (!adapted_r || !adapted_g || !adapted_b)
    {
        // No colorant tag, determine the XYZ coordinates of the primaries by converting from the colorspace.
        // According to the LCMS2 author (https://sourceforge.net/p/lcms/mailman/message/58730697/)
        // This is the correct way to deduce the chromaticities of an ICC profile
        Profile profile_xyz{cmsCreateXYZProfile()};
        if (!profile_xyz)
            return false;

        // Array arguments are one per profile.
        cmsHPROFILE      profiles[2]           = {profile.get(), profile_xyz.get()};
        cmsUInt32Number  intents[2]            = {INTENT_ABSOLUTE_COLORIMETRIC, INTENT_ABSOLUTE_COLORIMETRIC};
        cmsBool          black_compensation[2] = {0, 0};
        cmsFloat64Number adaption[2]           = {0.0, 0.0};
        // Only transforming three pixels, so skip expensive optimizations.
        cmsUInt32Number flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC;
        Transform xform{cmsCreateExtendedTransform(nullptr, 2, profiles, black_compensation, intents, adaption, nullptr,
                                                   0, TYPE_RGB_DBL, TYPE_XYZ_DBL, flags)};
        if (!xform)
            return false;

        const cmsFloat64Number in[9] = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
        cmsDoTransform(xform.get(), in, &converted_rgb->X, 3);
        adapted_r = &converted_rgb[0];
        adapted_g = &converted_rgb[1];
        adapted_b = &converted_rgb[2];
    }

    // Undo the chromatic adaptation.
    const cmsCIEXYZ d50 = {0.96420288, 1.0, 0.82490540};

    auto wp_unadapted = unadapted_white(profile);

    cmsCIEXYZ r, g, b;
    cmsAdaptToIlluminant(&r, &d50, &wp_unadapted, adapted_r);
    cmsAdaptToIlluminant(&g, &d50, &wp_unadapted, adapted_g);
    cmsAdaptToIlluminant(&b, &d50, &wp_unadapted, adapted_b);

    // Convert to xyY
    if (primaries)
    {
        cmsXYZ2xyY(&primaries->Red, &r);
        cmsXYZ2xyY(&primaries->Green, &g);
        cmsXYZ2xyY(&primaries->Blue, &b);
    }
    if (whitepoint)
        cmsXYZ2xyY(whitepoint, &wp_unadapted);
    return true;
}

string profile_description(const Profile &profile)
{
    if (auto desc = reinterpret_cast<const cmsMLU *>(cmsReadTag(profile.get(), cmsSigProfileDescriptionTag)))
    {
        int               size = cmsMLUgetASCII(desc, "en", "US", nullptr, 0);
        std::vector<char> desc_str(size);
        cmsMLUgetASCII(desc, "en", "US", desc_str.data(), desc_str.size());
        return string(desc_str.data());
    }
    return "";
}

bool linearize_colors(float *pixels, int3 size, const vector<uint8_t> &icc_profile, string *tf_description, float2 *red,
                      float2 *green, float2 *blue, float2 *white)
{
    if (icc_profile.empty())
        return false;

    auto            profile_in = icc::open_profile_from_mem(icc_profile);
    cmsCIExyY       whitepoint;
    cmsCIExyYTRIPLE primaries;
    icc::extract_chromaticities(profile_in, &primaries, &whitepoint);
    if (red)
        *red = float2(primaries.Red.x, primaries.Red.y);
    if (green)
        *green = float2(primaries.Green.x, primaries.Green.y);
    if (blue)
        *blue = float2(primaries.Blue.x, primaries.Blue.y);
    if (white)
        *white = float2(whitepoint.x, whitepoint.y);

    auto            profile_out = icc::create_linear_RGB_profile(whitepoint, primaries);
    cmsUInt32Number format      = TYPE_GRAY_FLT;
    if (size.z == 3)
        format = TYPE_RGB_FLT;
    else if (size.z == 4)
        format = TYPE_RGBA_FLT;
    auto flags = (size.z == 4 || size.z == 2 ? cmsFLAGS_COPY_ALPHA : 0) | cmsFLAGS_HIGHRESPRECALC | cmsFLAGS_NOCACHE;
    auto xform = icc::Transform{
        cmsCreateTransform(profile_in.get(), format, profile_out.get(), format, INTENT_ABSOLUTE_COLORIMETRIC, flags)};
    if (!xform)
    {
        spdlog::error("Could not create ICC color transform.");
        return false;
    }
    else
    {
        auto desc = icc::profile_description(profile_in);
        parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                     [xf = xform.get(), pixels, size](int start, int end, int unit_index, int thread_index)
                     {
                         auto data_p = pixels + start * size.z;
                         cmsDoTransform(xf, data_p, data_p, (end - start));
                     });
        if (tf_description)
            *tf_description = fmt::format("ICC profile{}", desc.empty() ? "" : " (" + desc + ")");
    }
    return true;
}

} // namespace icc

#else

namespace icc
{

bool linearize_colors(float *pixels, int3 size, const vector<uint8_t> &icc_profile, string *tf_description, float2 *red,
                      float2 *green, float2 *blue, float2 *white)
{
    return false;
}

} // namespace icc

#endif // HDRVIEW_ENABLE_LCMS2