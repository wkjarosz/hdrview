/** \file test_log_capture.h
    \author Wojciech Jarosz

    Captures spdlog output for the duration of a scope, for guards whose only visible effect is a warning.
*/

#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

class LogCapture
{
public:
    LogCapture() { spdlog::default_logger()->sinks().push_back(m_sink); }
    ~LogCapture()
    {
        auto &sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    LogCapture(const LogCapture &)            = delete;
    LogCapture &operator=(const LogCapture &) = delete;

    /// Whether anything logged so far contains `substring`.
    bool saw(const std::string &substring) const
    {
        std::lock_guard<std::mutex> lock(m_sink->mutex_);
        return std::any_of(m_sink->messages.begin(), m_sink->messages.end(),
                           [&](const std::string &m) { return m.find(substring) != std::string::npos; });
    }

    /// How many warnings and errors have been logged so far.
    int warnings() const
    {
        std::lock_guard<std::mutex> lock(m_sink->mutex_);
        return m_sink->num_warnings;
    }

private:
    struct Sink : spdlog::sinks::base_sink<std::mutex>
    {
        std::vector<std::string> messages;
        int                      num_warnings = 0;
        mutable std::mutex       mutex_;

        void sink_it_(const spdlog::details::log_msg &msg) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            messages.emplace_back(msg.payload.data(), msg.payload.size());
            if (msg.level >= spdlog::level::warn)
                ++num_warnings;
        }
        void flush_() override {}
    };

    std::shared_ptr<Sink> m_sink = std::make_shared<Sink>();
};
