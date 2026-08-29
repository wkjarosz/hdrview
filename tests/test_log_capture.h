/** \file test_log_capture.h
    \author Wojciech Jarosz

    Collects everything logged while it is in scope.

    Some guards have no other observable effect. Refusing to extract a zip entry that lies about its size
    and failing to extract it look identical from outside -- same empty result, same absent image -- and
    differ only in the memory and time spent finding out. Asserting on the returned value there passes
    whether or not the guard exists; the warning is what tells the two apart.

    Shared by the doctest and GUI suites, which both need it for exactly that reason.
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

    //! Whether anything logged so far contains `substring`.
    bool saw(const std::string &substring) const
    {
        std::lock_guard<std::mutex> lock(m_sink->mutex_);
        return std::any_of(m_sink->messages.begin(), m_sink->messages.end(),
                           [&](const std::string &m) { return m.find(substring) != std::string::npos; });
    }

private:
    struct Sink : spdlog::sinks::base_sink<std::mutex>
    {
        std::vector<std::string> messages;
        std::mutex               mutex_;

        void sink_it_(const spdlog::details::log_msg &msg) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            messages.emplace_back(msg.payload.data(), msg.payload.size());
        }
        void flush_() override {}
    };

    std::shared_ptr<Sink> m_sink = std::make_shared<Sink>();
};
