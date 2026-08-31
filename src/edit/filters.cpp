//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/filters.h"

#include <algorithm>
#include <cmath>
#include <smallthreadpool.h>
#include <vector>

namespace
{

//! Sample \p a at \p x, \p y with the border clamped to the nearest sample inside it.
inline float clamped(const Array2Df &a, int x, int y)
{
    return a(std::clamp(x, 0, a.width() - 1), std::clamp(y, 0, a.height() - 1));
}

//! Normalized 1D Gaussian taps out to where they stop mattering.
/*!
    Truncated at three standard deviations, past which the taps carry well under a thousandth of the
    weight -- far below what a float sample can express -- so keeping them costs time and changes nothing.
*/
std::vector<float> gaussian_taps(float sigma)
{
    const int          radius = std::max(1, int(std::ceil(3.f * sigma)));
    std::vector<float> taps(size_t(2 * radius + 1));

    float sum = 0.f;
    for (int i = -radius; i <= radius; ++i)
    {
        const float t            = std::exp(-float(i * i) / (2.f * sigma * sigma));
        taps[size_t(i + radius)] = t;
        sum += t;
    }
    // Normalized rather than scaled by the analytic 1/(sigma*sqrt(2pi)): the truncated taps do not sum to
    // that, and a filter whose weights miss one leaves the image slightly darker or brighter than it was.
    for (auto &t : taps) t /= sum;

    return taps;
}

//! Convolve along one axis with \p taps, centered.
Array2Df convolve_axis(const Array2Df &src, const std::vector<float> &taps, bool horizontal)
{
    Array2Df  out{src.size()};
    const int radius     = int(taps.size() / 2);
    const int block_size = std::max(1, 1024 * 1024 / std::max(1, src.width()));

    stp::parallel_for(stp::blocked_range<int>(0, src.height(), block_size),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                              for (int x = 0; x < src.width(); ++x)
                              {
                                  float sum = 0.f;
                                  for (int i = -radius; i <= radius; ++i)
                                      sum += taps[size_t(i + radius)] *
                                             (horizontal ? clamped(src, x + i, y) : clamped(src, x, y + i));
                                  out(x, y) = sum;
                              }
                      });

    return out;
}

} // namespace

Array2Df gaussian_blurred(const Array2Df &src, float sigma_x, float sigma_y)
{
    Array2Df out{src.size()};
    std::copy(src.data(), src.data() + src.num_elements(), out.data());

    if (sigma_x > 0.f)
        out = convolve_axis(out, gaussian_taps(sigma_x), true);
    if (sigma_y > 0.f)
        out = convolve_axis(out, gaussian_taps(sigma_y), false);

    return out;
}

Array2Df box_blurred(const Array2Df &src, int half_width_x, int half_width_y)
{
    Array2Df out{src.size()};
    std::copy(src.data(), src.data() + src.num_elements(), out.data());

    // Uniform taps, so the same separable pass serves; a running sum would be faster still but this is
    // already O(radius) per sample rather than O(radius^2).
    if (half_width_x > 0)
        out = convolve_axis(out, std::vector<float>(size_t(2 * half_width_x + 1), 1.f / float(2 * half_width_x + 1)),
                            true);
    if (half_width_y > 0)
        out = convolve_axis(out, std::vector<float>(size_t(2 * half_width_y + 1), 1.f / float(2 * half_width_y + 1)),
                            false);

    return out;
}

Array2Df unsharp_masked(const Array2Df &src, float sigma, float amount)
{
    const Array2Df blurred = gaussian_blurred(src, sigma, sigma);

    Array2Df  out{src.size()};
    const int block_size = std::max(1, 1024 * 1024 / std::max(1, src.width()));
    stp::parallel_for(stp::blocked_range<int>(0, src.height(), block_size),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                              for (int x = 0; x < src.width(); ++x)
                                  out(x, y) = src(x, y) + amount * (src(x, y) - blurred(x, y));
                      });

    return out;
}
