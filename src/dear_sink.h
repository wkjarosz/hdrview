#pragma once

#include <atomic>
#include <imgui.h>
#include <mutex>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace spdlog
{
namespace sinks
{

template <typename Mutex>
class dear_sink : public spdlog::sinks::base_sink<Mutex>
{
public:
    struct LogItem
    {
        std::string               message;
        spdlog::level::level_enum level;
        size_t                    color_range_start;
        size_t                    color_range_end;
    };

    dear_sink(int max_lines = 1024, bool dark_colors = false) : messages_(max_lines)
    {
        enum : ImU32
        {
            white       = IM_COL32(0xff, 0xff, 0xff, 0xff),
            black       = IM_COL32(0x00, 0x00, 0x00, 0xff),
            red         = IM_COL32(0xff, 0x00, 0x00, 0xff),
            darkRed     = IM_COL32(0x80, 0x00, 0x00, 0xff),
            green       = IM_COL32(0x00, 0xff, 0x00, 0xff),
            darkGreen   = IM_COL32(0x00, 0x80, 0x00, 0xff),
            blue        = IM_COL32(0x00, 0x00, 0xff, 0xff),
            darkBlue    = IM_COL32(0x00, 0x00, 0x80, 0xff),
            cyan        = IM_COL32(0x00, 0xff, 0xff, 0xff),
            darkCyan    = IM_COL32(0x00, 0x80, 0x80, 0xff),
            magenta     = IM_COL32(0xff, 0x00, 0xff, 0xff),
            darkMagenta = IM_COL32(0x80, 0x00, 0x80, 0xff),
            yellow      = IM_COL32(0xff, 0xff, 0x00, 0xff),
            darkYellow  = IM_COL32(0x80, 0x80, 0x00, 0xff),
            gray        = IM_COL32(0xa0, 0xa0, 0xa4, 0xff),
            darkGray    = IM_COL32(0x80, 0x80, 0x80, 0xff),
            lightGray   = IM_COL32(0xc0, 0xc0, 0xc0, 0xff),
        };

        default_color_                      = dark_colors ? darkGray : lightGray;
        colors_.at(spdlog::level::trace)    = dark_colors ? darkGray : gray;
        colors_.at(spdlog::level::debug)    = dark_colors ? darkCyan : cyan;
        colors_.at(spdlog::level::info)     = dark_colors ? darkGreen : green;
        colors_.at(spdlog::level::warn)     = dark_colors ? darkYellow : yellow;
        colors_.at(spdlog::level::err)      = red;
        colors_.at(spdlog::level::critical) = red;
        colors_.at(spdlog::level::off)      = dark_colors ? lightGray : darkGray;
    }

    ~dear_sink() { flush_(); }

    void set_default_color(ImU32 color)
    {
        // std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        default_color_ = color;
    }

    void set_level_color(spdlog::level::level_enum color_level, ImU32 color)
    {
        // std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        colors_.at(static_cast<size_t>(color_level)) = color;
    }

    ImU32 get_level_color(spdlog::level::level_enum color_level) const
    {
        // std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        return colors_.at(static_cast<size_t>(color_level));
    }

    ImU32 get_default_color() const
    {
        // std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        return default_color_;
    }

    void iterate(const std::function<bool(const LogItem &msg)> &iterator)
    {
        std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        for (size_t i = 0; i < messages_.size(); ++i)
            if (!iterator(messages_[i]))
                break;
    }

    void clear_messages()
    {
        std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        messages_.clear();
    }

    bool has_new_items() { return has_new_items_.exchange(false); }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

        messages_.push_back({fmt::to_string(formatted), msg.level, msg.color_range_start, msg.color_range_end});
        has_new_items_ = true;
    }
    void flush_() override {}

private:
    template <typename T>
    class CircularBuffer
    {
    private:
        std::vector<T> m_buffer;
        size_t         m_head; // Points to the oldest element
        size_t         m_size;

    public:
        CircularBuffer(size_t capacity = 0) : m_buffer(capacity), m_head(0), m_size(0) {}
        size_t size() const { return m_size; }
        size_t capacity() const { return m_buffer.size(); }
        bool   empty() const { return size() == 0; }
        bool   full() const { return size() == capacity(); }
        void   clear()
        {
            m_head = 0;
            m_size = 0;
        }

        void push_back(const T &value)
        {
            if (full())
                // Buffer is full, overwrite the oldest element
                m_head = (m_head + 1) % capacity();
            else
                ++m_size;

            // Calculate the index where the new element will be inserted
            size_t index    = (m_head + m_size - 1) % capacity();
            m_buffer[index] = value;
        }

        const T &operator[](size_t index) const
        {
            if (index >= m_size)
                throw std::out_of_range("Index out of range");

            // Calculate the index of the element based on its age
            size_t bufferIndex = (m_head + index) % capacity();
            return m_buffer[bufferIndex];
        }
    };

    CircularBuffer<LogItem>                    messages_;
    std::atomic<bool>                          has_new_items_ = false;
    ImU32                                      default_color_;
    std::array<ImU32, spdlog::level::n_levels> colors_;
};

using dear_sink_mt = dear_sink<std::mutex>;
using dear_sink_st = dear_sink<spdlog::details::null_mutex>;

} // namespace sinks

//
// Factory functions
//

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> dear_logger_mt(const std::string &logger_name, int max_lines = 1024,
                                              bool dark_colors = false)
{
    return Factory::template create<sinks::dear_sink_mt>(logger_name, max_lines, dark_colors);
}

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> dear_logger_st(const std::string &logger_name, int max_lines = 1024,
                                              bool dark_colors = false)
{
    return Factory::template create<sinks::dear_sink_st>(logger_name, max_lines, dark_colors);
}

} // namespace spdlog
