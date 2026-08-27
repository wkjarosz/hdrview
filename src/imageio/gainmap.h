//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include <limits>
#include <string>
#include <vector>

/**
    An HDR gain map holds, per pixel, how much brighter the HDR rendition of an image is than the SDR
    rendition stored in the file's base pixels. A viewer reconstructs the HDR rendition by scaling the
    base pixels up by that amount, as far as the display it is targeting can go.

    HDRView renders in extended sRGB with unbounded range, so there is no display ceiling to clip
    against: the default is to reconstruct the map in full. Lowering the target trades reconstructed
    highlight detail for a rendition closer to the base image, and zero shows the base image itself.
*/

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

/// A decoded gain-map image, in whatever resolution and channel count the file stored it at.
/**
    Gain maps are commonly stored at a fraction of the base image's resolution, and are usually
    monochrome. Values are the file's samples normalized to [0,1] and otherwise untouched --
    in particular still gamma-encoded, since linearizing them is part of applying the map.
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

/// Apply an Apple-format gain map to \p image, and append the map itself as a channel group.
/**
    The map is linearized, resized to the base image's resolution, and appended as a `gainmap.*`
    channel group whether or not it is applied, so that it stays inspectable at \p target_stops of
    zero. The base image's color channels are then scaled in place; its alpha channel is left alone.

    Apple's maps are monochrome, so the scale is the same for every color channel and applying it
    commutes with the primaries conversion the loader has already done. This is why \p image may
    already have been converted to Rec. 709.

    \param image         Base image, already linearized. Modified in place
    \param gainmap       Decoded gain map, still gamma-encoded
    \param params        Reconstruction strength, from the primary image's Apple maker note
    \param target_stops  Ceiling on the reconstruction, in stops; k_full_gainmap_headroom for all of it
*/
void apply_apple_gainmap(Image &image, const GainmapImage &gainmap, const AppleGainmapParams &params,
                         float target_stops);
