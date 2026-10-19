//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "json.h"

#include <optional>
#include <string_view>

/// Metadata key under which a loader records an image's content light level (MaxCLL and MaxFALL).
inline constexpr const char *k_content_light_level_key = "Content light level";

/// Metadata key under which a loader records the color volume of the display the image was mastered on.
inline constexpr const char *k_mastering_display_key = "Mastering display";

/// Metadata key under which a loader records the luminance of an image's (1, 1, 1), its white level.
inline constexpr const char *k_white_luminance_key = "White luminance";

/// The black level, in cd/m^2, HDRView gives a mastering display it describes, since it models no display's.
/**
    0.0001 cd/m^2, the conventional HDR10 value and the smallest that SMPTE ST 2086 and H.265 can express.
*/
inline constexpr float k_mastering_display_min_nits = 0.0001f;

/// Records in \p header the luminance, in cd/m^2, that an image's (1, 1, 1) stands for.
/**
    OpenEXR's whiteLuminance attribute, for one, which is how a scene-linear file says what its values mean in
    absolute terms. \p source names where the value came from, for the Info panel's description.
*/
void add_white_luminance(json &header, float nits, std::string_view source);

/// Records a content light level in \p header: MaxCLL and MaxFALL in cd/m^2, 0 meaning the file does not say.
/**
    MaxCLL bounds the brightest pixel, MaxFALL the brightest frame's average (CTA-861.3). \p source names where
    the values came from, such as a PNG cLLI chunk, for the Info panel's description.
*/
void add_content_light_level(json &header, float max_cll, float max_fall, std::string_view source);

/// Records the mastering display's color volume in \p header: its primaries and white, and luminance in cd/m^2.
/**
    SMPTE ST 2086's description of the display an image was graded on. A luminance of 0 means the file does
    not say. \p source names where the values came from, for the Info panel's description.
*/
void add_mastering_display(json &header, const Chromaticities &primaries, float min_nits, float max_nits,
                           std::string_view source);

/// A mastering display's color volume, as add_mastering_display() records it.
struct MasteringDisplay
{
    Chromaticities primaries;      ///< Its primaries and white
    float          min_nits = 0.f; ///< Its black, in cd/m^2, or 0 when unknown
    float          max_nits = 0.f; ///< Its peak, in cd/m^2, or 0 when unknown
};

/// The mastering display recorded in \p header, if one is.
std::optional<MasteringDisplay> mastering_display(const json &header);

/// The white luminance recorded in \p header, if one is.
std::optional<float> white_luminance(const json &header);

/// A content light level in cd/m^2: the brightest sample (MaxCLL) and the average pixel's brightest (MaxFALL).
struct ContentLightLevel
{
    float max_cll  = 0.f;
    float max_fall = 0.f;
};

/// The content light level recorded in \p header, if one is.
std::optional<ContentLightLevel> content_light_level(const json &header);

/// Measures the content light level of the samples a writer gets from \p img, with SDR white at 203 cd/m^2.
/**
    The samples are the selected group's, linear and straight, in \p img's primaries or, with \p convert_to_sRGB, in
   BT.709's, as Image::as_interleaved() gives them. Negative and non-finite samples count as 0; alpha does not count.
*/
ContentLightLevel measure_content_light_level(const Image &img, bool convert_to_sRGB);
