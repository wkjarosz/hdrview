#pragma once

#include <spdlog/details/circular_q.h>
#include <spdlog/details/log_msg_buffer.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace spdlog
{
namespace sinks
{

// Like ringbuffer_sink, but gives access to the color range
template <typename Mutex>
class ringbuffer_color_sink : public base_sink<Mutex>
{
public:
    struct LogItem
    {
        std::string       message;
        level::level_enum level;
        size_t            color_range_start;
        size_t            color_range_end;
        // Monotonic id, stable across the circular buffer wrapping around, so a consumer can locate
        // this exact item again later (e.g. to scroll to it) even after other items have been pushed.
        uint64_t seq;
    };

    // Severity and text of the highest-severity message pushed since the last mark_badge_seen(), plus
    // how many messages (of any severity) arrived in that span.
    struct BadgeState
    {
        level::level_enum level;
        uint64_t          seq;
        std::string       message;
        size_t            count;
    };

    explicit ringbuffer_color_sink(int max_items = 1024) : max_items_(max_items), q_(max_items) {}

    ~ringbuffer_color_sink() { flush_(); }

    void iterate(const std::function<bool(const LogItem &msg)> &iterator)
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        for (size_t i = 0; i < q_.size(); ++i)
            if (!iterator(q_.at(i)))
                break;
    }

    void clear_messages()
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        q_ = details::circular_q<LogItem>{max_items_};
    }

    // returns true if there are new logged items since the last time this function was called
    bool has_new_items() { return has_new_items_.exchange(false); }

    BadgeState badge_state()
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        size_t                 count = 0;
        for (auto c : badge_counts_) count += c;
        return {badge_level_, badge_seq_, badge_message_, count};
    }

    void mark_badge_seen()
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        badge_level_ = level::off;
        badge_seq_   = 0;
        badge_message_.clear();
        badge_counts_.fill(0);
    }

protected:
    void sink_it_(const details::log_msg &msg) override
    {
        memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);

        uint64_t seq = next_seq_++;
        q_.push_back({SPDLOG_BUF_TO_STRING(formatted), msg.level, msg.color_range_start, msg.color_range_end, seq});
        has_new_items_ = true;

        if (msg.level < level::off)
        {
            badge_counts_[msg.level]++;
            // badge_level_ == off means nothing has been recorded since the last mark_badge_seen();
            // otherwise only a message at least as severe as what's already showing takes over, so ties
            // go to the most recent message at the current highest severity. Tracking every level (not
            // just warn+) means there's always something to show once anything has been logged.
            if (badge_level_ == level::off || msg.level >= badge_level_)
            {
                badge_level_   = msg.level;
                badge_seq_     = seq;
                badge_message_ = std::string(msg.payload.data(), msg.payload.size());
            }
        }
    }
    void flush_() override {}

private:
    size_t                       max_items_ = 0;
    details::circular_q<LogItem> q_;
    std::atomic<bool>            has_new_items_ = false;
    uint64_t                     next_seq_      = 0;

    // Bookkeeping for badge_state()/mark_badge_seen(), guarded by base_sink<Mutex>::mutex_ (already
    // held across sink_it_ and the two methods above).
    level::level_enum                           badge_level_ = level::off;
    uint64_t                                    badge_seq_   = 0;
    std::string                                 badge_message_;
    std::array<size_t, spdlog::level::n_levels> badge_counts_{}; // indexed directly by level
};

using ringbuffer_color_sink_mt = ringbuffer_color_sink<std::mutex>;
using ringbuffer_color_sink_st = ringbuffer_color_sink<details::null_mutex>;

} // namespace sinks

//
// Factory functions
//

template <typename Factory = synchronous_factory>
inline std::shared_ptr<logger> dear_logger_mt(const std::string &logger_name, int max_items = 1024)
{
    return Factory::template create<sinks::ringbuffer_color_sink_mt>(logger_name, max_items);
}

template <typename Factory = synchronous_factory>
inline std::shared_ptr<logger> dear_logger_st(const std::string &logger_name, int max_items = 1024)
{
    return Factory::template create<sinks::ringbuffer_color_sink_st>(logger_name, max_items);
}

} // namespace spdlog
