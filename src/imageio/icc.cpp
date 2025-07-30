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
    return Profile{cmsOpenProfileFromMem(reinterpret_cast<const void *>(icc_profile.data()),
                                         static_cast<cmsUInt32Number>(icc_profile.size()))};
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
    static const cmsCIExyY       D65            = {0.3127, 0.3290, 1.0};
    static const cmsCIExyYTRIPLE sRGB_primaries = {{0.6400, 0.3300, 1.0}, {0.3000, 0.6000, 1.0}, {0.1500, 0.0600, 1.0}};
    return create_linear_RGB_profile(D65, sRGB_primaries);
}

// Returns white point that was specified when creating the profile.
// NOTE: we can't just use cmsSigMediaWhitePointTag because its interpretation differs between ICC versions.
cmsCIEXYZ unadapted_white(const Profile &profile)
{
    // This code is adapted from the UnadaptedWhitePoint function in libjxl
    // Copyright (c) the JPEG XL Project Authors. All rights reserved.
    //
    // Use of this source code is governed by a BSD-style
    // license that can be found in the LICENSE file.

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
    // This code is adapted from the IdentifyPrimaries function in libjxl
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
    auto d50 = cmsD50_XYZ();

    auto wp_unadapted = unadapted_white(profile);

    cmsCIEXYZ r, g, b;
    cmsAdaptToIlluminant(&r, d50, &wp_unadapted, adapted_r);
    cmsAdaptToIlluminant(&g, d50, &wp_unadapted, adapted_g);
    cmsAdaptToIlluminant(&b, d50, &wp_unadapted, adapted_b);

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
        auto              size = cmsMLUgetASCII(desc, "en", "US", nullptr, 0);
        std::vector<char> desc_str((size_t)size);
        cmsMLUgetASCII(desc, "en", "US", desc_str.data(), (cmsUInt32Number)desc_str.size());
        return string(desc_str.data());
    }
    return "";
}

bool is_cmyk(const vector<uint8_t> &icc_profile)
{
    if (icc_profile.empty())
        return false;

    auto profile = open_profile_from_mem(icc_profile);
    if (!profile)
    {
        spdlog::error("Could not open ICC profile from memory.");
        return false;
    }

    cmsColorSpaceSignature color_space = cmsGetColorSpace(profile.get());
    return (color_space == cmsSigCmykData);
}

bool linearize_colors(float *pixels, int3 size, const vector<uint8_t> &icc_profile, string *tf_description,
                      Chromaticities *c)
{
    if (icc_profile.empty())
        return false;

    auto profile_in = icc::open_profile_from_mem(icc_profile);
    if (!profile_in)
    {
        spdlog::error("Could not open ICC profile from memory.");
        return false;
    }

    // Detect profile color space
    cmsColorSpaceSignature color_space = cmsGetColorSpace(profile_in.get());
    bool                   is_cmyk     = (color_space == cmsSigCmykData);
    bool                   is_cmy      = (color_space == cmsSigCmyData);
    bool                   is_rgb      = (color_space == cmsSigRgbData);
    bool                   is_gray     = (color_space == cmsSigGrayData);

    spdlog::debug("ICC profile color space: {}\n\tCMYK: {}\n\tRGB: {}\n\tGray: {}\n\tCMY: {}", (int)color_space,
                  is_cmyk, is_rgb, is_gray, is_cmy);

    cmsUInt32Number format_in = TYPE_GRAY_FLT, format_out = TYPE_GRAY_FLT;
    if (is_rgb)
    {
        if (size.z == 3)
            format_in = format_out = TYPE_RGB_FLT;
        else if (size.z == 4)
            format_in = format_out = TYPE_RGBA_FLT;
        else
            format_in = format_out = TYPE_GRAY_FLT;
    }
    else if (is_cmyk)
    {
        format_in  = TYPE_CMYK_FLT;
        format_out = TYPE_RGBA_FLT;
        if (size.z != 4)
        {
            spdlog::error("CMYK profile expects 4 channels, but got {}.", size.z);
            return false;
        }
    }

    if (!is_rgb && !is_cmyk && !is_gray)
    {
        spdlog::error("Unsupported ICC profile color space: {}", (int)color_space);
        return false;
    }

    // If CMYK, lcms expects floating-point values in the range [0, 100]
    if (is_cmyk)
        for (int i = 0; i < size.x * size.y * size.z; ++i) pixels[i] = (1.0f - pixels[i]) * 100.0f;

    // Extract chromaticities/whitepoint for RGB, or set defaults for CMYK
    cmsCIExyY       whitepoint = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries  = {{0.6400, 0.3300, 1.0}, {0.3000, 0.6000, 1.0}, {0.1500, 0.0600, 1.0}};
    if (is_rgb)
    {
        if (!extract_chromaticities(profile_in, &primaries, &whitepoint))
        {
            spdlog::warn("Could not extract chromaticities from ICC profile, using sRGB defaults");
        }
    }

    if (c)
    {
        c->red   = float2((float)primaries.Red.x, (float)primaries.Red.y);
        c->green = float2((float)primaries.Green.x, (float)primaries.Green.y);
        c->blue  = float2((float)primaries.Blue.x, (float)primaries.Blue.y);
        c->white = float2((float)whitepoint.x, (float)whitepoint.y);
    }

    // Create a linear output profile matching the input color space
    icc::Profile profile_out = icc::create_linear_RGB_profile(whitepoint, primaries);

    if (!profile_out)
    {
        spdlog::error("Failed to create profile.");
        return false;
    }

    auto flags = (((size.z == 4 || size.z == 2) && !is_cmyk) ? cmsFLAGS_COPY_ALPHA : 0) | cmsFLAGS_HIGHRESPRECALC |
                 cmsFLAGS_NOCACHE;
    if (auto xform =
            icc::Transform{cmsCreateTransform(profile_in.get(), format_in, profile_out.get(), format_out,
                                              is_cmyk ? INTENT_PERCEPTUAL : INTENT_ABSOLUTE_COLORIMETRIC, flags)})
    {
        auto desc = icc::profile_description(profile_in);
        parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                     [xf = xform.get(), pixels, size, is_cmyk](int start, int end, int, int)
                     {
                         auto data_p = pixels + start * size.z;
                         cmsDoTransform(xf, data_p, data_p, (end - start));
                         if (is_cmyk)
                             for (int i = start; i < end; ++i) pixels[i * size.z + 3] = 1.f;
                     });
        if (tf_description)
            *tf_description = fmt::format("ICC profile{}", desc.empty() ? "" : " (" + desc + ")");

        return true;
    }

    spdlog::error("Could not create ICC color transform.");
    return false;
}

} // namespace icc

#else

namespace icc
{

bool linearize_colors(float *pixels, int3 size, const vector<uint8_t> &icc_profile, string *tf_description,
                      Chromaticities *chr)
{
    return false;
}

} // namespace icc

#endif // HDRVIEW_ENABLE_LCMS2