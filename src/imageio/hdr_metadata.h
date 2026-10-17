//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"
#include "json.h"

#include <string_view>

/// Metadata key under which a loader records an image's content light level (MaxCLL and MaxFALL).
inline constexpr const char *k_content_light_level_key = "Content light level";

/// Metadata key under which a loader records the color volume of the display the image was mastered on.
inline constexpr const char *k_mastering_display_key = "Mastering display";

/// Metadata key under which a loader records the luminance of an image's (1, 1, 1), its white level.
inline constexpr const char *k_white_luminance_key = "White luminance";

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
