// Copyright (c) 2022 Max Liani
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// scheduler.cpp
#include "scheduler.h"
#include <assert.h>

#include <spdlog/spdlog.h>

struct Scheduler::Task
{
    inline Task(int num_units, void *data, TaskFn fn, TaskFn epilogue = nullptr) :
        data(data), fn(fn), epilogue(epilogue), parent(nullptr), num_units(num_units)
    {
    }

    // These fields are read-only, they are defined on task creation and never change thereafter.
    void  *data;      //< The task data to be passed as argument to the task function.
    TaskFn fn;        //< The task function, executed by as many threads as num_units.
    TaskFn epilogue;  //< The optional task epilogue function, executed only once upon task completion.
    Task  *parent;    //< The parent task in case of nested parallelism.
    int    num_units; //< This is number of units of work. Ideally, this shouldn't exceed the width
                      //  of the hardware concurrency.

    // The following fields change value during the lifetime of the task.
    std::atomic<int> completed{0};           //< How many units of work are completed.
    std::atomic<int> refcount{0};            //< Traditional ref-counting to govern the task lifetime.
    std::atomic<int> dependencies{1};        //< How many nested tasks are still running. Set to one because
                                             //  each task is considered to depend on its own completion too.
    std::atomic<bool>  has_exception{false}; //< Atomic flag stating whether an exception has already been saved.
    std::exception_ptr exception{nullptr};   //< Pointer to an exception in case the task failed

    // The insertion of an invalid task in the scheduler queue causes one of its threads to terminate.
    // Besides that, tasks are never invalid by design.
    bool valid() const { return num_units != 0; }
};

/////////////////////////////////////////////////////////////////////////////////
// Scheduler static members and globals
thread_local int              Scheduler::m_thread_index            = Scheduler::k_invalid_thread_index;
int                           Scheduler::m_next_guest_thread_index = 0;
thread_local Scheduler::Task *Scheduler::m_thread_task             = nullptr;

static std::unique_ptr<Scheduler> s_singleton;
static std::mutex                 s_singleton_lock;

Scheduler *Scheduler::singleton()
{
    std::unique_lock<std::mutex> guard(s_singleton_lock);

    if (!s_singleton)
    {
        s_singleton = std::make_unique<Scheduler>();
        s_singleton->start();
    }

    return s_singleton.get();
}

Scheduler::Scheduler() {}

Scheduler::~Scheduler() { stop(); }

static int get_nesting_level(const Scheduler::Task *task)
{
    int level = 0;
    while (task)
    {
        task = task->parent;
        level++;
    }
    return level;
}

int Scheduler::get_nesting_level() { return ::get_nesting_level(m_thread_task); }

void Scheduler::bind(Task *task)
{
    if (!task)
        return;
    task->refcount++;
}

// Unbind is where tasks are deallocated
bool Scheduler::unbind(Task *task)
{
    if (!task)
        return false;

    int current = --task->refcount;
    assert(current >= 0);

    if (current == 0)
    {
        Task *parent = task->parent;
        delete_task(task);

        // recursion
        if (parent)
            unbind(parent);
        return true;
    }

    return false;
}

// Memory management functions: tasks are allocated and freed by the same module, to safeguard
// from heap corruption across DSO boundaries.
Scheduler::Task *Scheduler::new_task(void *data, TaskFn fn, TaskFn epilogue, int num_units)
{
    return new Task(num_units, data, fn, epilogue);
}

void Scheduler::delete_task(Task *task)
{
    if (!task)
        return;

    assert(task->refcount.load() == 0);

    delete task;
}

// Internal function to track dependencies between nested tasks. By binding a parent task, we make it wait on the
// completion of nested task.
static void bind_parents(Scheduler::Task *task)
{
    while (task)
    {
        task->dependencies++;
        task = task->parent;
    }
}
static void unbind_parents(Scheduler::Task *task)
{
    while (task)
    {
        task->dependencies--;
        task = task->parent;
    }
}

void Scheduler::start(int num_threads)
{
    assert(m_workers.empty() && "Assure scheduler is not initialized twice!");
    {
#if defined(__EMSCRIPTEN__) && !defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
        // if threading is disabled, create no threads
        auto logical_cores = 0;
#elif defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
        // if threading is enabled in emscripten, then use just 1 thread
        auto logical_cores = 1;
#else
        auto logical_cores = std::thread::hardware_concurrency();
#endif
        if (num_threads == k_all)
            num_threads = logical_cores;
        else
            num_threads = std::min<int>(num_threads, logical_cores);
        // The reason we cap num_threads to the number of logical threads in the system is to avoid
        // any conflict in the thread_index assignment for guest threads (calling threads) entering
        // the scheduler during calls to TaskTracker::wait().
        // If the thread index is above the hardware concurrency, it means it is a guest thread,
        // independently on how many threads a scheduler has. This is not important with just one
        // scheduler, but it becomes important when there are more, each with a different count of
        // threads.
    }

    m_next_guest_thread_index = num_threads;
    m_workers.resize(num_threads, nullptr);

    // Spawn worker threads
    for (int thread_index = 0; thread_index < (int)num_threads; ++thread_index)
    {
        m_workers[thread_index] = new std::thread(
            [this, thread_index]
            {
                try
                {
                    // Initialize thread index, worker threads have numbers in the range [0, num_threads-1]
                    // Guest threads will get their own unique indices.
                    m_thread_index = thread_index;

                    spdlog::trace("Spawning worker thread {}", thread_index);
                    while (true)
                    {
                        WorkUnit work_unit;
                        {
                            // usual thread-safe queue code:
                            std::unique_lock<std::mutex> lock(m_work_mutex);
                            m_work_signal.wait(lock, [&] { return !m_work.empty(); });

                            // Transfer ownership to this thread, unbind tasks after running
                            work_unit = m_work.front();
                            m_work.pop_front();
                        }

                        // if the task is invalid, it means we are asked to abort:
                        if (work_unit.task && !work_unit.task->valid())
                        {
                            unbind(work_unit.task);
                            break;
                        }

                        run_task(work_unit.task, work_unit.index, thread_index);
                        unbind(work_unit.task);
                    }
                }
                catch (const std::exception &e)
                {
                    spdlog::error("Caught an exception in a worker thread: '{}'", e.what());
                }
                catch (...)
                {
                    spdlog::error("Caught an exception in a worker thread");
                }
            });
    }
}

void Scheduler::run_task(Task *task, int unit_index, int thread_index)
{
    Task *old_task = m_thread_task;
    m_thread_task  = task;

    if (!task->has_exception.load())
    {
        try
        {
            task->fn(unit_index, thread_index, task->data);
        }
        catch (...)
        {
            bool value = false;
            if (task->has_exception.compare_exchange_strong(value, true))
            {
                spdlog::trace("Storing exception thrown by a task...");
                task->exception = std::current_exception();
            }
            else
                spdlog::trace("Ignoring exception thrown by a task (another exception has already been stored)...");
        }
    }
    else
    {
        spdlog::trace(
            "Skipping callback (task={}, unit_index={}, thread_index={}) because another work unit of this task "
            "threw an exception.",
            (void *)task, unit_index, thread_index);
    }

    int done = ++task->completed;
    if (done == task->num_units)
    {
        if (task->epilogue)
            task->epilogue(task->num_units, thread_index, task->data);

        unbind_parents(task);
    }

    m_thread_task = old_task;
}

void Scheduler::pick_work_unit(int nesting_level, int thread_index)
{
    WorkUnit work_unit;
    {
        std::unique_lock<std::mutex> lock(m_work_mutex);
        if (m_work.empty())
            return;

        // Do not steal any work that is an outer loop compared to the calling context. Doing
        // so would have side effects, including:
        //  - deferring completion of the current workload, and larger memory peaks due to
        //    longer persistent of any temporary buffer.
        //  - risk of deadlocks in the calling user code
        //  - risk of misconfiguration of per-thread context objects pertaining the outer loop
        // These are examples of real problems I had to work around in past projects using TBB,
        // where there is no such protection in place.
        // Note: this mechanism is still simplistic, ideally we could use a dependency tree, and
        //       pick only work_units that are in the current branch; recursively looking at the
        //       Task::parent should do the trick. We could also look ahead in the queue to
        //       search for such a work_unit. Up to now this proved not to be needed; I am post-
        //       poning any feature adding significant complexity to the system if not strictly
        //       required.
        work_unit                   = m_work.front();
        int work_unit_nesting_level = ::get_nesting_level(work_unit.task);
        if (work_unit_nesting_level < nesting_level)
            return;

        m_work.pop_front();
    }

    // if the task is invalid, it means we are asked to abort:
    if (work_unit.task && !work_unit.task->valid())
    {
        assert(false);
        unbind(work_unit.task);
        return;
    }

    run_task(work_unit.task, work_unit.index, thread_index);
    unbind(work_unit.task);
}

void Scheduler::stop()
{
    if (m_workers.empty())
        return;

    // Push invalid tasks, one for each thread. The invalid task will make a thread terminate
    {
        std::unique_lock<std::mutex> lock(m_work_mutex);
        for (size_t i = 0; i < m_workers.size(); ++i)
        {
            Task *task = new Task(0, nullptr, nullptr);
            bind(task);
            m_work.push_back({task, 0});
        }
    }
    m_work_signal.notify_all();

    // Wait for threads to terminate
    for (std::thread *thread : m_workers) thread->join();

    m_workers.clear();
    assert(m_work.empty() && "Work queue should be empty");
}

Scheduler::TaskTracker Scheduler::async(int num_units, void *data, TaskFn f, TaskFn epilogue, int reserved_units,
                                        bool front)
{
    Task *task   = new_task(data, f, epilogue, num_units);
    task->parent = m_thread_task;
    if (task->parent)
        bind(task->parent);
    bind_parents(task->parent);

    // Get the future return value before we hand off the task
    TaskTracker result(task, this);

    num_units -= reserved_units;
    task->refcount += num_units;
    // Store the task in the queue
    if (num_units > 0)
    {
        std::unique_lock<std::mutex> lock(m_work_mutex);
        if (front)
            while (--num_units >= 0) m_work.push_front({task, reserved_units + num_units});
        else
            while (--num_units >= 0) m_work.push_back({task, reserved_units + num_units});
    }

    // Wake as many thread as we need to work on the task
    m_work_signal.notify_all();

    return result;
}

bool Scheduler::TaskTracker::ready() const { return !task || task->dependencies.load() == 0; }

void Scheduler::TaskTracker::wait()
{
    if (!task)
        return;

    int thread_index  = get_or_assign_thread_index();
    int nesting_level = get_nesting_level();

    while (true)
    {
        if (task->dependencies.load() == 0)
            break;

        // Work stealing
        scheduler->pick_work_unit(nesting_level, thread_index);
    }

    // save the exception
    std::exception_ptr exc = task->exception;

    // cleanup
    scheduler->unbind(task);
    task = nullptr, scheduler = nullptr;

    if (exc)
        std::rethrow_exception(exc);
}
