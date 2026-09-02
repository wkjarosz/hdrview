/** \file alpha.h
    \author Wojciech Jarosz
*/

#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "imageio/image_loader.h"

//! What the file says its alpha is, unless the load options override it.
/*!
    Resolved by each loader rather than stamped on afterwards, because the answer decides whether the color
    channels have to be divided by alpha before the transfer function is inverted -- which happens while the
    loader still holds the encoded samples.

    This is only half of an override. AlphaType_None also has to keep the channel out of an alpha-bearing
    group, and that half is applied centrally, once a loader returns, by BackgroundImageLoader. So a caller
    that reaches a loader directly rather than through load_image() gets the premultiplication but not the
    grouping.
*/
inline AlphaType_ effective_alpha_type(const ImageLoadOptions &opts, AlphaType_ from_file)
{
    return opts.override_alpha ? opts.alpha_override : from_file;
}

/** \name Premultiplied alpha across a transfer function
    Multiplying by alpha and applying a transfer function do not commute, so color premultiplied *after*
    encoding has to be divided by alpha before the transfer is inverted and multiplied back once it is:
    \f$EOTF(a \cdot C) \neq a \cdot EOTF(C)\f$. Call these in pairs around whatever a loader does to reach
    linear light. Both are no-ops for every other AlphaType_: color premultiplied in linear light is already
    what the inverse transfer produces, and straight color is premultiplied later, by Image::finalize(), once
    every loader has reached the same representation.

    Nearly every format's premultiplied alpha is the post-transfer kind -- Photoshop, OpenImageIO,
    ImageMagick and vips all write it that way -- but a file can carry either, which is what
    ImageLoadOptions::alpha_override exists to state. `pixels` is interleaved with `size.z` channels, alpha
    last. A linear transfer makes the pair cancel, so this stays correct for formats storing linear samples.
*/
/**@{*/
void unpremultiply_before_transfer(float *pixels, int3 size, AlphaType_ alpha_type);
void repremultiply_after_transfer(float *pixels, int3 size, AlphaType_ alpha_type);
/**@}*/
