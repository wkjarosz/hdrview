#pragma once

#include <atomic>
#include <chrono>
#include <mutex>

// Fires exactly once per call site, independent of message content.
// Usage: if (static LogOnce guard; guard) spdlog::warn(...);
class LogOnce
{
public:
    explicit operator bool() { return !m_fired.exchange(true, std::memory_order_relaxed); }

private:
    std::atomic<bool> m_fired{false};
};

// Fires at most once per interval, independent of message content.
// Usage: if (static LogThrottle guard{std::chrono::seconds(5)}; guard) spdlog::warn(...);
class LogThrottle
{
public:
    explicit LogThrottle(std::chrono::steady_clock::duration interval) : m_interval(interval) {}

    explicit operator bool()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        now = std::chrono::steady_clock::now();
        if (now - m_last < m_interval)
            return false;
        m_last = now;
        return true;
    }

private:
    std::mutex                            m_mutex;
    std::chrono::steady_clock::duration   m_interval;
    std::chrono::steady_clock::time_point m_last{};
};
