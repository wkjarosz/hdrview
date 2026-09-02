/** \file alpha.cpp
    \author Wojciech Jarosz
*/

#include "imageio/alpha.h"
#include <smallthreadpool.h>

using namespace stp;

namespace
{

//! Scale each pixel's color channels by `factor(alpha)`, leaving alpha itself alone.
template <typename F>
void scale_colors_by_alpha(float *pixels, int3 size, F &&factor)
{
    int block_size = std::max(1, 1024 * 1024 / size.x);
    parallel_for(blocked_range<int>(0, size.y, block_size),
                 [pixels, size, &factor](int begin_y, int end_y, int, int)
                 {
                     for (int y = begin_y; y < end_y; ++y)
                         for (int x = 0; x < size.x; ++x)
                         {
                             const size_t scanline = size_t(x + y * size.x) * size.z;
                             const float  f        = factor(pixels[scanline + size.z - 1]);
                             for (int c = 0; c < size.z - 1; ++c) pixels[scanline + c] *= f;
                         }
                 });
}

bool needs_undoing(int3 size, AlphaType_ alpha_type)
{
    return alpha_type == AlphaType_PremultipliedNonLinear && size.z > 1;
}

} // namespace

void unpremultiply_before_transfer(float *pixels, int3 size, AlphaType_ alpha_type)
{
    if (!needs_undoing(size, alpha_type))
        return;

    // a fully transparent pixel carries no color to recover; don't divide by zero
    scale_colors_by_alpha(pixels, size, [](float a) { return a == 0.f ? 1.f : 1.f / a; });
}

void repremultiply_after_transfer(float *pixels, int3 size, AlphaType_ alpha_type)
{
    if (!needs_undoing(size, alpha_type))
        return;

    scale_colors_by_alpha(pixels, size, [](float a) { return a; });
}
