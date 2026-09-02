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

/**
    Helper object to manage the progress display.

        AtomicProgress p(true);
        p.set_num_steps(10);
        for (int i = 0; i < 10; ++i, ++p) { ... }

    A share of a job is another instance holding the same state, each contributing its own fraction of the
    total, so several can be alive at once:

        AtomicProgress first_pass{p, 0.5f};   // reports into the first half
        AtomicProgress second_pass{p, 0.5f};  // and this into the second
*/
class AtomicProgress
{
public:
    /// No state: every operation is a no-op and canceled() is false.
    /**
        Must stay a separate non-explicit constructor so that a `= {}` default argument compiles on clang.
    */
    AtomicProgress() { set_num_steps(1); }

    /// \p create_state for the one instance that owns the accumulator everything reports into.
    explicit AtomicProgress(bool create_state, float share_of_total = 1.f) :
        m_share(share_of_total), m_state(create_state ? std::make_shared<State>() : nullptr)
    {
        set_num_steps(1);
    }

    /// A share of \p parent's own share, reporting into the same total.
    AtomicProgress(const AtomicProgress &parent, float share_of_parent) :
        m_share(parent.m_share * share_of_parent), m_state(parent.m_state)
    {
        set_num_steps(1);
    }

    /// Written out because m_taken is atomic, and these are passed and returned by value throughout.
    AtomicProgress(const AtomicProgress &o) :
        m_share(o.m_share), m_step_ticks(o.m_step_ticks), m_taken(o.m_taken.load(std::memory_order_relaxed)),
        m_state(o.m_state)
    {
    }
    AtomicProgress &operator=(const AtomicProgress &o)
    {
        m_share      = o.m_share;
        m_step_ticks = o.m_step_ticks;
        m_taken.store(o.m_taken.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_state = o.m_state;
        return *this;
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

    /// Force the whole job to read as finished.
    /**
        Only for the instance that owns the total; a share must use finish_share() instead.
    */
    void set_done()
    {
        if (m_state)
            m_state->ticks.store(k_ticks, std::memory_order_relaxed);
    }

    /// Hand in whatever is left of this instance's share, leaving the rest of the total alone.
    /**
        This is what a reporter that stopped early owes, so a bar driven by several such loops still
        reaches the end.
    */
    void finish_share()
    {
        if (!m_state)
            return;

        const int64_t total = std::llround(double(m_share) * double(k_ticks));
        const int64_t taken = m_taken.exchange(total, std::memory_order_relaxed);
        if (taken < total)
            m_state->ticks.fetch_add(total - taken, std::memory_order_relaxed);
    }

    /// How many increments make up this instance's share. Set before the loop that reports.
    void set_num_steps(int num_steps)
    {
        m_step_ticks = num_steps > 0 ? std::llround(double(m_share) * double(k_ticks) / double(num_steps)) : 0;
    }

    AtomicProgress &operator+=(int steps)
    {
        if (m_state && m_step_ticks)
        {
            const int64_t ticks = int64_t(steps) * m_step_ticks;
            m_state->ticks.fetch_add(ticks, std::memory_order_relaxed);
            m_taken.fetch_add(ticks, std::memory_order_relaxed);
        }
        return *this;
    }
    AtomicProgress &operator++() { return *this += 1; }

private:
    /// Fixed-point fraction: C++17's std::atomic<float> has no fetch_add, and several threads add at once.
    /**
        Fine enough that set_num_steps()'s rounding is invisible.
    */
    static constexpr int64_t k_ticks = int64_t(1) << 24;

    struct State
    {
        std::atomic<int64_t> ticks{0}; ///< Atomic internal state of progress
        /// Flag set if the calling code wants to cancel the associated task
        std::atomic<bool> canceled{false};
    };

    float   m_share      = 1.f;
    int64_t m_step_ticks = 0;
    /// Ticks this instance has reported, so finish_share() knows what remains.
    /**
        Atomic because one instance is shared across the threads of a parallel loop.
    */
    std::atomic<int64_t>   m_taken{0};
    std::shared_ptr<State> m_state;
};
