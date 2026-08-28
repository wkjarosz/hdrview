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

// Written from ISO 21496-1, Apple's published reconstruction parameters, and the files themselves.
// tev (https://github.com/Tom94/tev) was a valuable reference while working this out: for which
// containers carry which flavor of gain map, for the flags libultrahdr writes that ISO 21496-1 does
// not define, and for two observations about real files that are recorded at the places they are
// relied on.
//
// An HDR gain map holds, per pixel, how much brighter the HDR rendition of an image is than the SDR
// rendition stored in the file's base pixels. A viewer reconstructs the HDR rendition by scaling the
// base pixels up by that amount, as far as the display it is targeting can go.
//
// HDRView renders in extended sRGB with unbounded range, so there is no display ceiling to clip
// against: the default is to reconstruct the map in full. Lowering the target trades reconstructed
// highlight detail for a rendition closer to the base image, and zero shows the base image itself.

/// Parameters of Apple's gain-map format, which predates ISO 21496-1 and is not compatible with it.
/**
    Apple stores neither the gain-map minimum and maximum nor a gamma. It stores two numbers in the
    primary image's maker note, from which the reconstruction strength follows by a piecewise-linear
    fit published in Apple's developer forums. Both fields default to the values that produce the
    weakest reconstruction, which is what Apple's own software falls back to when the note is absent.
*/
struct AppleGainmapParams
{
    float hdr_headroom = 0.f; //!< Apple maker-note tag 0x21 ("HDR Headroom")
    float hdr_gain     = 8.f; //!< Apple maker-note tag 0x30 ("HDR Gain")

    //! Stops of reconstruction this pair asks for, i.e. the log2 of the fully-applied gain.
    float stops() const;
};

/// A decoded gain-map image, at the resolution and channel count the file stored it at.
/**
    Samples are normalized to [0,1] and otherwise untouched -- in particular still gamma-encoded,
    since linearizing them is part of applying the map.
*/
struct GainmapImage
{
    std::vector<float> pixels;       //!< Interleaved samples, normalized to [0,1]
    int2               size{0, 0};   //!< Resolution the file stored the map at
    int                channels = 0; //!< Samples per pixel: 1 for a monochrome map, 3 for a per-channel one

    bool valid() const
    {
        return size.x > 0 && size.y > 0 && channels > 0 && pixels.size() >= size_t(size.x) * size.y * channels;
    }
};

//! Target headroom, in stops, meaning "reconstruct the map in full".
inline constexpr float k_full_gainmap_headroom = std::numeric_limits<float>::infinity();

/// Resample \p gainmap to \p size, so it lines up with the base image it applies to.
/**
    Gain maps are usually stored at a fraction of the base resolution, so this resamples bilinearly;
    replicating samples instead would put visible blocks around high-contrast highlights.

    \param gainmap    Decoded map, at the resolution the file stored it at
    \param size       Resolution of the base image
    \param linearize  Whether to undo an sRGB encoding on the way. Apple's maps are sRGB-encoded;
                      ISO 21496-1 maps carry their own encoding and must not be
    \return           The resampled map, or an empty one if \p gainmap is malformed
*/
GainmapImage resample_gainmap(const GainmapImage &gainmap, int2 size, bool linearize);

//! Append \p gainmap to \p image as a `gainmap.*` channel group. It must already be \p image's size.
void append_gainmap_channels(Image &image, const GainmapImage &gainmap);

//! Copy \p image's colour channels into a `base.*` group, before a gain map is applied to them.
/*!
    Called "base" rather than "sdr" because which rendition a file stores is the file's choice: a
    base-HDR JPEG XL keeps its HDR rendition here and derives an SDR one from the map. Alpha is not
    part of what the map converts and is not copied.

    \param image     Image to append to
    \param num_base  How many of \p image's leading channels are the base image's own
*/
void append_base_rendition(Image &image, int num_base);

//! Whether an auxiliary image type names one of Apple's gain maps.
/*!
    Apple has dated this URN both 2020 and 2023 and spells it differently in HEIF's auxC property
    and in a JPEG's XMP, so this matches the parts that have stayed put, case-insensitively.

    \param aux_type  The auxiliary image type, or a document containing it
*/
bool is_apple_gainmap_type(std::string_view aux_type);

//! Reconstruction strength for an Apple gain map, from the primary image's maker note.
/*!
    Both fields keep their defaults when the note is absent or has no such tag, which is the weakest
    reconstruction and what Apple's own software falls back to.

    \param exif  EXIF of the *primary* image; Apple sizes the map from there, not from the map itself
*/
AppleGainmapParams apple_gainmap_params(const Exif &exif);

//! Flatten a decoded image's color channels into a gain map, as the loaders' recursion produces it.
/*!
    A gain map is monochrome or RGB; an alpha channel on one would be meaningless, so this stops at
    three channels.

    \param map  Image the container's decoder returned for the gain map
    \return     The map, or an empty one if \p map has no channels or they disagree on size
*/
GainmapImage gainmap_from_image(const Image &map);

/// Metadata of an ISO 21496-1 gain map: how to turn the base rendition into the alternate one.
/**
    The map stores, per channel, a normalized value that decodes to a log2 gain somewhere between
    \ref min and \ref max. Which rendition is which is not fixed: \ref base_headroom and
    \ref alternate_headroom say how much headroom each end wants, and either may be the brighter.
    JPEG XL files in particular often store the HDR rendition as the base and use the map to derive
    an SDR one, in which case the alternate headroom is the lower of the two.

    Adobe's `hdrgm:` XMP schema carries the same information in a different encoding; both parse into
    this struct so that only one implementation has to apply it.
*/
struct IsoGainmapParams
{
    float3 min{0.f};                     //!< log2 gain at a gain-map value of 0
    float3 max{1.f};                     //!< log2 gain at a gain-map value of 1
    float3 gamma{1.f};                   //!< Encoding gamma of the map's own values
    float3 base_offset{1.f / 64.f};      //!< Added to the base pixels before the gain is applied
    float3 alternate_offset{1.f / 64.f}; //!< Subtracted from the result afterwards

    float base_headroom      = 0.f; //!< Stops of headroom the base rendition is graded for
    float alternate_headroom = 1.f; //!< Stops of headroom the alternate rendition is graded for

    //! Whether the map applies in the base image's color space rather than the alternate image's.
    bool use_base_color_space = true;

    std::string version = "n/a"; //!< For display: which encoding of these values the file used

    //! How much of the map to apply to land at \p target_stops of headroom.
    /*!
        Zero leaves the base rendition alone and one applies the map in full. The result is negative
        when the alternate rendition is the darker one, which is how a base-HDR file's map gets
        applied in reverse rather than not at all.
    */
    float weight(float target_stops) const;
};

//! Parse the binary form ISO 21496-1 defines, as carried in a JPEG APP2 or a JPEG XL `jhgm` box.
/*!
    \param data  Start of the metadata proper. A HEIF `tmap` item prefixes this with a one-byte
                version field of its own -- ISO/IEC 23008-12:2024's ToneMapImage, whose payload is
                that byte followed by this structure -- and the caller has to consume it first. A
                JPEG APP2 block or a JPEG XL `jhgm` box carries the metadata on its own.
    \param size  Its length in bytes
    \return      The parsed parameters
    \throws std::invalid_argument if the data is truncated or announces a version this cannot read
*/
IsoGainmapParams parse_iso_gainmap(const uint8_t *data, size_t size);

//! Parse Adobe's `hdrgm:` XMP schema, which encodes the same parameters as ISO 21496-1.
/*!
    \param xml  The XMP packet
    \param len  Its length in bytes
    \return     The parsed parameters, or nullopt when the packet carries no `hdrgm:` properties
    \throws std::invalid_argument if `hdrgm:` is present but missing a property with no default
*/
std::optional<IsoGainmapParams> parse_hdrgm_xmp(const char *xml, size_t len);

/// Apply an ISO 21496-1 gain map to \p image, and append the map itself as a channel group.
/**
    The map is decoded out of its gamma and normalization into the log2 gain it represents, and
    appended in that form; the channel group therefore reads in stops, positive where the alternate
    rendition is brighter. The base image's color channels are then scaled in place, per channel,
    since ISO maps may carry three.

    \param image         Base image, already linearized. Modified in place
    \param gainmap       Decoded gain map, still in its encoded form
    \param params        Parameters from the file's ISO or `hdrgm:` metadata
    \param target_stops     Headroom to land at, in stops; k_full_gainmap_headroom for the alternate
                           rendition in full
    \param keep_renditions  Whether to also keep what the file actually stores -- its base rendition
                           and its gain map -- as their own channel groups
*/
void apply_iso_gainmap(Image &image, const GainmapImage &gainmap, const IsoGainmapParams &params, float target_stops,
                       bool keep_renditions);

/// Apply an Apple-format gain map to \p image, and append the map itself as a channel group.
/**
    The map goes in via append_gainmap_channels() whether or not it is applied, so it stays
    inspectable at \p target_stops of zero. The base image's color channels are then scaled in place;
    alpha is not a color and is left alone.

    Apple's maps are monochrome, so the scale is the same for every color channel, and applying it
    therefore commutes with a primaries conversion -- which is why \p image may already have been
    converted to Rec. 709 by the time it gets here.

    \param image         Base image, already linearized. Modified in place
    \param gainmap       Decoded gain map, still gamma-encoded
    \param params        Reconstruction strength, from the primary image's Apple maker note
    \param target_stops     Ceiling on the reconstruction, in stops; k_full_gainmap_headroom for all
    \param keep_renditions  Whether to also keep what the file actually stores -- its base rendition
                           and its gain map -- as their own channel groups
*/
void apply_apple_gainmap(Image &image, const GainmapImage &gainmap, const AppleGainmapParams &params,
                         float target_stops, bool keep_renditions);
