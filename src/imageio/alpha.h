/** \file alpha.h
    \author Wojciech Jarosz
*/

#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "imageio/image_loader.h"
#include <optional>

//! The alpha override from the load options, or nullopt. Loaders need it before linearizing.
inline std::optional<AlphaType_> alpha_override_of(const ImageLoadOptions &opts)
{
    return opts.override_alpha ? std::optional<AlphaType_>{opts.alpha_override} : std::nullopt;
}

/** \name Premultiplied alpha across a transfer function
    \f$EOTF(a \cdot C) \neq a \cdot EOTF(C)\f$, so color premultiplied after encoding must be divided by
    alpha before the transfer is inverted and multiplied back once it is. Call these in pairs around a
    loader's linearization; both are no-ops for every other AlphaType_. `pixels` is interleaved with `size.z`
    channels, alpha last.
*/
/**@{*/
void unpremultiply_before_transfer(float *pixels, int3 size, AlphaType_ alpha_type);
void repremultiply_after_transfer(float *pixels, int3 size, AlphaType_ alpha_type);
/**@}*/
