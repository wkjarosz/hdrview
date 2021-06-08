//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

/*!
 * A fixed-point fractional number stored using an std::atomic
 */
template <typename Fixed, typename BigFixed, int FractionBits>
class AtomicFixed
{
public:
    static const Fixed ScalingFactor = (1 << FractionBits);

    static Fixed float2fixed(float b) { return std::round(b * ScalingFactor); }

    static float fixed2float(Fixed f) { return float(f) / ScalingFactor; }

    std::atomic<Fixed> f;

    AtomicFixed() = default;

    explicit AtomicFixed(float d) : f(float2fixed(d))
    {
        // empty
    }

    explicit operator float() const { return fixed2float(f); }

    Fixed operator=(float b) { return (f = float2fixed(b)); }

    Fixed operator+=(float b) { return (f += float2fixed(b)); }

    Fixed operator-=(float b) { return (f -= float2fixed(b)); }

    // Disabling to avoid accidental non-atomic operations
    // /// This operator is *NOT* atomic
    // Fixed operator*=(float b)
    // {
    // 	return (f = Fixed(BigFixed(f) * BigFixed(float2fixed(b))) / ScalingFactor);
    // }

    // /// This operator is *NOT* atomic
    // Fixed operator/=(float b)
    // {
    // 	return (f = Fixed((BigFixed(f) * ScalingFactor) / float2fixed(b)));
    // }

    bool operator<(float b) const { return f < float2fixed(b); }

    bool operator<=(float b) const { return f <= float2fixed(b); }

    bool operator>(float b) const { return f > float2fixed(b); }

    bool operator>=(float b) const { return f >= float2fixed(b); }

    bool operator==(float b) const { return f == float2fixed(b); }

    bool operator!=(float b) const { return f != float2fixed(b); }
};

using AtomicFixed16 = AtomicFixed<std::int16_t, std::int32_t, 8>;
using AtomicFixed32 = AtomicFixed<std::int32_t, std::int64_t, 16>;

/*!
 * Helper object to manage the progress display.
 * 	{
 *   	AtomicProgress p1(true);
 *   	p1.set_num_steps(10);
 *   	for (int i = 0; i < 10; ++i, ++p1)
 *   	{
 *     		// do something
 *   	}
 * 	} // end progress p1
 *
 */
class AtomicProgress
{
public:
    using AtomicPercent32 = AtomicFixed<std::int32_t, std::int64_t, 30>;

    explicit AtomicProgress(bool createState = false, float totalPercentage = 1.f);
    AtomicProgress(const AtomicProgress &parent, float percentageOfParent = 1.f);

    // access to the atomic internal storage
    void  reset_progress(float p = 0.f);
    float progress() const;
    void  set_done() { reset_progress(1.f); }
    void  set_busy() { reset_progress(-1.f); }
    bool  canceled() const;
    void  cancel();

    // access to the discrete stepping
    void            set_available_percent(float percent);
    void            set_num_steps(int numSteps);
    AtomicProgress &operator+=(int steps);
    AtomicProgress &operator++() { return ((*this) += 1); }

private:
    int   m_num_steps;
    float m_percentage_of_parent, m_step_percent;
    struct State
    {
        State() : progress(0.f), canceled(false) {}

        AtomicPercent32 progress; ///< Atomic internal state of progress

        // theoretically this should be guarded by an std::atomic,
        // but on any reasonable architecture a bool will be atomic
        bool canceled; ///< Flag set if the calling code wants to cancel the associated task
    };

    std::shared_ptr<State> m_state;
};