//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <smallthreadpool.h>
#include <utility>

/// A result computed on a worker thread and cached against the settings it was computed for.
/**
    The main thread asks for a result with update(), which starts a computation when neither the cache nor
    the one under way matches, and reads it with get(), which takes a finished computation as the new cache.
    The cache keeps serving the previous result until then, so a reader never waits.

    `Result` provides:
    - `Settings settings` and `bool computed`, the settings it was computed for and whether it finished;
    - `Settings::match(const Settings &)`, true when two settings describe the same result;
    - `Settings::take_content_versions(const Settings &)`, copying the other settings' versions of the
      pixels, for the streaming throttle described at update().
*/
template <typename Result>
class AsyncCache
{
public:
    using Settings = typename Result::Settings;

    AsyncCache() : m_cached(std::make_shared<Result>()), m_async(std::make_shared<Result>()) {}
    AsyncCache(const AsyncCache &)            = delete;
    AsyncCache &operator=(const AsyncCache &) = delete;
    AsyncCache(AsyncCache &&) noexcept        = default;
    AsyncCache &operator=(AsyncCache &&)      = default;
    ~AsyncCache() { cancel(); }

    /// The newest finished result, or an uncomputed one before any has finished; never null.
    Result *get()
    {
        if (m_tracker.ready() && m_async->computed)
            adopt();
        return m_cached.get();
    }

    /// Bumped each time a finished computation becomes the cache.
    int generation() const { return m_generation; }

    /// Make sure a result for \p desired is cached or being computed.
    /**
        Pixels streaming in from a renderer change the content version faster than a full pass can finish,
        and chasing every version would cancel each computation partway. So a newer version is only picked
        up once the cache has stood as a finished result for a moment.

        \param compute  Called on a worker thread as `compute(Result &, const Settings &, std::atomic<bool>
                        &canceled)`; it fills in the result and sets `computed` unless canceled
    */
    template <typename Compute>
    void update(Settings desired, Compute &&compute)
    {
        static constexpr auto k_min_age = std::chrono::milliseconds{200};
        if (m_cached->computed && std::chrono::steady_clock::now() - m_ready_at < k_min_age)
            desired.take_content_versions(m_cached->settings);

        if (m_cached->computed && m_cached->settings.match(desired))
            return;

        // nothing under way for these settings, or it was canceled
        if (!m_async_settings.match(desired) || (m_tracker.ready() && !m_async->computed))
            return start(desired, std::forward<Compute>(compute));

        if (m_tracker.ready() && m_async->computed)
        {
            adopt();
            if (!m_cached->settings.match(desired))
                start(desired, std::forward<Compute>(compute));
        }
    }

    /// Stop any computation under way and wait for it to unwind.
    void cancel()
    {
        if (m_canceled)
            m_canceled->store(true);
        m_tracker.wait();
    }

private:
    template <typename Compute>
    void start(const Settings &desired, Compute &&compute)
    {
        cancel();
        m_canceled = std::make_shared<std::atomic<bool>>(false);
        m_tracker  = stp::do_async(
            [result = m_async, desired, canceled = m_canceled, compute = std::forward<Compute>(compute)]() mutable
            { compute(*result, desired, *canceled); });
        m_async_settings = desired;
    }

    /// Take the finished computation as the cache and start a fresh slot for the next one.
    void adopt()
    {
        m_cached   = std::exchange(m_async, std::make_shared<Result>());
        m_ready_at = std::chrono::steady_clock::now();
        ++m_generation;
    }

    std::shared_ptr<Result>               m_cached;
    std::shared_ptr<Result>               m_async;
    Settings                              m_async_settings{};
    stp::ThreadPool::TaskTracker          m_tracker;
    std::shared_ptr<std::atomic<bool>>    m_canceled; ///< Shared with the task; a bare atomic cannot be moved
    std::chrono::steady_clock::time_point m_ready_at{};
    int                                   m_generation = 0;
};
