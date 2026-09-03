//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/poisson.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <smallthreadpool.h>
#include <vector>

namespace
{

/// Inner product of \p a and \p b.
/**
    In double: the step lengths are ratios of two of these, and a float sum over a large region loses enough
    of the tail to stall the descent.
*/
double dot(const Array2Df &a, const Array2Df &b)
{
    double sum = 0.0;
    for (int i = 0; i < a.num_elements(); ++i) sum += double(a(i)) * double(b(i));
    return sum;
}

} // namespace

Array2Df laplacian(const Array2Df &src)
{
    Array2Df out{src.size()};

    const int block_size = std::max(1, 1024 * 1024 / std::max(1, src.width()));
    stp::parallel_for(stp::blocked_range<int>(0, src.height(), block_size),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                              for (int x = 0; x < src.width(); ++x)
                                  out(x, y) = src.clamped(x - 1, y) + src.clamped(x + 1, y) + src.clamped(x, y - 1) +
                                              src.clamped(x, y + 1) + src.clamped(x - 1, y - 1) +
                                              src.clamped(x + 1, y + 1) + src.clamped(x + 1, y - 1) +
                                              src.clamped(x - 1, y + 1) - 8.f * src.clamped(x, y);
                      });

    return out;
}

Array2Df poisson_blended(const Array2Df &background, const Array2Df &source, const Array2Df &mask, int iterations,
                         float tolerance, AtomicProgress progress)
{
    Array2Df x = background;
    if (background.size() != source.size() || background.size() != mask.size())
    {
        progress.finish_share();
        return x;
    }

    const int n = x.num_elements();

    // which samples are being solved for; everything else keeps the background's value throughout
    std::vector<uint8_t> inside(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) inside[size_t(i)] = mask(i) > 1e-5f ? 1 : 0;

    auto restrict_to_mask = [&](Array2Df &a)
    {
        for (int i = 0; i < n; ++i)
            if (!inside[size_t(i)])
                a(i) = 0.f;
    };

    // what the interior's Laplacian should come out as
    const Array2Df b = laplacian(source);

    // r = (b - A x) inside the mask, starting from the background, which is already the answer outside
    Array2Df r = laplacian(x);
    for (int i = 0; i < n; ++i) r(i) = b(i) - r(i);
    restrict_to_mask(r);

    Array2Df d = r;

    double rTr = dot(r, r);
    if (rTr <= 0.0)
    {
        // already solved; still report this channel's share of the progress
        progress.finish_share();
        return x;
    }

    // relative to where the residual started, so the bound is scale-independent
    const double target = tolerance * tolerance * rTr;

    progress.set_num_steps(iterations);

    for (int iter = 0; iter < iterations; ++iter)
    {
        if (progress.canceled())
            return x;

        // d is zero outside the mask, so this is the system's operator applied to it
        Array2Df Ad = laplacian(d);
        restrict_to_mask(Ad);

        const double dTAd = dot(d, Ad);
        if (std::abs(dTAd) < 1e-30)
            break;

        const double alpha = rTr / dTAd;

        for (int i = 0; i < n; ++i)
            if (inside[size_t(i)])
            {
                x(i) = float(double(x(i)) + alpha * double(d(i)));
                r(i) = float(double(r(i)) - alpha * double(Ad(i)));
            }

        const double rTr_next = dot(r, r);
        ++progress;

        // stop once the residual has fallen far enough
        if (rTr_next <= target)
            break;

        const double beta = rTr_next / rTr;
        for (int i = 0; i < n; ++i) d(i) = float(double(r(i)) + beta * double(d(i)));
        restrict_to_mask(d);

        rTr = rTr_next;
    }

    // only this channel's share of the bar, since the solve likely stopped inside the iteration bound
    progress.finish_share();
    return x;
}
