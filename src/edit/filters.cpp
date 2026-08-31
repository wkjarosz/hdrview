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

//! Uniform taps of the given half-width, for a box of the same separable shape.
std::vector<float> box_taps(int half_width)
{
    return std::vector<float>(size_t(2 * half_width + 1), 1.f / float(2 * half_width + 1));
}

/*!
    Convolve \p region of \p src with the separable kernel \p taps_x by \p taps_y.

    Only \p region is produced, and only what feeds it is read. The horizontal pass covers the region's
    columns over rows grown by the vertical radius, because that is exactly the set the vertical pass then
    reads: growing by more would be wasted work, by less would read samples that were never computed. An
    empty tap list along an axis means that axis is left alone.
*/
Array2Df convolve_separable(const Array2Df &src, const Box2i &region, const std::vector<float> &taps_x,
                            const std::vector<float> &taps_y)
{
    const int  rx     = taps_x.empty() ? 0 : int(taps_x.size() / 2);
    const int  ry     = taps_y.empty() ? 0 : int(taps_y.size() / 2);
    const int2 extent = region.size();

    // Rows the vertical pass will reach for, which is the region's own rows grown by its radius.
    const int rows = extent.y + 2 * ry;

    Array2Df horizontal{int2{extent.x, rows}};
    {
        const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
        stp::parallel_for(stp::blocked_range<int>(0, rows, block_size),
                          [&](int j0, int j1, int, int)
                          {
                              for (int j = j0; j < j1; ++j)
                              {
                                  const int y = region.min.y - ry + j;
                                  for (int i = 0; i < extent.x; ++i)
                                  {
                                      const int x = region.min.x + i;
                                      if (taps_x.empty())
                                      {
                                          horizontal(i, j) = clamped(src, x, y);
                                          continue;
                                      }
                                      float sum = 0.f;
                                      for (int k = -rx; k <= rx; ++k)
                                          sum += taps_x[size_t(k + rx)] * clamped(src, x + k, y);
                                      horizontal(i, j) = sum;
                                  }
                              }
                          });
    }

    Array2Df out{extent};
    {
        const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
        stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                          [&](int y0, int y1, int, int)
                          {
                              for (int y = y0; y < y1; ++y)
                                  for (int x = 0; x < extent.x; ++x)
                                  {
                                      if (taps_y.empty())
                                      {
                                          out(x, y) = horizontal(x, y + ry);
                                          continue;
                                      }
                                      float sum = 0.f;
                                      // The region's row y sits at index y + ry in the taller intermediate.
                                      for (int k = -ry; k <= ry; ++k)
                                          sum += taps_y[size_t(k + ry)] * horizontal(x, y + ry + k);
                                      out(x, y) = sum;
                                  }
                          });
    }

    return out;
}

} // namespace

Array2Df gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y)
{
    return convolve_separable(src, region, sigma_x > 0.f ? gaussian_taps(sigma_x) : std::vector<float>{},
                              sigma_y > 0.f ? gaussian_taps(sigma_y) : std::vector<float>{});
}

Array2Df box_blurred(const Array2Df &src, const Box2i &region, int half_width_x, int half_width_y)
{
    return convolve_separable(src, region, half_width_x > 0 ? box_taps(half_width_x) : std::vector<float>{},
                              half_width_y > 0 ? box_taps(half_width_y) : std::vector<float>{});
}

Array2Df unsharp_masked(const Array2Df &src, const Box2i &region, float sigma, float amount)
{
    const Array2Df blurred = gaussian_blurred(src, region, sigma, sigma);
    const int2     extent  = region.size();

    Array2Df  out{extent};
    const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
    stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                              for (int x = 0; x < extent.x; ++x)
                              {
                                  const float v = clamped(src, region.min.x + x, region.min.y + y);
                                  out(x, y)     = v + amount * (v - blurred(x, y));
                              }
                      });

    return out;
}
