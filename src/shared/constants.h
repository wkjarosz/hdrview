/** \file constants.h
    The enum values the image shader branches on, listed once for both languages; see shim.h for the dialect.

    In GLSL each entry defines an int constant and the enum types stand for int. In C++, where fwd.h defines
    the enums, each entry instead asserts that fwd.h agrees, so moving an enumerator there without moving it
    here fails to compile.
*/

#ifndef HDRVIEW_SHARED_CONSTANTS_H
#define HDRVIEW_SHARED_CONSTANTS_H

#ifdef __cplusplus

#include "shim.h"

#define SHARED_ENUM_VALUE(name, value)                                                                                 \
    static_assert(name == (value), "fwd.h's " #name " differs from src/shared/constants.h, which the shaders use");

#else

#define SHARED_ENUM_VALUE(name, value) const int name = value;

#define Channels_       int
#define Tonemap_        int
#define BlendMode_      int
#define BackgroundMode_ int

#endif

SHARED_ENUM_VALUE(Channels_RGBA, 0)
SHARED_ENUM_VALUE(Channels_RGB, 1)
SHARED_ENUM_VALUE(Channels_Red, 2)
SHARED_ENUM_VALUE(Channels_Green, 3)
SHARED_ENUM_VALUE(Channels_Blue, 4)
SHARED_ENUM_VALUE(Channels_Alpha, 5)
SHARED_ENUM_VALUE(Channels_Y, 6)

SHARED_ENUM_VALUE(Tonemap_Gamma, 0)
SHARED_ENUM_VALUE(Tonemap_FalseColor, 1)
SHARED_ENUM_VALUE(Tonemap_PositiveNegative, 2)

SHARED_ENUM_VALUE(BlendMode_Normal, 0)
SHARED_ENUM_VALUE(BlendMode_Multiply, 1)
SHARED_ENUM_VALUE(BlendMode_Divide, 2)
SHARED_ENUM_VALUE(BlendMode_Add, 3)
SHARED_ENUM_VALUE(BlendMode_Average, 4)
SHARED_ENUM_VALUE(BlendMode_Subtract, 5)
SHARED_ENUM_VALUE(BlendMode_Relative_Subtract, 6)
SHARED_ENUM_VALUE(BlendMode_Difference, 7)
SHARED_ENUM_VALUE(BlendMode_Relative_Difference, 8)

SHARED_ENUM_VALUE(BGMode_Black, 0)
SHARED_ENUM_VALUE(BGMode_White, 1)
SHARED_ENUM_VALUE(BGMode_Dark_Checker, 2)
SHARED_ENUM_VALUE(BGMode_Light_Checker, 3)
SHARED_ENUM_VALUE(BGMode_Custom_Color, 4)

/// The smallest alpha divided by when undoing a premultiply, so that an alpha of 0 leaves a color finite.
/**
    The technical introduction to OpenEXR recommends "a power of two" that is "less than half of the smallest
    positive 16-bit floating-point value"; that value is the denormal 2^-24, so this is 2^-26.
*/
SHARED_CONST float k_small_alpha = 1.0f / 67108864.0f;

#endif // HDRVIEW_SHARED_CONSTANTS_H
