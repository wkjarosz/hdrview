//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdr_metadata.h"
#include "image.h"

#include <algorithm>
#include <cmath>
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

std::optional<MasteringDisplay> mastering_display(const json &header)
{
    const auto it = header.find(k_mastering_display_key);
    if (it == header.end() || !it->contains("value"))
        return std::nullopt;
    try
    {
        const json      &v = (*it)["value"];
        MasteringDisplay md;
        md.primaries.red   = v.at("red").get<float2>();
        md.primaries.green = v.at("green").get<float2>();
        md.primaries.blue  = v.at("blue").get<float2>();
        md.primaries.white = v.at("white").get<float2>();
        md.min_nits        = v.at("min luminance").get<float>();
        md.max_nits        = v.at("max luminance").get<float>();
        return md;
    }
    catch (const json::exception &)
    {
        return std::nullopt;
    }
}

std::optional<float> white_luminance(const json &header)
{
    const auto it = header.find(k_white_luminance_key);
    if (it == header.end() || !it->contains("value") || !(*it)["value"].is_number())
        return std::nullopt;
    return (*it)["value"].get<float>();
}

std::optional<ContentLightLevel> content_light_level(const json &header)
{
    const auto it = header.find(k_content_light_level_key);
    if (it == header.end() || !it->contains("value"))
        return std::nullopt;
    try
    {
        const json &v = (*it)["value"];
        return ContentLightLevel{v.at("MaxCLL").get<float>(), v.at("MaxFALL").get<float>()};
    }
    catch (const json::exception &)
    {
        return std::nullopt;
    }
}

ContentLightLevel measure_content_light_level(const Image &img, bool convert_to_sRGB)
{
    int        w = 0, h = 0, n = 0;
    const auto samples = img.as_interleaved<float>(&w, &h, &n, TransferFunction::Linear, false, true, convert_to_sRGB);
    const int  num_color = group_has_alpha(img.groups[img.selected_group].type) ? n - 1 : n;

    double brightest = 0., total = 0.;
    for (size_t i = 0, num_pixels = (size_t)w * h; i < num_pixels; ++i)
    {
        float pixel = 0.f;
        for (int c = 0; c < num_color; ++c)
            if (const float v = samples[i * n + c]; std::isfinite(v))
                pixel = std::max(pixel, v);
        brightest = std::max(brightest, (double)pixel);
        total += pixel;
    }
    const double pixels = std::max(1.0, (double)w * h);
    return {float(brightest * HDR_REFERENCE_WHITE_NITS), float(total / pixels * HDR_REFERENCE_WHITE_NITS)};
}
