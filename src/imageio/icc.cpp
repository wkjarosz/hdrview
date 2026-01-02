#include "icc.h"
#include "colorspace.h"
#include "smallthreadpool.h"
#include <spdlog/spdlog.h>
#include <vector>

using std::string;
using std::string_view;
using std::vector;
using namespace stp;

#if HDRVIEW_ENABLE_LCMS2
#include <lcms2.h>

//
// Some minimal wrappers around just the Little CMS 2 functionality we need
//

// Custom deleters for unique_ptr
struct TransformDeleter
{
    void operator()(cmsHTRANSFORM t) const { cmsDeleteTransform(t); }
};

struct ToneCurveDeleter
{
    void operator()(cmsToneCurve *t) const { cmsFreeToneCurve(t); }
};

// Safe auto-freeing wrappers to LCMS2's opaque types
using Transform = std::unique_ptr<std::remove_pointer_t<cmsHTRANSFORM>, TransformDeleter>;
using ToneCurve = std::unique_ptr<cmsToneCurve, ToneCurveDeleter>;

class CmsErrorHandler
{
public:
    CmsErrorHandler()
    {
        cmsSetLogErrorHandler([](cmsContext, cmsUInt32Number errorCode, const char *message)
                              { spdlog::error("lcms error #{}: {}", errorCode, message); });
    }
};

class CmsContext
{
public:
    static const CmsContext &thread_local_instance()
    {
        static thread_local CmsContext c;
        return c;
    }

    cmsContext get() const { return m_ctx; }

private:
    CmsContext()
    {
        static CmsErrorHandler ehandler;

        m_ctx = cmsCreateContext(nullptr, nullptr);
        if (!m_ctx)
            throw std::runtime_error{"Failed to create LCMS context."};
    }

    ~CmsContext()
    {
        if (m_ctx)
            cmsDeleteContext(m_ctx);
    }

    cmsContext m_ctx = nullptr;
};

ICCProfile::ICCProfile(const uint8_t *icc_profile, size_t icc_profile_size) :
    m_profile(cmsOpenProfileFromMemTHR(CmsContext::thread_local_instance().get(), (const void *)icc_profile,
                                       (cmsUInt32Number)icc_profile_size))
{
    // empty
}

ICCProfile::~ICCProfile()
{
    if (m_profile)
        cmsCloseProfile(static_cast<cmsHPROFILE>(m_profile));
}

ICCProfile ICCProfile::linear_RGB(const Chromaticities &chr)
{
    cmsCIExyY       whitepoint = {chr.white.x, chr.white.y, 1.0};
    cmsCIExyYTRIPLE primaries  = {
        {chr.red.x, chr.red.y, 1.0}, {chr.green.x, chr.green.y, 1.0}, {chr.blue.x, chr.blue.y, 1.0}};

    // Create linear transfer curves
    ToneCurve linear_curve(cmsBuildGamma(CmsContext::thread_local_instance().get(), 1.0));
    if (!linear_curve)
    {
        spdlog::error("Failed to create linear tone curve.");
        return nullptr;
    }

    cmsToneCurve *linear_curves[3] = {linear_curve.get(), linear_curve.get(), linear_curve.get()};
    return ICCProfile{
        cmsCreateRGBProfileTHR(CmsContext::thread_local_instance().get(), &whitepoint, &primaries, linear_curves)};
}

ICCProfile ICCProfile::linear_Gray(const float2 &whitepoint)
{
    cmsCIExyY wp = {whitepoint.x, whitepoint.y, 1.0};
    // Create linear transfer curves
    ToneCurve linear_curve(cmsBuildGamma(CmsContext::thread_local_instance().get(), 1.0));
    if (!linear_curve)
    {
        spdlog::error("Failed to create linear tone curve.");
        return nullptr;
    }

    return ICCProfile{cmsCreateGrayProfileTHR(CmsContext::thread_local_instance().get(), &wp, linear_curve.get())};
}

// NOTE: we can't just use cmsSigMediaWhitePointTag because its interpretation differs between ICC versions.
// The white-point extraction logic is adapted from the UnadaptedWhitePoint function in libjxl.
bool ICCProfile::extract_chromaticities(Chromaticities *c) const
{
    // This code is adapted from the IdentifyPrimaries function in libjxl
    // Copyright (c) the JPEG XL Project Authors. All rights reserved.
    //
    // Use of this source code is governed by a BSD-style
    // license that can be found in the LICENSE file.

    // Read white point / chromatic adaptation tags so we can decide whether
    // we need to build an XYZ profile + transform. We cannot rely solely on
    // cmsSigMediaWhitePointTag because its interpretation differs between ICC
    // versions; when a chromatic adaptation tag is present we must transform
    // through the profile to obtain the unadapted white point.

    // These were adapted to the profile illuminant before storing in the profile.
    const cmsCIEXYZ *adapted_r = nullptr, *adapted_g = nullptr, *adapted_b = nullptr, *white_point_tag = nullptr;
    adapted_r                       = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(get(), cmsSigRedColorantTag));
    adapted_g                       = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(get(), cmsSigGreenColorantTag));
    adapted_b                       = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(get(), cmsSigBlueColorantTag));
    white_point_tag                 = reinterpret_cast<const cmsCIEXYZ *>(cmsReadTag(get(), cmsSigMediaWhitePointTag));
    const void *chromatic_adapt_tag = cmsReadTag(get(), cmsSigChromaticAdaptationTag);

    cmsCIEXYZ converted_rgb[3];

    // Determine whether we need a transform: if the profile doesn't include
    // colorant tags (so we must derive primaries by transforming) or if the
    // white point needs to be obtained via a transform (no direct unadapted
    // tag present), create the XYZ profile and transform once and reuse it.
    bool need_unadapted_wp = !(white_point_tag && !chromatic_adapt_tag);
    bool need_transform    = (!adapted_r || !adapted_g || !adapted_b) || need_unadapted_wp;

    ICCProfile profile_xyz{nullptr};
    Transform  xform;
    if (need_transform)
    {
        profile_xyz = ICCProfile{cmsCreateXYZProfileTHR(CmsContext::thread_local_instance().get())};
        if (profile_xyz.valid())
        {
            // Array arguments are one per profile.
            cmsHPROFILE      profiles[2]           = {get(), profile_xyz.get()};
            cmsUInt32Number  intents[2]            = {INTENT_ABSOLUTE_COLORIMETRIC, INTENT_ABSOLUTE_COLORIMETRIC};
            cmsBool          black_compensation[2] = {0, 0};
            cmsFloat64Number adaption[2]           = {0.0, 0.0};
            // Only transforming a small number of pixels, so skip expensive optimizations.
            cmsUInt32Number flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC;
            xform = Transform{cmsCreateExtendedTransform(CmsContext::thread_local_instance().get(), 2, profiles,
                                                         black_compensation, intents, adaption, nullptr, 0,
                                                         TYPE_RGB_DBL, TYPE_XYZ_DBL, flags)};
        }
    }

    if (!adapted_r || !adapted_g || !adapted_b)
    {
        // No colorant tag, determine the XYZ coordinates of the primaries by converting from the colorspace.
        // According to the LCMS2 author (https://sourceforge.net/p/lcms/mailman/message/58730697/)
        // This is the correct way to deduce the chromaticities of an ICC profile
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

    // Compute the unadapted white point. If the profile provides a MediaWhitePoint
    // and there is no chromatic adaptation tag, use it directly. Otherwise fall
    // back to transforming an RGB white through the profile into XYZ. If we
    // couldn't create the transform, use a safe default of {1,1,1}.
    cmsCIEXYZ wp_unadapted = {1.0, 1.0, 1.0};
    if (!need_unadapted_wp)
    {
        // No chromatic adaptation matrix: the white point is already unadapted.
        wp_unadapted = *white_point_tag;
    }
    else if (xform)
    {
        const cmsFloat64Number in_white[3] = {1.0, 1.0, 1.0};
        cmsDoTransform(xform.get(), in_white, &wp_unadapted.X, 1);
    }

    cmsCIEXYZ r, g, b;
    cmsAdaptToIlluminant(&r, d50, &wp_unadapted, adapted_r);
    cmsAdaptToIlluminant(&g, d50, &wp_unadapted, adapted_g);
    cmsAdaptToIlluminant(&b, d50, &wp_unadapted, adapted_b);

    // Convert to xyY
    cmsCIExyYTRIPLE primaries;
    cmsXYZ2xyY(&primaries.Red, &r);
    cmsXYZ2xyY(&primaries.Green, &g);
    cmsXYZ2xyY(&primaries.Blue, &b);
    cmsCIExyY whitepoint;
    cmsXYZ2xyY(&whitepoint, &wp_unadapted);

    *c = {{(float)primaries.Red.x, (float)primaries.Red.y},
          {(float)primaries.Green.x, (float)primaries.Green.y},
          {(float)primaries.Blue.x, (float)primaries.Blue.y},
          {(float)whitepoint.x, (float)whitepoint.y}};

    return true;
}
int ICCProfile::lcms_version() { return cmsGetEncodedCMMversion(); }

string ICCProfile::description() const
{
    if (!valid())
    {
        spdlog::error("Could not open ICC profile from memory.");
        return "Invalid, ICC";
    }

    if (auto desc = reinterpret_cast<const cmsMLU *>(cmsReadTag(get(), cmsSigProfileDescriptionTag)))
    {
        auto              size = cmsMLUgetASCII(desc, "en", "US", nullptr, 0);
        std::vector<char> desc_str((size_t)size);
        cmsMLUgetASCII(desc, "en", "US", desc_str.data(), (cmsUInt32Number)desc_str.size());
        return fmt::format("{}, ICC", string(desc_str.data()));
    }
    return "Unknown description, ICC";
}

bool ICCProfile::is_CMYK() const
{
    if (!valid())
        return false;

    return (cmsGetColorSpace(get()) == cmsSigCmykData);
}
bool ICCProfile::is_RGB() const
{
    if (!valid())
        return false;

    return (cmsGetColorSpace(get()) == cmsSigRgbData);
}
bool ICCProfile::is_Gray() const
{
    if (!valid())
        return false;

    return (cmsGetColorSpace(get()) == cmsSigGrayData);
}

ICCProfile ICCProfile::linearized_profile(Chromaticities *c) const
{
    if (!valid())
        return nullptr;

    Chromaticities chr;
    if (!extract_chromaticities(&chr))
    {
        spdlog::error("Could not extract chromaticities from ICC profile.");
        return nullptr;
    }

    if (c)
        *c = chr;

    if (is_Gray())
        return ICCProfile::linear_Gray(chr.white);
    else
        return ICCProfile::linear_RGB(chr);
}

bool ICCProfile::transform_pixels(float *pixels, int3 size, const ICCProfile &profile_in, const ICCProfile &profile_out)
{
    if (!profile_in || !profile_out)
        return false;

    // Detect profile color space
    bool is_cmyk = profile_in.is_CMYK() && profile_out.is_RGB();
    bool is_rgb  = profile_in.is_RGB() && profile_out.is_RGB();
    bool is_gray = profile_in.is_Gray() && profile_out.is_Gray();

    spdlog::debug("ICC profile color space:\n\tCMYK: {}\n\tRGB: {}\n\tGray: {}", is_cmyk, is_rgb, is_gray);

    if (!is_rgb && !is_cmyk && !is_gray)
    {
        spdlog::error("Unsupported ICC profile color space");
        return false;
    }

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

    // If CMYK, lcms expects floating-point values in the range [0, 100]
    if (is_cmyk)
        for (int i = 0; i < size.x * size.y * size.z; ++i) pixels[i] = (1.0f - pixels[i]) * 100.0f;

    auto flags = (((size.z == 4 || size.z == 2) && !is_cmyk) ? cmsFLAGS_COPY_ALPHA : 0) | cmsFLAGS_HIGHRESPRECALC |
                 cmsFLAGS_NOCACHE;
    if (auto xform = Transform{cmsCreateTransformTHR(
            CmsContext::thread_local_instance().get(), profile_in.get(), format_in, profile_out.get(), format_out,
            is_cmyk ? INTENT_PERCEPTUAL : INTENT_ABSOLUTE_COLORIMETRIC, flags)})

    {
        parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                     [xf = xform.get(), pixels, size, is_cmyk](int start, int end, int, int)
                     {
                         auto data_p = pixels + start * size.z;
                         cmsDoTransform(xf, data_p, data_p, (end - start));
                         if (is_cmyk)
                             for (int i = start; i < end; ++i) pixels[i * size.z + 3] = 1.f;
                     });

        return true;
    }

    spdlog::error("Could not create ICC color transform.");
    return false;
}

bool ICCProfile::linearize_pixels(float *pixels, int3 size, bool keep_primaries, string *tf_description,
                                  Chromaticities *c) const
{
    ICCProfile profile_out = nullptr;
    // create the output profile and store either the input or output primaries in c
    if (keep_primaries)
        profile_out = linearized_profile(c);
    else
    {
        profile_out = (is_Gray() ? ICCProfile::linear_Gray() : ICCProfile::linear_RGB());
        if (c)
            *c = Chromaticities();
    }

    if (transform_pixels(pixels, size, *this, profile_out))
    {
        if (tf_description)
            *tf_description = description();

        return true;
    }
    return false;
}

std::vector<uint8_t> ICCProfile::dump_to_memory() const
{
    if (!valid())
        return {};

    std::vector<uint8_t> data;
    cmsUInt32Number      size;
    cmsSaveProfileToMem(get(), nullptr, &size);
    data.resize(size);
    if (cmsSaveProfileToMem(get(), reinterpret_cast<void *>(data.data()), &size))
        return data;

    return {};
}

#else

// Stubs for builds without LCMS2 which just return failure for operations that require LCMS functionality.

ICCProfile::ICCProfile(const uint8_t * /*icc_profile*/, size_t /*icc_profile_size*/) : m_profile(nullptr) {}
ICCProfile::~ICCProfile() {}
ICCProfile           ICCProfile::linear_RGB(const Chromaticities           &/*chr*/) { return ICCProfile(nullptr); }
ICCProfile           ICCProfile::linear_Gray(const float2           &/*whitepoint*/) { return ICCProfile(nullptr); }
ICCProfile           ICCProfile::linearized_profile(Chromaticities           */*c*/) const { return ICCProfile(nullptr); }
int                  ICCProfile::lcms_version() { return 0; }
std::string          ICCProfile::description() const { return std::string(); }
bool                 ICCProfile::extract_chromaticities(Chromaticities                 */*c*/) const { return false; }
std::vector<uint8_t> ICCProfile::dump_to_memory() const { return {}; }
bool                 ICCProfile::is_CMYK() const { return false; }
bool                 ICCProfile::is_RGB() const { return false; }
bool                 ICCProfile::is_Gray() const { return false; }

bool ICCProfile::transform_pixels(float * /*pixels*/, int3 /*size*/, const ICCProfile & /*profile_in*/,
                                  const ICCProfile & /*profile_out*/)
{
    return false;
}
bool ICCProfile::linearize_pixels(float * /*pixels*/, int3 /*size*/, bool /*keep_primaries*/,
                                  std::string * /*tf_description*/, Chromaticities * /*c*/) const
{
    return false;
}

#endif // HDRVIEW_ENABLE_LCMS2

// --- CICPProfile implementation -------------------------------------------------

// version of linearize_pixels that uses chromaticities and transfer function (instead of ICC or CICP profiles)
bool linearize_pixels(float *pixels, int3 size, const Chromaticities &src_chroma, const TransferFunction &tf,
                      bool keep_primaries, std::string *profile_description, Chromaticities *c,
                      AdaptationMethod CAT_method)
{
    if (profile_description)
        *profile_description = color_profile_name(src_chroma, tf);

    // Apply inverse transfer (encode -> linear)
    to_linear(pixels, size, tf);

    if (!src_chroma.valid())
    {
        spdlog::debug("linearize_pixels: invalid color primaries");
        return true;
    }

    if (!keep_primaries)
    {
        spdlog::debug("linearize_pixels: converting to Rec.709/sRGB primaries.");
        auto dst_chroma = Chromaticities{}; // Rec.709/sRGB
        if (c)
            *c = dst_chroma;
        convert_primaries(pixels, size, src_chroma, dst_chroma, CAT_method);
    }
    else
    {
        spdlog::debug("linearize_pixels: keeping original primaries.");
        // keep primaries; provide chromaticities if requested
        if (c)
            *c = src_chroma;
    }

    return true;
}

// Linearize CICP-encoded pixels in-place. Uses colorspace helpers for transfer
// functions and chromaticity conversions.
bool CICPProfile::linearize_pixels(float *pixels, int3 size, bool keep_primaries, std::string *profile_description,
                                   Chromaticities *c, AdaptationMethod CAT_method) const
{
    // Determine transfer function and chromaticities
    auto tf         = transfer_function_from_CICP(tc());
    auto src_chroma = chromaticities_from_CICP(cp());

    if (profile_description)
        *profile_description = short_name();

    return ::linearize_pixels(pixels, size, src_chroma, tf, keep_primaries, nullptr, c, CAT_method);
}

// Transform pixels from one CICP profile to another in-place.
bool CICPProfile::transform_pixels(float *pixels, int3 size, const CICPProfile &profile_in,
                                   const CICPProfile &profile_out, AdaptationMethod CAT_method)
{
    if (!pixels)
        return false;

    auto tf_in  = transfer_function_from_CICP(profile_in.tc());
    auto tf_out = transfer_function_from_CICP(profile_out.tc());

    // Handle grayscale (1 channel) separately
    if (size.z < 3)
    {
        // inverse transfer of input -> linear
        to_linear(pixels, size, tf_in);
        // forward transfer of output
        from_linear(pixels, size, tf_out);
        return true;
    }

    // RGB path

    // inverse transfer
    to_linear(pixels, size, tf_in);

    // primary conversion if needed
    Chromaticities src_chroma{chromaticities_from_CICP(profile_in.cp())},
        dst_chroma{chromaticities_from_CICP(profile_out.cp())};

    if (src_chroma.valid() && dst_chroma.valid())
        convert_primaries(pixels, size, src_chroma, dst_chroma, CAT_method);

    // forward transfer of output
    from_linear(pixels, size, tf_out);
    return true;
}

// --- CICPProfile helper implementations -------------------------------------

bool CICPProfile::valid_cp() const { return chromaticities_from_CICP(cp()).valid(); }
bool CICPProfile::valid_tc() const { return transfer_function().type != TransferFunction::Invalid; }
bool CICPProfile::valid_mc() const { return 0 <= mc() && mc() < 18 && mc() != 3; }
bool CICPProfile::valid_fr() const { return fr() == 0 || fr() == 1; }
bool CICPProfile::valid() const { return valid_tc() && valid_cp() && valid_mc() && valid_fr(); }

TransferFunction CICPProfile::transfer_function() const { return transfer_function_from_CICP(tc()); }
Chromaticities   CICPProfile::chromaticities() const { return chromaticities_from_CICP(cp()); }

ColorGamut_ CICPProfile::gamut_enum() const
{
    if (auto chr = chromaticities_from_CICP(cp()); chr.valid())
        return named_color_gamut(chromaticities_from_CICP(cp()));
    else
        return ColorGamut_Unspecified;
}

std::string CICPProfile::cp_long_name() const
{
    if (auto chr = chromaticities_from_CICP(cp()); chr.valid())
        return color_gamut_name(named_color_gamut(chromaticities_from_CICP(cp())));
    else
        return fmt::format("CP={}", cp());
}

std::string CICPProfile::tc_long_name() const
{
    auto tf = transfer_function_from_CICP(tc());
    return transfer_function_name(tf);
}

std::string CICPProfile::mc_long_name() const
{
    if (mc() > 255 || mc() < 0)
        return "Invalid";
    switch (mc())
    {
    case 0: return "Identity";
    case 1: return "BT.709 / BT.1361";
    case 2: return "Unspecified";
    case 3: return "Reserved";
    case 4: return "FCC";
    case 5: return "BT.470 BG / BT.601/625";
    case 6: return "BT.601/525 / NTSC";
    case 7: return "SMPTE 240M";
    case 8: return "YCgCo";
    case 9: return "BT.2020 non-constant luminance";
    case 10: return "BT.2020 constant luminance";
    case 11: return "SMPTE ST 2085";
    case 12: return "Chromaticity-derived non-constant luminance";
    case 13: return "Chromaticity-derived constant luminance";
    case 14: return "BT.2100 ICtCp";
    case 15: return "IPT-PQ-C2";
    case 16: return "YCgCo-Re";
    case 17: return "YCgCo-Ro";
    default: return "Reserved";
    }
}

const char *CICPProfile::cp_short_name() const
{
    if (cp() > 255 || cp() < 0)
        return "Invalid";
    switch (cp())
    {
    case 0: return "Reserved";
    case 1: return "BT709";
    case 4: return "BT470M";
    case 5: return "BT470BG";
    case 6: return "BT601/525";
    case 7: return "SMPTE240M";
    case 8: return "Film";
    case 9: return "BT2020";
    case 10: return "XYZ";
    case 11: return "DCI-P3";
    case 12: return "Display-P3";
    case 22: return "BT601/525Alt";
    default: return "Reserved";
    }
}

const char *CICPProfile::tc_short_name() const
{
    if (tc() > 255 || tc() < 0)
        return "Invalid";
    switch (tc())
    {
    case 0: return "Reserved";
    case 1: return "BT709";
    case 2: return "Unspecified";
    case 3: return "Reserved";
    case 4: return "Gamma2.2";
    case 5: return "Gamma2.8";
    case 6: return "BT601";
    case 7: return "SMPTE240M";
    case 8: return "Linear";
    case 9: return "Log100";
    case 10: return "Log100Sqrt";
    case 11: return "IEC61966";
    case 12: return "BT1361";
    case 13: return "sRGB";
    case 14: return "BT.2020-10bit";
    case 15: return "BT.2020-12bit";
    case 16: return "PQ";
    case 17: return "DCI-P3";
    case 18: return "HLG";
    default: return "Reserved";
    }
}

const char *CICPProfile::mc_short_name() const
{
    if (mc() > 255 || mc() < 0)
        return "Invalid";
    switch (mc())
    {
    case 0: return "RGB";
    case 1: return "BT709";
    case 2: return "Unspecified";
    case 3: return "Reserved";
    case 4: return "FCC";
    case 5: return "BT601";
    case 6: return "BT601";
    case 7: return "SMPTE240M";
    case 8: return "YCgCo";
    case 9: return "BT2020NCL";
    case 10: return "BT2020CL";
    case 11: return "SMPTE2085";
    case 12: return "ChromaNCL";
    case 13: return "ChromaCL";
    case 14: return "ICtCp";
    case 15: return "IPTC2";
    case 16: return "YCgCo-Re";
    case 17: return "YCgCo-Ro";
    default: return "Reserved";
    }
}

std::string CICPProfile::short_name() const
{
    auto   p       = cp();
    auto   t       = tc();
    string details = fmt::format(" {}, CICP {}-{}-{}-{}", r_long_name(), p, t, mc(), fr());
    if (p == 1)
    {
        if (t == 1)
            return fmt::format("Rec.ITU-R BT.709-6{}", details);
        else if (t == 8)
            return fmt::format("linear-light sRGB{}", details);
        else if (t == 13)
            return fmt::format("IEC 61966-2-1 sRGB{}", details);
        else
            return fmt::format("Unknown sRGB-like{}", details);
    }
    else if (p == 5)
    {
        if (t == 4)
            return fmt::format("Rec.ITU-R BT.1700-0 625 (SECAM){}", details);
        else if (t == 6)
            return fmt::format("Rec.ITU-R BT.601-7 625 (PAL){}", details);
        else
            return fmt::format("Unknown 625-line PAL-like{}", details);
    }
    else if (p == 6)
    {
        if (t == 6)
            return fmt::format("Rec.ITU-R BT.601-7 525 (NTSC){}", details);
        else
            return fmt::format("Unknown 525-line NTSC-like{}", details);
    }
    else if (p == 9)
    {
        if (t == 14)
            return fmt::format("Rec.ITU-R BT.2020-2 (10-bit){}", details);
        else if (t == 15)
            return fmt::format("Rec.ITU-R BT.2020-2 (12-bit){}", details);
        else if (t == 16)
            return fmt::format("Rec.ITU-R BT.2100-2 PQ{}", details);
        else if (t == 18)
            return fmt::format("Rec.ITU-R BT.2100-2 HLG{}", details);
        else
            return fmt::format("Unknown Rec2020-like{}", details);
    }
    else if (p == 11)
    {
        if (t == 17)
            return fmt::format("SMPTE RP 431-2 with SMPTE ST 428-1 D-Cinema Distribution Master (DCI-P3){}", details);
        else
            return fmt::format("Unknown DCI-P3-like{}", details);
    }
    else if (p == 12)
    {
        if (t == 13)
            return fmt::format("Display P3{}", details);
        else if (t == 16)
            return fmt::format("P3D65-PQ{}", details);
        else
            return fmt::format("Unknown D65 P3-like{}", details);
    }
    else
        return fmt::format("Unusual/historical{}", details);

    return fmt::format("CICP ({} {} {} {})", cp_short_name(), tc_short_name(), mc_short_name(), r_short_name());
}

CICPProfile CICPProfile::from_gamut_and_transfer(ColorGamut_ gamut, TransferFunction tf, int matrix_coeffs,
                                                 int full_range)
{
    return {chromaticities_to_CICP(gamut_chromaticities(gamut)), transfer_function_to_CICP(tf), matrix_coeffs,
            full_range};
}
