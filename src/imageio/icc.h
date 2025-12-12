//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

#include "colorspace.h"
#include "fwd.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/types.h>

/*!
    \brief Wrapper for an ICC profile providing utility routines used by HDRView.

    ICCProfile is non-copyable, and internally holds an opaque LCMS profile pointer (when built with LCMS2) and exposes
    a small set of convenience methods for creating linear profiles, extracting chromaticities, building linearized
    profiles, and transforming pixel buffers.
 */
class ICCProfile
{
public:
    /**
        \brief Construct an ICCProfile from raw ICC profile data.

        Parses and validates the provided ICC profile byte buffer and initializes
        the ICCProfile instance. The input data is copied into internal storage;
        the caller retains ownership of the provided pointer.

        \param icc_profile Pointer to the ICC profile byte data.
        \param icc_profile_size Size in bytes of the ICC profile data.

        \note If parsing fails, the resulting ICCProfile object will be invalid
              (i.e., `valid()` will return false).
     */
    ICCProfile(const uint8_t *icc_profile, size_t icc_profile_size);
    //! Construct an ICCProfile from a vector containing the ICC profile data. \see ICCProfile(const uint8_t*, size_t)
    ICCProfile(const std::vector<uint8_t> &icc_profile) : ICCProfile(icc_profile.data(), icc_profile.size()) {}
    //! Constructs an null/invalid ICCProfile object.
    ICCProfile() : m_profile{nullptr} {}
    //! Destructor that releases internal ICC profile resources.
    ~ICCProfile();

    // non-copyable
    ICCProfile(const ICCProfile &)            = delete;
    ICCProfile &operator=(const ICCProfile &) = delete;

    // movable (transfer ownership)
    // ColorProfile(ColorProfile &&other) noexcept : m_profile(other.m_profile) { other.m_profile = nullptr; }
    ICCProfile &operator=(ICCProfile &&other) noexcept
    {
        if (this != &other)
        {
            // swap pointers so the moved-from object will clean up the old resource in its dtor
            void *tmp       = m_profile;
            m_profile       = other.m_profile;
            other.m_profile = tmp;
        }
        return *this;
    }

    //! Construct a linear RGB ICC profile with the specified chromaticities (or sRGB/Rec709 primaries if none are
    //! provided).
    static ICCProfile linear_RGB(const Chromaticities &chr = Chromaticities{});
    //! Construct a linear Gray ICC profile with the specified white point (or D65 if none is provided).
    static ICCProfile linear_Gray(const float2 &whitepoint = white_point(WhitePoint_D65));

    //! Check if ICC profile support is available (i.e., built with LCMS2).
    static bool supported();

    //!< Get the version of the linked LCMS2 library (or 0 if not built with LCMS2).
    static int lcms_version();

    /*!
        \brief Create a linearized version of this ICC profile.

        This function creates a new ICC profile that has the same primaries and white point as this profile,
        but with linear transfer functions.

        If this profile is Gray, the returned profile will also be Gray. If this profile is RGB or CMYK, the returned
        profile will be RGB.

        \param[out] c
            If not nullptr, the chromaticities of this profile will be written to these variables.
        \returns
            A new ColorProfile object representing the linearized profile, or nullptr on failure.
    */
    ICCProfile linearized_profile(Chromaticities *c = nullptr) const;

    void    *get() const { return m_profile; }
    bool     valid() const { return m_profile != nullptr; }
    explicit operator bool() const noexcept { return m_profile != nullptr; }

    std::string description() const;
    bool        extract_chromaticities(Chromaticities *c) const;

    std::vector<uint8_t> dump_to_memory() const;

    bool is_CMYK() const; //!< Check if this is a CMYK profile.
    bool is_RGB() const;  //!< Check if this is a RGB profile.
    bool is_Gray() const; //!< Check if this is a Grayscale profile.

    /*!
        \brief Linearize a (potentially interleaved) array of floating-point pixel values  in-place using the transfer
               function of this ICC profile.

        \param[in,out] pixels
            Pointer to the pixel buffer.
        \param[in] size
            The dimensions of the pixels array in width, height, and number of channels. If size.z > 1 the pixel array
            is interleaved.
        \param[in] keep_primaries
            If true, then try to apply only the inverse transfer function of the ICC profile while keeping the primaries
            unchanged. Otherwise, transform to Rec709/sRGB or Gray at D65 primaries as appropriate.
        \param[out] profile_description
            The profile name/description will be written to this string.
        \param[out] Chromaticities
            If not nullptr, the chromaticities of the ICC profile will be written to these variables.
        \returns
            True if the pixel values were successfully linearized.
    */
    bool linearize_pixels(float *pixels, int3 size, bool keep_primaries = true,
                          std::string *profile_description = nullptr, Chromaticities *c = nullptr) const;

    /**
       \brief Transform an array of floating-point pixels between two ICC profiles in-place.

       Creates an ICC transform and applies it to the provided pixel buffer, converting pixels from
       `profile_in` to `profile_out`. The pixel buffer is modified in-place and is expected to be
       interleaved row-major with `size.z` channels per pixel.

       Supported conversions (when built with LCMS2) include RGB↔RGB, CMYK→RGBA, and Gray↔Gray.
       For CMYK inputs this function internally converts values to the range expected by LCMS
       (LCMS commonly expects CMYK in [0,100] for float formats).

       \param[in,out] pixels  Pointer to the pixel buffer. On success the buffer contains transformed
                              pixel values. For RGB/Gray values are typically in [0,1]. For CMYK
                              inputs values are expected in [0,1] and are rescaled internally.
       \param[in] size        Dimensions of the pixel buffer: `size.x` = width, `size.y` = height,
                              `size.z` = number of channels (e.g., 3 for RGB, 4 for RGBA/CMYK).
       \param[in] profile_in  Source ICC profile (must be valid).
       \param[in] profile_out Destination ICC profile (must be valid).
       \returns               True if the transform was created and applied successfully; false otherwise.

       \note The function uses LCMS2 when `HDRVIEW_ENABLE_LCMS2` is enabled; without LCMS2 it just returns false.
             The function sets alpha to 1.0 for outputs produced by CMYK→RGBA conversions.
     */
    static bool transform_pixels(float *pixels, int3 size, const ICCProfile &profile_in, const ICCProfile &profile_out);

private:
    //! Internal constructor from raw profile pointer.
    ICCProfile(void *profile) : m_profile{profile} {}
    void *m_profile = nullptr;
};

// A lightweight wrapper for color profiles described using CICP integers
// (color_primaries, transfer_characteristics, matrix_coeffs, full_range). This class
// provides utility functions analogous to `ICCProfile` but for CICP-encoded
// profiles and uses the helpers in `colorspace.h`.
class CICPProfile
{
public:
    // defaults to unspecified RGB full-range
    CICPProfile(const std::array<int, 4> &quad = {2, 2, -1, -1}) : m_quad{quad} {}
    CICPProfile(int color_primaries, int transfer_characteristics, int matrix_coeffs, int full_range) :
        m_quad{color_primaries, transfer_characteristics, matrix_coeffs, full_range}
    {
    }

    static CICPProfile BT709() { return CICPProfile{1, 1, 1, 1}; }
    static CICPProfile sRGB() { return CICPProfile{1, 13, 0, 1}; }
    static CICPProfile linear_sRGB() { return CICPProfile{1, 8, 0, 1}; }

    // Construct a CICPProfile from explicit enums. If the provided enums/map
    // cannot be converted to CICP integer codes an invalid CICPProfile is
    // returned (i.e. `valid()` will be false).
    static CICPProfile from_gamut_and_tf(ColorGamut_ gamut, TransferFunction tf, int matrix_coeffs = 0,
                                         bool full_range = true);

    // casting to array
    operator std::array<int, 4> &() noexcept { return m_quad; }
    operator const std::array<int, 4> &() const noexcept { return m_quad; }
    const std::array<int, 4> &to_array() const { return m_quad; }

    // color primaries
    int  cp() const { return m_quad[0]; }
    int &cp() { return m_quad[0]; }

    // transfer characteristics
    int  tc() const { return m_quad[1]; }
    int &tc() { return m_quad[1]; }

    // matrix coefficients
    int  mc() const { return m_quad[2]; }
    int &mc() { return m_quad[2]; }

    // range
    int  r() const { return m_quad[3]; }
    int &r() { return m_quad[3]; }
    bool full_range() const { return m_quad[3] != 0; }

    int  operator[](size_t index) const { return m_quad[index]; }
    int &operator[](size_t index) { return m_quad[index]; }

    // Validate the stored CICP codes. Returns true if both the transfer characteristics and color primaries map to
    // known entries in the helpers from `colorspace.h`.
    bool valid() const;

    //! Returns true if the transfer characteristics code maps to a known HDR transfer function -- PQ (16) or HLG (18).
    bool     is_HDR() const { return tc() == 16 || tc() == 18; }
    explicit operator bool() const noexcept { return valid(); }

    // Returns true if the color primaries code maps to a known chromaticity set
    bool has_known_primaries() const;

    // Returns true if the transfer characteristics code maps to a known transfer
    bool has_known_transfer() const;

    // Return the TransferFunction corresponding to `m_transfer_characteristics`.
    TransferFunction transfer_function() const;
    // Return the Chromaticities corresponding to `m_color_primaries`.
    Chromaticities chromaticities() const;

    // Convert to the `ColorGamut_` enum if possible, otherwise returns
    // `ColorGamut_Unspecified`.
    ColorGamut_ gamut_enum() const;

    // Human-friendly names for the stored codes. If the code is unknown a
    // string containing the numeric code is returned.
    std::string cp_long_name() const;
    std::string tc_long_name() const;
    std::string mc_long_name() const;
    std::string r_long_name() const { return full_range() ? "Full" : "Narrow"; }

    const char *cp_short_name() const;
    const char *tc_short_name() const;
    const char *mc_short_name() const;
    const char *r_short_name() const { return full_range() ? "FR" : "NR"; }
    std::string short_name() const;

    // Linearize a pixel buffer encoded with this CICP profile. If `keep_primaries`
    // is false, the pixels will also be converted to Rec.709/sRGB primaries.
    bool linearize_pixels(float *pixels, int3 size, bool keep_primaries = true,
                          std::string *profile_description = nullptr, Chromaticities *c = nullptr,
                          AdaptationMethod CAT_method = AdaptationMethod_Bradford) const;

    // Transform pixel buffer in-place from one CICP profile to another. This
    // performs inverse transfer of `profile_in`, optional primary conversion,
    // and forward transfer of `profile_out`.
    static bool transform_pixels(float *pixels, int3 size, const CICPProfile &profile_in,
                                 const CICPProfile &profile_out,
                                 AdaptationMethod   CAT_method = AdaptationMethod_Bradford);

private:
    std::array<int, 4> m_quad;
};

// version of linearize_pixels that uses chromaticities and transfer function (instead of ICC or CICP profiles)
bool linearize_pixels(float *pixels, int3 size, const Chromaticities &gamut, const TransferFunction &tf,
                      bool keep_primaries, std::string *profile_description, Chromaticities *c,
                      AdaptationMethod CAT_method = AdaptationMethod_Bradford);