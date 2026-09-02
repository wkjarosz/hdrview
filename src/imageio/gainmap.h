//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include "imageio/exif.h"

#include <string_view>

#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Written from ISO 21496-1, Apple's published reconstruction parameters, and the files themselves;
// tev (https://github.com/Tom94/tev) was a valuable reference for which containers carry which flavor
// of gain map and for the flags libultrahdr writes that ISO 21496-1 does not define.
//
// A gain map holds, per pixel, how much brighter an image's HDR rendition is than the SDR pixels stored
// as the file's base. HDRView's working space is unbounded, so the default is to reconstruct in full;
// lowering the target trades highlight detail for a rendition closer to the base image.

/// Parameters of Apple's gain-map format, which predates ISO 21496-1 and is not compatible with it.
/**
    Apple stores no minimum, maximum or gamma: the strength follows from two maker-note numbers by a
    piecewise-linear fit published in Apple's developer forums.
*/
struct AppleGainmapParams
{
    float hdr_headroom = 0.f; ///< Apple maker-note tag 0x21 ("HDR Headroom")
    float hdr_gain     = 8.f; ///< Apple maker-note tag 0x30 ("HDR Gain")

    /// Stops of reconstruction this pair asks for, i.e. the log2 of the fully-applied gain.
    float stops() const;
};

/// A decoded gain-map image, at the resolution and channel count the file stored it at.
/**
    Samples are normalized to [0,1] but still gamma-encoded; linearizing them is part of applying the map.
*/
struct GainmapImage
{
    std::vector<float> pixels;       ///< Interleaved samples, normalized to [0,1]
    int2               size{0, 0};   ///< Resolution the file stored the map at
    int                channels = 0; ///< Samples per pixel: 1 for a monochrome map, 3 for a per-channel one

    bool valid() const
    {
        return size.x > 0 && size.y > 0 && channels > 0 && pixels.size() >= size_t(size.x) * size.y * channels;
    }
};

/// Target headroom, in stops, meaning "reconstruct the map in full".
inline constexpr float k_full_gainmap_headroom = std::numeric_limits<float>::infinity();

/// Bilinearly resample \p gainmap to \p size, the base image's resolution, or an empty map if it is malformed.
/**
    \p linearize undoes an sRGB encoding on the way: Apple's maps are sRGB-encoded, ISO 21496-1 maps carry
    their own and must not be.
*/
GainmapImage resample_gainmap(const GainmapImage &gainmap, int2 size, bool linearize);

/// Append \p gainmap to \p image as a `gainmap.*` channel group. It must already be \p image's size.
void append_gainmap_channels(Image &image, const GainmapImage &gainmap);

/// Copy \p image's \p num_base leading color channels into a `base.*` group, before a gain map is applied.
/**
    Alpha is not part of what the map converts and is not copied.
*/
void append_base_rendition(Image &image, int num_base);

/// Whether \p aux_type, or a document containing it, names one of Apple's gain maps.
/**
    Apple dates this URN both 2020 and 2023 and spells it differently in HEIF's auxC property and in a
    JPEG's XMP, so this matches the parts that have stayed put, case-insensitively.
*/
bool is_apple_gainmap_type(std::string_view aux_type);

/// Reconstruction strength for an Apple gain map, from the EXIF of the primary image (not of the map).
/**
    The defaults stand when the maker note is absent, giving the weakest reconstruction.
*/
AppleGainmapParams apple_gainmap_params(const Exif &exif);

/// Flatten a decoded image's color channels into a gain map, stopping at three: a map is monochrome or RGB.
/**
    Empty if \p map has no channels or they disagree on size.
*/
GainmapImage gainmap_from_image(const Image &map);

/// Metadata of an ISO 21496-1 gain map: how to turn the base rendition into the alternate one.
/**
    The map stores, per channel, a normalized value that decodes to a log2 gain between \ref min and
    \ref max. Either rendition may be the brighter one: JPEG XL files often store the HDR rendition as the
    base and use the map to derive an SDR one. Adobe's `hdrgm:` XMP schema encodes the same parameters.
*/
struct IsoGainmapParams
{
    float3 min{0.f};                     ///< log2 gain at a gain-map value of 0
    float3 max{1.f};                     ///< log2 gain at a gain-map value of 1
    float3 gamma{1.f};                   ///< Encoding gamma of the map's own values
    float3 base_offset{1.f / 64.f};      ///< Added to the base pixels before the gain is applied
    float3 alternate_offset{1.f / 64.f}; ///< Subtracted from the result afterwards

    float base_headroom      = 0.f; ///< Stops of headroom the base rendition is graded for
    float alternate_headroom = 1.f; ///< Stops of headroom the alternate rendition is graded for

    /// Whether the map applies in the base image's color space rather than the alternate image's.
    bool use_base_color_space = true;

    std::string version = "n/a"; ///< For display: which encoding of these values the file used

    /// How much of the map to apply to land at \p target_stops of headroom.
    /**
        0 leaves the base rendition alone, 1 applies the map in full, and negative applies it in reverse
        when the alternate is darker.
    */
    float weight(float target_stops) const;
};

/// Parse the binary form ISO 21496-1 defines, as carried in a JPEG APP2 or a JPEG XL `jhgm` box.
/**
    \param data  Start of the metadata proper. A HEIF `tmap` item prefixes it with the one-byte version
                 field of ISO/IEC 23008-12:2024's ToneMapImage, which the caller must consume first
    \param size  Its length in bytes
    \throws std::invalid_argument if the data is truncated or announces a version this cannot read
*/
IsoGainmapParams parse_iso_gainmap(const uint8_t *data, size_t size);

/// Parse Adobe's `hdrgm:` XMP schema, which encodes the same parameters as ISO 21496-1.
/**
    Returns nullopt when the packet carries no `hdrgm:` properties, and throws when one is present but a
    defaultless property is missing.
*/
std::optional<IsoGainmapParams> parse_hdrgm_xmp(const char *xml, size_t len);

/// Apply an ISO 21496-1 gain map to \p image (already linearized, modified in place), and append the map.
/**
    The appended map is decoded out of its gamma and normalization, so the group reads in log2 gain.
    \p target_stops is the headroom to land at, k_full_gainmap_headroom for the alternate rendition in full;
    \p keep_renditions also keeps the file's own base rendition and gain map as their own channel groups.
*/
void apply_iso_gainmap(Image &image, const GainmapImage &gainmap, const IsoGainmapParams &params, float target_stops,
                       bool keep_renditions);

/// Apply an Apple-format gain map to \p image (already linearized, modified in place), and append the map.
/**
    The map is appended whether or not it is applied, so it stays inspectable at \p target_stops of zero.
    Apple's maps are monochrome, so the scale is the same for every color channel and applying it commutes
    with a primaries conversion; \p image may already have been converted to Rec. 709 when it gets here.
*/
void apply_apple_gainmap(Image &image, const GainmapImage &gainmap, const AppleGainmapParams &params,
                         float target_stops, bool keep_renditions);
