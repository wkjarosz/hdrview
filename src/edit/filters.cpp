//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/filters.h"

#include <algorithm>
#include <array>
#include <atomic>
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

/*!
    Convolve \p region of \p src with the separable kernel \p taps_x by \p taps_y.

    Only \p region is produced, and only what feeds it is read. The horizontal pass covers the region's
    columns over rows grown by the vertical radius, because that is exactly the set the vertical pass then
    reads: growing by more would be wasted work, by less would read samples that were never computed. An
    empty tap list along an axis means that axis is left alone.
*/
Array2Df convolve_separable(const Array2Df &src, const Box2i &region, const std::vector<float> &taps_x,
                            const std::vector<float> &taps_y, AtomicProgress progress)
{
    const int  rx     = taps_x.empty() ? 0 : int(taps_x.size() / 2);
    const int  ry     = taps_y.empty() ? 0 : int(taps_y.size() / 2);
    const int2 extent = region.size();

    // Rows the vertical pass will reach for, which is the region's own rows grown by its radius.
    const int rows = extent.y + 2 * ry;

    Array2Df  horizontal{int2{extent.x, rows}};
    const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));

    // A pass each, so each reports over half of whatever share this filter was given.
    AtomicProgress h_progress{progress, 0.5f};
    h_progress.set_num_steps(rows);

    stp::parallel_for(stp::blocked_range<int>(0, rows, block_size),
                      [&](int j0, int j1, int, int)
                      {
                          for (int j = j0; j < j1; ++j)
                          {
                              if (h_progress.canceled())
                                  return;
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
                              ++h_progress;
                          }
                      });

    Array2Df       out{extent};
    AtomicProgress v_progress{progress, 0.5f};
    v_progress.set_num_steps(extent.y);

    stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                          {
                              if (v_progress.canceled())
                                  return;
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
                              ++v_progress;
                          }
                      });

    return out;
}

/*!
    One separable box pass, costing the same per sample whatever the half-widths.

    The sum over the box is carried along the row (then down the column) rather than recomputed: stepping
    one sample adds the sample entering the box and subtracts the one leaving it. Only the first sum of
    each line costs the half-width; every one after it costs an add and a subtract. This is the whole
    reason to reach for boxes, and what makes repeating them cheap.

    \p src holds the samples for absolute coordinates starting at \p src_origin, so passes can be chained
    over arrays that each cover a different rectangle. Reads outside \p src clamp to its edge.
*/
Array2Df box_pass(const Array2Df &src, int2 src_origin, const Box2i &region, int rx, int ry)
{
    const int2 extent = region.size();
    const int  rows   = extent.y + 2 * ry;

    auto at = [&src, src_origin](int x, int y) { return clamped(src, x - src_origin.x, y - src_origin.y); };

    Array2Df    horizontal{int2{extent.x, rows}};
    const float inv_x = 1.f / float(2 * rx + 1);

    stp::parallel_for(stp::blocked_range<int>(0, rows, std::max(1, 1024 * 1024 / std::max(1, extent.x))),
                      [&](int j0, int j1, int, int)
                      {
                          for (int j = j0; j < j1; ++j)
                          {
                              const int y = region.min.y - ry + j;

                              float acc = 0.f;
                              for (int k = -rx; k <= rx; ++k) acc += at(region.min.x + k, y);
                              horizontal(0, j) = acc * inv_x;

                              for (int i = 1; i < extent.x; ++i)
                              {
                                  const int x = region.min.x + i;
                                  acc += at(x + rx, y) - at(x - 1 - rx, y);
                                  horizontal(i, j) = acc * inv_x;
                              }
                          }
                      });

    Array2Df    out{extent};
    const float inv_y = 1.f / float(2 * ry + 1);

    // The intermediate was sized to exactly what this reads, so it is indexed directly rather than clamped:
    // the region's row y sits at index y + ry, and the box around it spans [y, y + 2*ry].
    stp::parallel_for(stp::blocked_range<int>(0, extent.x, std::max(1, 1024 * 1024 / std::max(1, extent.y))),
                      [&](int x0, int x1, int, int)
                      {
                          for (int x = x0; x < x1; ++x)
                          {
                              float acc = 0.f;
                              for (int k = 0; k <= 2 * ry; ++k) acc += horizontal(x, k);
                              out(x, 0) = acc * inv_y;

                              for (int y = 1; y < extent.y; ++y)
                              {
                                  acc += horizontal(x, y + 2 * ry) - horizontal(x, y - 1);
                                  out(x, y) = acc * inv_y;
                              }
                          }
                      });

    return out;
}

//! \p b grown by \p by on every side, then clipped to \p bounds.
/*!
    The clip is what keeps a chain of passes agreeing with the same chain run over the whole image. Left
    unclipped, an intermediate would extend past the image and the pass after it would clamp against that
    outer edge -- whose samples are a box average of clamped values, not the edge sample the whole-image
    computation clamps to.
*/
Box2i dilated(const Box2i &b, int2 by, const Box2i &bounds)
{
    Box2i out{b.min - by, b.max + by};
    out.intersect(bounds);
    return out;
}

/*!
    Widths for \p n boxes whose combined variance lands as near \p sigma as odd integers allow.

    Compute box blur size for desired sigma and number of iterations:
    The kernel resulting from repeated box blurs of the same width is the
    Irwin-Hall distribution
    (https://en.wikipedia.org/wiki/Irwin-Hall_distribution)

    The variance of the Irwin-Hall distribution with n unit-sized boxes:

         V(1, n) = n/12.

    Since V[w * X] = w^2 V[X] where w is a constant, we know that the
    variance will scale as follows using width-w boxes:

         V(w, n) = w^2*n/12.

    To achieve a certain standard deviation sigma, we want to solve:

         sqrt(V(w, n)) = w*sqrt(n/12) = sigma

    for w, given n and sigma; which is:

         w = sqrt(12/n)*sigma

    Rounding that single width to an odd integer is not good enough here, though: the error does not
    shrink as the count rises -- at sigma 6 it is 5% at six passes and 15% at twelve -- so "more
    iterations" would visibly change how much blur there is, which is the one thing this control must not
    do. Splitting the passes between the two adjacent odd widths and solving for how many take the smaller
    keeps the total within a few percent everywhere, and exact once there are enough passes to choose from.
*/
std::vector<int> box_widths_for_sigma(float sigma, int n)
{
    const double s2 = double(sigma) * double(sigma);

    // Width a single box would need if widths could be continuous. If width is odd, then we can use a
    // centered box and are good to go; if it is even we would need a symmetric pair of off-centered boxes
    // instead, so round down to the next odd width and let the split below make up the difference.
    int lower = int(std::floor(std::sqrt(12.0 * s2 / n + 1.0)));
    if (lower % 2 == 0)
        --lower;
    lower = std::max(1, lower);

    // How many passes take `lower`; the rest take the next odd width up.
    const double ideal = (12.0 * s2 - double(n) * lower * lower - 4.0 * n * lower - 3.0 * n) / (-4.0 * lower - 4.0);
    const int    count = std::clamp(int(std::lround(ideal)), 0, n);

    std::vector<int> widths(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) widths[size_t(i)] = i < count ? lower : lower + 2;
    return widths;
}

} // namespace

Array2Df gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y,
                          AtomicProgress progress)
{
    return convolve_separable(src, region, sigma_x > 0.f ? gaussian_taps(sigma_x) : std::vector<float>{},
                              sigma_y > 0.f ? gaussian_taps(sigma_y) : std::vector<float>{}, progress);
}

namespace
{

//! Run \p half_widths box passes in order, producing \p region.
/*!
    Each pass reads its own half-width beyond what it produces, so the first has to cover the region grown
    by every later pass's reach. Worked out backwards from the rectangle actually wanted, and clipped to the
    image at each step so the clamping matches what the same chain over the whole image would do.
*/
Array2Df box_chain(const Array2Df &src, const Box2i &region, const std::vector<int2> &half_widths,
                   AtomicProgress progress)
{
    const Box2i bounds{int2{0}, src.size()};
    const int   n = int(half_widths.size());

    std::vector<Box2i> regions(size_t(n), Box2i{});
    Box2i              cur = region;
    for (int i = n - 1; i >= 0; --i)
    {
        regions[size_t(i)] = cur;
        cur                = dilated(cur, half_widths[size_t(i)], bounds);
    }

    // Every pass costs the same, so they divide the share evenly.
    const float    share = 1.f / float(n);
    AtomicProgress pass_progress{progress, share};
    pass_progress.set_num_steps(1);

    Array2Df buffer = box_pass(src, int2{0}, regions[0], half_widths[0].x, half_widths[0].y);
    int2     origin = regions[0].min;
    ++pass_progress;

    for (int i = 1; i < n && !progress.canceled(); ++i)
    {
        buffer = box_pass(buffer, origin, regions[size_t(i)], half_widths[size_t(i)].x, half_widths[size_t(i)].y);
        origin = regions[size_t(i)].min;
        ++pass_progress;
    }

    return buffer;
}

} // namespace

Array2Df box_blurred(const Array2Df &src, const Box2i &region, int half_width_x, int half_width_y, int iterations,
                     AtomicProgress progress)
{
    const int2 h = int2{std::max(0, half_width_x), std::max(0, half_width_y)};
    return box_chain(src, region, std::vector<int2>(size_t(std::max(1, iterations)), h), progress);
}

Array2Df fast_gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y, int iterations,
                               AtomicProgress progress)
{
    const int n = std::max(1, iterations);

    // n boxes carry the variance of the Irwin-Hall distribution: the sum of each box's. Choosing widths so
    // that total lands on the sigma asked for is what holds the width of the result fixed as the count
    // changes -- iterations buy accuracy, not more blur.
    const std::vector<int> wx =
        sigma_x > 0.f ? box_widths_for_sigma(sigma_x, n) : std::vector<int>(static_cast<size_t>(n), 1);
    const std::vector<int> wy =
        sigma_y > 0.f ? box_widths_for_sigma(sigma_y, n) : std::vector<int>(static_cast<size_t>(n), 1);

    std::vector<int2> half_widths(static_cast<size_t>(n), int2{0});
    for (int i = 0; i < n; ++i) half_widths[size_t(i)] = int2{(wx[size_t(i)] - 1) / 2, (wy[size_t(i)] - 1) / 2};

    return box_chain(src, region, half_widths, progress);
}

Array2Df unsharp_masked(const Array2Df &src, const Box2i &region, float sigma, float amount, AtomicProgress progress)
{
    // The blur is all of the cost; adding the difference back is one pass over the region.
    const Array2Df blurred = gaussian_blurred(src, region, sigma, sigma, progress);
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

Array2Df median_filtered(const Array2Df &src, const Box2i &region, float radius, bool disc, AtomicProgress progress)
{
    const int2  extent = region.size();
    const int   r      = std::max(0, int(std::ceil(radius)));
    const float r2     = radius * radius;

    Array2Df out{extent};
    if (r == 0)
    {
        for (int y = 0; y < extent.y; ++y)
            for (int x = 0; x < extent.x; ++x) out(x, y) = clamped(src, region.min.x + x, region.min.y + y);
        return out;
    }

    progress.set_num_steps(extent.y);

    stp::parallel_for(stp::blocked_range<int>(0, extent.y, 1),
                      [&](int y0, int y1, int, int)
                      {
                          // Reused across the whole block rather than reallocated per sample.
                          std::vector<float> window;
                          window.reserve(size_t((2 * r + 1) * (2 * r + 1)));

                          for (int y = y0; y < y1; ++y)
                          {
                              if (progress.canceled())
                                  return;

                              for (int x = 0; x < extent.x; ++x)
                              {
                                  window.clear();
                                  for (int dy = -r; dy <= r; ++dy)
                                      for (int dx = -r; dx <= r; ++dx)
                                          if (!disc || float(dx * dx + dy * dy) <= r2)
                                              window.push_back(
                                                  clamped(src, region.min.x + x + dx, region.min.y + y + dy));

                                  // nth_element is enough -- only the middle value is wanted, not a sorted
                                  // window -- and is linear where a full sort is not.
                                  const size_t mid = window.size() / 2;
                                  std::nth_element(window.begin(), window.begin() + long(mid), window.end());
                                  out(x, y) = window[mid];
                              }

                              ++progress;
                          }
                      });

    return out;
}

Array2Df zapped_gremlins(const Array2Df &src, const Box2i &region, float replacement)
{
    const int2 extent = region.size();
    Array2Df   out{extent};

    const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
    stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                              for (int x = 0; x < extent.x; ++x)
                              {
                                  const int   sx = region.min.x + x, sy = region.min.y + y;
                                  const float v = clamped(src, sx, sy);
                                  if (std::isfinite(v))
                                  {
                                      out(x, y) = v;
                                      continue;
                                  }

                                  // The eight around it, skipping any that are gremlins themselves.
                                  std::array<float, 8> ring;
                                  int                  n = 0;
                                  for (int dy = -1; dy <= 1; ++dy)
                                      for (int dx = -1; dx <= 1; ++dx)
                                      {
                                          if (dx == 0 && dy == 0)
                                              continue;
                                          const float nv = clamped(src, sx + dx, sy + dy);
                                          if (std::isfinite(nv))
                                              ring[size_t(n++)] = nv;
                                      }

                                  if (n == 0)
                                  {
                                      // In the middle of a run of them there is nothing to agree with.
                                      out(x, y) = replacement;
                                      continue;
                                  }

                                  const size_t mid = size_t(n) / 2;
                                  std::nth_element(ring.begin(), ring.begin() + long(mid), ring.begin() + n);
                                  out(x, y) = ring[mid];
                              }
                      });

    return out;
}
