//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

/*!
    How far a long operation has got, and whether it should stop.

    Passed by value into anything slow enough to be worth watching. Copies share one accumulator, so a
    filter reports in terms of its own work and never has to know what is running alongside it:

        FilterProgress progress{true};   // the caller, who will read it
        progress.set_num_steps(rows);
        for (each row) { if (progress.canceled()) return; ...; ++progress; }

    A *share* of a job is another instance rather than a change to this one:

        FilterProgress first_pass{progress, 0.5f};   // reports into the first half
        FilterProgress second_pass{progress, 0.5f};  // and this into the second

    Each contributes its own share of the total, so nothing has to be saved and put back, and two of them
    can be alive at once without interfering -- which a single object carrying a current offset could not
    manage.

    A default-constructed one carries no state: every operation is a no-op and canceled() is false. That is
    what a caller who does not care passes, and it is why the filters take this by value with a default
    argument and never test it for null.

    Adapted from HDRView 1.8's AtomicProgress, which had the same shape.
*/
class FilterProgress
{
public:
    //! Stateless by default; \p create_state for the one instance that owns the accumulator.
    explicit FilterProgress(bool create_state = false, float share_of_total = 1.f) :
        m_share(share_of_total), m_state(create_state ? std::make_shared<State>() : nullptr)
    {
        set_num_steps(1);
    }

    //! A share of \p parent's own share, reporting into the same total.
    FilterProgress(const FilterProgress &parent, float share_of_parent) :
        m_share(parent.m_share * share_of_parent), m_state(parent.m_state)
    {
        set_num_steps(1);
    }

    /// Fraction of the whole job done, in [0, 1]; 0 when there is no state to report into.
    float progress() const
    {
        return m_state ? float(double(m_state->ticks.load(std::memory_order_relaxed)) / double(k_ticks)) : 0.f;
    }

    bool canceled() const { return m_state && m_state->canceled.load(std::memory_order_relaxed); }
    void cancel()
    {
        if (m_state)
            m_state->canceled.store(true, std::memory_order_relaxed);
    }

    /// Force the whole job to read as finished, whatever the increments have added up to.
    void set_done()
    {
        if (m_state)
            m_state->ticks.store(k_ticks, std::memory_order_relaxed);
    }

    //! How many increments make up this instance's share. Set before the loop that reports.
    void set_num_steps(int num_steps)
    {
        m_step_ticks = num_steps > 0 ? std::llround(double(m_share) * double(k_ticks) / double(num_steps)) : 0;
    }

    FilterProgress &operator+=(int steps)
    {
        if (m_state && m_step_ticks)
            m_state->ticks.fetch_add(int64_t(steps) * m_step_ticks, std::memory_order_relaxed);
        return *this;
    }
    FilterProgress &operator++() { return *this += 1; }

private:
    /*!
        Progress is counted in integer ticks rather than kept as a float.

        Adding to it happens from several worker threads at once, and C++17's std::atomic<float> has no
        fetch_add -- only integers do. The scale is fine enough that the rounding in set_num_steps() stays
        far below anything a progress bar could show.
    */
    static constexpr int64_t k_ticks = int64_t(1) << 24;

    struct State
    {
        std::atomic<int64_t> ticks{0};
        std::atomic<bool>    canceled{false};
    };

    float                  m_share      = 1.f;
    int64_t                m_step_ticks = 0;
    std::shared_ptr<State> m_state;
};
