#pragma once

#include <spdlog/details/circular_q.h>
#include <spdlog/details/log_msg_buffer.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include <atomic>
#include <mutex>

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

protected:
    void sink_it_(const details::log_msg &msg) override
    {
        memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);

        q_.push_back({SPDLOG_BUF_TO_STRING(formatted), msg.level, msg.color_range_start, msg.color_range_end});
        has_new_items_ = true;
    }
    void flush_() override {}

private:
    size_t                       max_items_ = 0;
    details::circular_q<LogItem> q_;
    std::atomic<bool>            has_new_items_ = false;
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
