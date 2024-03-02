//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

#include "progress.h"
#include "scheduler.h"
#include <functional>

template <typename T>
class AsyncTask
{
public:
    using TaskFunc           = std::function<T(AtomicProgress &progress)>;
    using NoProgressTaskFunc = std::function<T()>;

    /*!
     * Create an asynchronous task that can report back on its progress
     * @param compute The function to execute asynchronously
     */
    AsyncTask(TaskFunc f, Scheduler *s = nullptr) :
        m_func(
            [f](AtomicProgress &prog)
            {
                T ret = f(prog);
                prog.set_done();
                return ret;
            }),
        m_progress(true), m_threadpool(s)
    {
    }

    /*!
     * Create an asynchronous task without progress updates
     * @param compute The function to execute asynchronously
     */
    AsyncTask(NoProgressTaskFunc f, Scheduler *s = nullptr) :
        m_func([f](AtomicProgress &) { return f(); }), m_progress(false), m_threadpool(s)
    {
    }

    /*!
     * Start the computation (if it hasn't already been started)
     */
    void compute()
    {
        // start only if not done and not already started
        if (!m_started)
        {
            auto callback = [](int, int, void *payload)
            {
                AsyncTask *self = (AsyncTask *)payload;
                self->m_value   = self->m_func(self->m_progress);
            };

            auto pool = m_threadpool ? m_threadpool : Scheduler::singleton();
            m_task    = pool->parallelizeAsync(1, this, callback);

            m_started = true;
        }
    }

    /*!
     * Waits until the task has finished, and returns the result.
     * The tasks return value is cached, so get can be called multiple times.
     *
     * @return	The result of the computation
     */
    T &get()
    {
        m_task.wait();
        return m_value;
    }

    /*!
     * Query the progress of the task.
     *
     * @return The percentage done, ranging from 0.f to 100.f,
     * or -1 to indicate busy if the task doesn't report back progress
     */
    float progress() const { return m_progress.progress(); }

    void set_progress(float p) { m_progress.reset_progress(p); }

    /// Query whether the task is canceled.
    bool canceled() const { return m_progress.canceled(); }

    void cancel() { m_progress.cancel(); }

    /*!
     * @return true if the computation has finished
     */
    bool ready() const { return m_started && m_task.ready(); }

private:
    T                      m_value;
    TaskFunc               m_func;
    Scheduler::TaskTracker m_task;
    AtomicProgress         m_progress;
    bool                   m_started    = false;
    Scheduler             *m_threadpool = nullptr;
};