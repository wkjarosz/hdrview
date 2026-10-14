//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdr_metadata.h"

#include <string>

namespace
{

std::string nits_or_unknown(float nits) { return nits > 0.f ? fmt::format("{:g} cd/m²", nits) : "unknown"; }

} // namespace

void add_white_luminance(json &header, float nits, std::string_view source)
{
    header[k_white_luminance_key] = {
        {"value", nits},
        {"string", nits_or_unknown(nits)},
        {"type", "white luminance"},
        {"description", fmt::format("From the {}: the luminance the image's (1, 1, 1) stands for", source)}};
}

void add_content_light_level(json &header, float max_cll, float max_fall, std::string_view source)
{
    header[k_content_light_level_key] = {
        {"value", {{"MaxCLL", max_cll}, {"MaxFALL", max_fall}}},
        {"string", fmt::format("MaxCLL: {}\nMaxFALL: {}", nits_or_unknown(max_cll), nits_or_unknown(max_fall))},
        {"type", "content light level"},
        {"description", fmt::format("From the {}: the brightest pixel (MaxCLL) and the brightest frame's average "
                                    "(MaxFALL) the content reaches",
                                    source)}};
}

void add_mastering_display(json &header, const Chromaticities &chr, float min_nits, float max_nits,
                           std::string_view source)
{
    header[k_mastering_display_key] = {
        {"value",
         {{"red", chr.red},
          {"green", chr.green},
          {"blue", chr.blue},
          {"white", chr.white},
          {"min luminance", min_nits},
          {"max luminance", max_nits}}},
        {"string", fmt::format("R: ({:.4f},{:.4f})\nG: ({:.4f},{:.4f})\nB: ({:.4f},{:.4f})\nW: ({:.4f},{:.4f})\n"
                               "Luminance: {} to {}",
                               chr.red.x, chr.red.y, chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x,
                               chr.white.y, nits_or_unknown(min_nits), nits_or_unknown(max_nits))},
        {"type", "mastering display"},
        {"description",
         fmt::format("From the {}: the primaries, white and luminance range of the display the image was mastered on",
                     source)}};
}
