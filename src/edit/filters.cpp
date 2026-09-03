//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/filters.h"

#include "common.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <smallthreadpool.h>
#include <vector>

int wrap_coord(int p, int extent, int mode)
{
    if (p >= 0 && p < extent)
        return p;

    switch (mode)
    {
    case BorderMode_Edge: return std::clamp(p, 0, extent - 1);
    case BorderMode_Repeat: return mod(p, extent);
    case BorderMode_Mirror:
    {
        // the reflection has period twice the extent and folds in the middle of it, so -1 lands on 0 and
        // extent lands on extent-1
        const int period = 2 * extent;
        const int q      = mod(p, period);
        return q < extent ? q : period - 1 - q;
    }
    case BorderMode_Black:
    default: return -1;
    }
}

namespace
{

/// One sample of \p a read through the border modes; zero where they say there is nothing.
inline float bordered(const Array2Df &a, int x, int y, int mx, int my)
{
    x = wrap_coord(x, a.width(), mx);
    y = wrap_coord(y, a.height(), my);
    return (x < 0 || y < 0) ? 0.f : a(x, y);
}

/// One tap of the interpolating cubic with a = -0.75, as Photoshop's "bicubic".
/**
    A function, not a lambda: MSVC will not read a constexpr local from inside a captureless lambda.
*/
inline float cubic_weight(float d)
{
    constexpr float A = -0.75f;

    d = std::abs(d);
    return d <= 1.f ? ((A + 2.f) * d - (A + 3.f)) * d * d + 1.f : ((A * d - 5.f * A) * d + 8.f * A) * d - 4.f * A;
}

/// Value of \p a at the continuous position \p sx, \p sy, with samples at the centers of their cells.
float sample_at(const Array2Df &a, float sx, float sy, int sampler, int mx, int my)
{
    if (sampler == Sampler_Nearest)
        return bordered(a, int(std::floor(sx)), int(std::floor(sy)), mx, my);

    // shift so a sample sits at the center of its cell rather than at its corner
    sx -= 0.5f;
    sy -= 0.5f;

    const int   x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
    const float tx = sx - float(x0), ty = sy - float(y0);

    if (sampler == Sampler_Bilinear)
    {
        const float top = lerp(bordered(a, x0, y0, mx, my), bordered(a, x0 + 1, y0, mx, my), tx);
        const float bot = lerp(bordered(a, x0, y0 + 1, mx, my), bordered(a, x0 + 1, y0 + 1, mx, my), tx);
        return lerp(top, bot, ty);
    }

    float value = 0.f, total = 0.f;
    for (int j = -1; j <= 2; ++j)
    {
        const float wy = cubic_weight(ty - float(j));
        for (int i = -1; i <= 2; ++i)
        {
            const float w = cubic_weight(tx - float(i)) * wy;
            value += w * bordered(a, x0 + i, y0 + j, mx, my);
            total += w;
        }
    }
    // the taps sum to one analytically; dividing keeps that true against rounding
    return total != 0.f ? value / total : value;
}

/// Normalized 1D Gaussian taps, truncated at three standard deviations.
/**
    Past three sigma the taps carry well under a thousandth of the weight.
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
    // normalize the truncated taps so they sum to 1
    for (auto &t : taps) t /= sum;

    return taps;
}

/// Convolve \p region of \p src with the separable kernel \p taps_x by \p taps_y.
/**
    The horizontal pass covers the region's columns over rows grown by the vertical radius, which is the set
    the vertical pass then reads. An empty tap list along an axis leaves that axis alone.
*/
Array2Df convolve_separable(const Array2Df &src, const Box2i &region, const std::vector<float> &taps_x,
                            const std::vector<float> &taps_y, AtomicProgress progress)
{
    const int  rx     = taps_x.empty() ? 0 : int(taps_x.size() / 2);
    const int  ry     = taps_y.empty() ? 0 : int(taps_y.size() / 2);
    const int2 extent = region.size();

    // rows the vertical pass will reach for
    const int rows = extent.y + 2 * ry;

    Array2Df  horizontal{int2{extent.x, rows}};
    const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));

    // a pass each, so each reports over half of this filter's share
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
                                  // the region's row y sits at index y + ry in the taller intermediate
                                  for (int k = -ry; k <= ry; ++k)
                                      sum += taps_y[size_t(k + ry)] * horizontal(x, y + ry + k);
                                  out(x, y) = sum;
                              }
                              ++v_progress;
                          }
                      });

    return out;
}

/**
    One separable box pass, costing the same per sample whatever the half-widths: the sum over the box is
    carried along the row, then down the column, adding the sample entering it and subtracting the one
    leaving.

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

    // the intermediate was sized to what this reads, so index it directly: the region's row y sits at
    // index y + ry, and the box around it spans [y, y + 2*ry]
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

/// \p b grown by \p by on every side, then clipped to \p bounds.
/**
    The clip keeps a chain of passes agreeing with the same chain over the whole image, which clamps against
    the image's own edge.
*/
Box2i dilated(const Box2i &b, int2 by, const Box2i &bounds)
{
    Box2i out{b.min - by, b.max + by};
    out.intersect(bounds);
    return out;
}

/**
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

    Rounding that single width to an odd integer is not good enough: the error does not shrink as the count
    rises (at sigma 6 it is 5% at six passes and 15% at twelve), so raising \p n would change how much blur
    there is. Splitting the passes between the two adjacent odd widths and solving for how many take the
    smaller keeps the total within a few percent everywhere.
*/
std::vector<int> box_widths_for_sigma(float sigma, int n)
{
    const double s2 = double(sigma) * double(sigma);

    // width a single box would need if widths could be continuous. An odd width can be a centered box; an
    // even one would need a symmetric pair of off-centered boxes, so round down and let the split below
    // make up the difference.
    int lower = int(std::floor(std::sqrt(12.0 * s2 / n + 1.0)));
    if (lower % 2 == 0)
        --lower;
    lower = std::max(1, lower);

    // how many passes take `lower`; the rest take the next odd width up
    const double ideal = (12.0 * s2 - double(n) * lower * lower - 4.0 * n * lower - 3.0 * n) / (-4.0 * lower - 4.0);
    const int    count = std::clamp(int(std::lround(ideal)), 0, n);

    std::vector<int> widths(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) widths[size_t(i)] = i < count ? lower : lower + 2;
    return widths;
}

} // namespace

const char *border_mode_name(int mode)
{
    switch (mode)
    {
    case BorderMode_Black: return "Black";
    case BorderMode_Edge: return "Edge";
    case BorderMode_Repeat: return "Repeat";
    case BorderMode_Mirror: return "Mirror";
    default: return "";
    }
}

const char *sampler_name(int sampler)
{
    switch (sampler)
    {
    case Sampler_Nearest: return "Nearest neighbor";
    case Sampler_Bilinear: return "Bilinear";
    case Sampler_Bicubic: return "Bicubic";
    default: return "";
    }
}

Array2Df shifted(const Array2Df &src, const Box2i &region, float dx, float dy, int sampler, int border_x, int border_y)
{
    const int2 size = region.size();
    Array2Df   out{size};

    // a whole-sample offset needs no reconstruction, so shifting one way and back returns what was there
    const bool integral = dx == std::floor(dx) && dy == std::floor(dy);

    const int block_size = std::max(1, 1024 * 1024 / std::max(1, size.x));
    stp::parallel_for(
        stp::blocked_range<int>(0, size.y, block_size),
        [&](int y0, int y1, int, int)
        {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    // the sample that has to travel to here is the one that far back
                    const float sx = float(region.min.x + x) - dx;
                    const float sy = float(region.min.y + y) - dy;

                    out(x, y) = integral ? bordered(src, int(std::lround(sx)), int(std::lround(sy)), border_x, border_y)
                                         : sample_at(src, sx + 0.5f, sy + 0.5f, sampler, border_x, border_y);
                }
        });

    return out;
}

Array2Df gaussian_blurred(const Array2Df &src, const Box2i &region, float sigma_x, float sigma_y,
                          AtomicProgress progress)
{
    return convolve_separable(src, region, sigma_x > 0.f ? gaussian_taps(sigma_x) : std::vector<float>{},
                              sigma_y > 0.f ? gaussian_taps(sigma_y) : std::vector<float>{}, progress);
}

namespace
{

/// Run \p half_widths box passes in order, producing \p region.
/**
    Each pass reads its own half-width beyond what it produces, so the regions are worked out backwards from
    the one wanted and clipped at each step.
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

    // every pass costs the same, so they divide the share evenly
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

    // widths chosen so the combined variance lands on sigma; see box_widths_for_sigma()
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
    // the blur is all of the cost; adding the difference back is one pass over the region
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
                          // reused across the whole block rather than reallocated per sample
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

                                  // nth_element is linear, and only the middle value is wanted
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

                                  // the eight around it, skipping any that are gremlins themselves
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
                                      // in the middle of a run of them there is nothing to agree with
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
