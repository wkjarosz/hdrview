//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

#include "progress.h"
#include "scheduler.h"
#include <functional>
#include <spdlog/spdlog.h>

template <typename T>
class AsyncTask
{
public:
    using TaskFunc           = std::function<T(std::atomic<bool> &canceled)>;
    using NoProgressTaskFunc = std::function<T()>;

    /*!
     * Create an asynchronous task that can report back on its progress
     * @param compute The function to execute asynchronously
     */
    AsyncTask(TaskFunc f, Scheduler *s = nullptr) :
        m_state(std::make_shared<SharedState>([f](std::atomic<bool> &canceled) { return f(canceled); }, s))
    {
    }

    /*!
     * Create an asynchronous task without progress updates
     * @param compute The function to execute asynchronously
     */
    AsyncTask(NoProgressTaskFunc f, Scheduler *s = nullptr) :
        m_state(std::make_shared<SharedState>([f](std::atomic<bool> &) { return f(); }, s))
    {
    }

    ~AsyncTask()
    {
        cancel();
        // no need to wait, the task continues running and the scheduler will clean it up
    }

    /*!
     * Start the computation (if it hasn't already been started)
     */
    void compute()
    {
        if (m_state->started)
            return;

        struct Payload
        {
            std::shared_ptr<SharedState> state;
        };

        auto callback = [](int, int, void *payload)
        {
            auto &state  = ((Payload *)payload)->state;
            state->value = state->f(state->canceled);
        };
        auto deleter = [](int, int, void *payload) { delete (Payload *)payload; };

        Payload *payload = new Payload{m_state};

        auto pool        = m_state->threadpool ? m_state->threadpool : Scheduler::singleton();
        m_state->task    = pool->parallelize_async(1, payload, callback, deleter);
        m_state->started = true;
    }

    /*!
     * Waits until the task has finished, and returns the result.
     * The tasks return value is cached, so get can be called multiple times.
     *
     * @return	The result of the computation
     */
    T &get()
    {
        m_state->task.wait();
        return m_state->value;
    }

    /*!
     * Query the progress of the task.
     *
     * @return The percentage done, ranging from 0.f to 100.f,
     * or -1 to indicate busy if the task doesn't report back progress
     */
    // float progress() const { return m_progress.progress(); }

    // void set_progress(float p) { m_progress.reset_progress(p); }

    /// Query whether the task is canceled.
    bool canceled() const { return m_state->canceled; }

    void cancel()
    {
        spdlog::trace("Canceling async computation");
        m_state->canceled = true;
    }

    /*!
     * @return true if the computation has finished
     */
    bool ready() const { return m_state->started && m_state->task.ready(); }

private:
    struct SharedState
    {
        TaskFunc               f;
        Scheduler             *threadpool{nullptr};
        std::atomic<bool>      canceled{false};
        bool                   started{false};
        Scheduler::TaskTracker task;
        T                      value;

        SharedState(TaskFunc fn, Scheduler *s) : f(fn), threadpool(s), canceled(false), started(false), task(), value()
        {
        }
    };
    std::shared_ptr<SharedState> m_state; //!< Shared state between this AsyncTask and the Scheduler executing the task
};