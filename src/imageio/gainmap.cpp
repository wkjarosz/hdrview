//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "gainmap.h"

#include "colorspace.h"
#include "image.h"
#include "timer.h"

#include <algorithm>
#include <cmath>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

using namespace std;

float AppleGainmapParams::stops() const
{
    // Piecewise-linear fit relating the two maker-note fields to reconstruction strength, from
    // https://developer.apple.com/forums/thread/709331. The 0.01 knee separates the range where a
    // small change in the gain field moves the result a lot from the range where it barely does.
    if (hdr_headroom < 1.f)
        return hdr_gain <= 0.01f ? -20.f * hdr_gain + 1.8f : -0.101f * hdr_gain + 1.601f;
    else
        return hdr_gain <= 0.01f ? -70.f * hdr_gain + 3.f : -0.303f * hdr_gain + 2.303f;
}

//! Sample \p src bilinearly at the location destination pixel (\p x, \p y) of a \p dst_size grid maps to.
static float sample_bilinear(const vector<float> &src, int2 src_size, int channels, int c, int2 dst_size, int x, int y)
{
    // Align pixel centers rather than pixel corners, so that upsampling doesn't shift the map by
    // half of a destination pixel against the base image.
    const float sx = (x + 0.5f) * (float(src_size.x) / dst_size.x) - 0.5f;
    const float sy = (y + 0.5f) * (float(src_size.y) / dst_size.y) - 0.5f;

    const int   x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
    const float fx = sx - x0, fy = sy - y0;

    const auto at = [&](int xi, int yi)
    {
        xi = std::clamp(xi, 0, src_size.x - 1);
        yi = std::clamp(yi, 0, src_size.y - 1);
        return src[(size_t(yi) * src_size.x + xi) * channels + c];
    };

    const float top    = at(x0, y0) * (1.f - fx) + at(x0 + 1, y0) * fx;
    const float bottom = at(x0, y0 + 1) * (1.f - fx) + at(x0 + 1, y0 + 1) * fx;
    return top * (1.f - fy) + bottom * fy;
}

pair<int, int> append_gainmap_channels(Image &image, const GainmapImage &gainmap, bool linearize)
{
    if (image.channels.empty() || !gainmap.valid())
        return {0, 0};

    const int2 size = image.channels.front().size();

    vector<float> src = gainmap.pixels;
    if (linearize)
    {
        // Apple's documentation specifies the Rec. 709 transfer function here. tev's comparisons
        // against the same scenes encoded as ISO 21496-1 gain maps show the maps are really
        // sRGB-encoded, and sRGB is what its loader uses; the two curves differ most exactly where
        // gain maps spend their values, so this is not a distinction without a difference.
        for (auto &v : src) v = (float)sRGB_to_linear(v);
    }

    // Name the group the way Image names its own channels, so that finalize() groups it the same way.
    const int n     = std::min(gainmap.channels, 4);
    const int first = (int)image.channels.size();

    static const char *names[] = {"gainmap.R", "gainmap.G", "gainmap.B", "gainmap.A"};
    for (int c = 0; c < n; ++c)
        image.channels.emplace_back(n < 3 ? (c == 0 ? "gainmap.Y" : "gainmap.A") : names[c], size);

    parallel_for(blocked_range<int>(0, size.y, 128),
                 [&, first, n](int begin_y, int end_y, int, int)
                 {
                     for (int y = begin_y; y < end_y; ++y)
                         for (int x = 0; x < size.x; ++x)
                             for (int c = 0; c < n; ++c)
                                 image.channels[first + c](x, y) =
                                     sample_bilinear(src, gainmap.size, gainmap.channels, c, size, x, y);
                 });

    return {first, n};
}

void apply_apple_gainmap(Image &image, const GainmapImage &gainmap, const AppleGainmapParams &params,
                         float target_stops)
{
    if (image.channels.empty())
    {
        spdlog::warn("Gain map: base image has no channels; skipping.");
        return;
    }

    if (!gainmap.valid())
    {
        spdlog::warn("Gain map: decoded map is empty or malformed ({}x{}, {} channels); skipping.", gainmap.size.x,
                     gainmap.size.y, gainmap.channels);
        return;
    }

    Timer timer;

    // Which channels the gain applies to. Apple's maps are monochrome, so every color channel is
    // scaled by the same amount; alpha is not a color and is left as it is.
    const int  num_base = (int)image.channels.size();
    const auto is_color = [&](int c) { return image.channels[c].name != "A"; };

    const auto [first, n] = append_gainmap_channels(image, gainmap, true);

    const float stops    = params.stops();
    const float applied  = std::clamp(stops, 0.f, target_stops);
    const float headroom = std::exp2(applied);

    image.metadata["header"]["Gain map"] = {
        {"value", "Apple"},
        {"string", "Apple (urn:com:apple:photo:aux:hdrgainmap)"},
        {"type", "string"},
        {"description", "Vendor format Apple used for HDR photos before ISO 21496-1."}};
    image.metadata["header"]["Gain map headroom"] = {
        {"value", stops},
        {"string", fmt::format("{:.3f} stops", stops)},
        {"type", "float"},
        {"description", "Stops of brightening the gain map asks for, derived from Apple maker-note tags "
                        "0x21 (HDR headroom) and 0x30 (HDR gain)."}};
    image.metadata["header"]["Gain map applied"] = {
        {"value", applied},
        {"string", applied < stops ? fmt::format("{:.3f} stops (limited from {:.3f})", applied, stops)
                                   : fmt::format("{:.3f} stops", applied)},
        {"type", "float"},
        {"description", "Stops actually reconstructed, after the target headroom in the image loading options."}};

    spdlog::info("Apple gain map: {}x{}x{}, asks for {:.3f} stops, applying {:.3f} (maker note 0x21={}, 0x30={})",
                 gainmap.size.x, gainmap.size.y, gainmap.channels, stops, applied, params.hdr_headroom,
                 params.hdr_gain);

    // The map is still worth having as a channel group when nothing is reconstructed, so this test
    // comes after it has been linearized and appended above.
    if (headroom <= 1.f)
    {
        spdlog::debug("Apple gain map: target headroom is {:.3f}; leaving base pixels alone.", headroom);
        return;
    }

    const int2 size = image.channels.front().size();
    parallel_for(blocked_range<int>(0, size.y, 128),
                 [&, first, n, num_base, headroom](int begin_y, int end_y, int, int)
                 {
                     for (int c = 0; c < num_base; ++c)
                     {
                         if (!is_color(c))
                             continue;

                         // A monochrome map drives every color channel; a per-channel one pairs up
                         // with the first three, and any beyond that reuse the last.
                         const auto &gain = image.channels[first + std::min(c, n - 1)];
                         auto       &base = image.channels[c];

                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < size.x; ++x) base(x, y) *= 1.f + (headroom - 1.f) * gain(x, y);
                     }
                 });

    spdlog::debug("Applying gain map took: {} seconds.", (timer.elapsed() / 1000.f));
}
