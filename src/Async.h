//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

#include <functional>
#include <future>
#include <chrono>
#include "progress.h"


template <typename T>
class AsyncTask
{
public:
#if FORCE_SERIAL
	static const auto policy = std::launch::deferred;
#else
	static const auto policy = std::launch::async;
#endif

	using TaskFunc = std::function<T(AtomicProgress & progress)>;
	using NoProgressTaskFunc = std::function<T(void)>;

	/*!
	 * Create an asyncronous task that can report back on its progress
	 * @param compute The function to execute asyncrhonously
	 */
	AsyncTask(TaskFunc compute)
		: m_compute([compute](AtomicProgress & prog){T ret = compute(prog); prog.set_done(); return ret;}), m_progress(true)
	{

	}

	/*!
	 * Create an asyncronous task without progress updates
	 * @param compute The function to execute asyncrhonously
	 */
	AsyncTask(NoProgressTaskFunc compute)
		: m_compute([compute](AtomicProgress &){return compute();}), m_progress(false)
	{

	}

	/*!
	 * Start the computation (if it hasn't already been started)
	 */
	void compute()
	{
		// start only if not done and not already started
		if (!m_future.valid() && !m_ready)
			m_future = std::async(policy, m_compute, std::ref(m_progress));
	}

	/*!
	 * Waits until the task has finished, and returns the result.
	 * The tasks return value is cached, so get can be called multiple times.
	 *
	 * @return	The result of the computation
	 */
	T & get()
	{
		if (m_ready)
			return m_value;

		m_value = m_future.valid() ? m_future.get() : m_compute(m_progress);

		m_ready = true;
		return m_value;
	}

	/*!
	 * Query the progress of the task.
	 *
	 * @return The percentage done, ranging from 0.f to 100.f,
	 * or -1 to indicate busy if the task doesn't report back progress
	 */
	float progress() const
	{
		return m_progress.progress();
	}

	void set_progress(float p)
	{
		m_progress.reset_progress(p);
	}

	/*!
	 * @return true if the computation has finished
	 */
	bool ready() const
	{
		if (m_ready)
			return true;

		if (!m_future.valid())
			return false;

		auto status = m_future.wait_for(std::chrono::seconds(0));

		// predent that the computation is ready for deferred execution since we will compute it on-demand in
		// get() anyway
		return (status == std::future_status::ready || status == std::future_status::deferred);
	}

private:
	TaskFunc m_compute;
	std::future<T> m_future;
	T m_value;
	AtomicProgress m_progress;
	bool m_ready = false;
};