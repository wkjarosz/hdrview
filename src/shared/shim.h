/** \file shim.h
    The dialect that the headers in src/shared/ are written in, which reads the same as C++ and as GLSL 4.50.

    A shared header is @include'd into a shader's @block, which sokol-shdc cross-compiles to every shader
    language HDRView targets, and #include'd by C++, where its functions land in namespace `shader`. Its code
    keeps to what the two languages agree on:
    - float only, with literals written 1.0f, so C++ does not promote to double;
    - mul(M, v) for a matrix product, since linalg's operator* is component-wise where GLSL's is not;
    - components as .x/.y/.z/.w and vectors built by constructors, since linalg has no .rgb or swizzles;
    - no assignment to a parameter, which SPIRV-Cross turns into an inout (in Metal, thread&) parameter;
    - no templates, `out` parameters or recursion, and only the builtins this file provides to C++.
*/

#ifndef HDRVIEW_SHARED_SHIM_H
#define HDRVIEW_SHARED_SHIM_H

#ifdef __cplusplus

#include "fwd.h"

#include <algorithm>
#include <cmath>

namespace shader
{
using vec2 = float2;
using vec3 = float3;
using vec4 = float4;
using mat3 = float3x3;

using la::dot;
using la::mul;
using std::abs;
using std::max;
using std::min;
using std::pow;
} // namespace shader

// clang-format off
#define SHARED_FN    inline
#define SHARED_CONST inline constexpr
#define SHARED_BEGIN namespace shader {
#define SHARED_END   }
// clang-format on

#else

#define SHARED_FN
#define SHARED_CONST const
#define SHARED_BEGIN
#define SHARED_END
#define mul(a, b) ((a) * (b))

#endif

#endif // HDRVIEW_SHARED_SHIM_H
