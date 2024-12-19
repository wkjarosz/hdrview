// Based on the article and code at:
// https://maxliani.wordpress.com/2022/07/27/anatomy-of-a-task-scheduler/
//
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

//
//
//
// Modifications by Wojciech Jarosz (c) 2024, released under the same Apache License as above
// - Added ability to run tasks in serial, bypassing the thread pool
//   - Using num_threads == 0 doesn't create any workers, and all tasks run in serial immediately by the calling thread
//   - k_all is now defined as -1
// - Added Scheduler::singleton() which returns a global instance of a Scheduler. Upon first being called, the scheduler
//   is created and started with num_threads = k_all. The function is guarded by a mutex.
// - Tasks now store an exception which is re-thrown in TaskTracker::wait()
// - Added ready() function to TaskTracker
// - Added various wrapper/utility templates (parallel_for, blocked_range, do_async) which provide a simpler,
//   higher-level API and allow creating tasks with lambdas containing captures Fixed a few type inconsistencies (mixing
//   uint32_t with int)
// - fixed minor spelling mistakes
// - converted to snake_casing, and added some more comments.
//

// scheduler.h

#pragma once

#include "progress.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

/**
    Implementation of a simple but versatile task scheduler. The scheduler allows to parallelize
    workload and gives full control over how many threads to burst compute to. Nested parallelism
    is fully supported, with priority to inner parallelism. Compared to a typical work stealing
    implementation, there is no spinning, and when there is not enough workload, some threads
    will go idle instead of spinning, making it obvious when the CPU runs underutilized.
    Launching a task incurs a small memory allocation of 48 bytes for the task itself, that
    is it. Lambda captures are not allowed to contain the overhead and the system implementation
    nimble. The scheduler can be instantiated multiple times, to create multiple isolated thread
    pools or \ref singleton() returns a pointer to a global singleton (constructed on first
    use). Schedulers can be instantiated on demand and destroyed if needed.
    Use \ref start() and \ref stop() methods to initialize and teardown a scheduler.
    For examples, look at the documentation of the \ref parallelize() and \ref parallelize_async() methods.
*/
class Scheduler
{
public:
    // The task function prototype. The arguments are the same for the task function and epilogue,
    // however for the latter the first int argument is not a unit_index, but the number of units.
    using TaskFn = void (*)(int unit_index, int thread_index, void *data);

    // Opaque task type.
    struct Task;

    // The task scheduler returns a TaskTracker for any async launch. Use the method \ref wait() to
    // synchronize on the task completion, or \ref ready() for a non-blocking check whether the computation is done.
    struct TaskTracker
    {
        TaskTracker() : task(nullptr), scheduler(nullptr) {}
        TaskTracker(Task *task, Scheduler *scheduler) : task(task), scheduler(scheduler)
        {
            if (scheduler)
                scheduler->bind(task);
        }
        TaskTracker(const TaskTracker &other) : task(other.task), scheduler(other.scheduler)
        {
            if (scheduler)
                scheduler->bind(task);
        }
        TaskTracker(TaskTracker &&other) noexcept :
            task(std::exchange(other.task, nullptr)), scheduler(std::exchange(other.scheduler, nullptr))
        {
        }
        const TaskTracker &operator=(const TaskTracker &other)
        {
            if (scheduler)
                scheduler->unbind(task);
            task = other.task, scheduler = other.scheduler;
            if (scheduler)
                scheduler->bind(task);
            return *this;
        }
        ~TaskTracker()
        {
            if (scheduler)
                scheduler->unbind(task);
        }

        /**
            Non-blocking check whether the computation is finished.

            Ready tasks may still need some cleanup (via \ref ~TaskTracker() or \ref wait()).
        */
        bool ready() const;

        /**
            Wait for the task to complete.

            Calling wait will make the calling thread temporarily enter the task scheduler and participate in the
            computation.

            If any exceptions were thrown during the execution of task, \ref wait() will re-throw *one* of them within
            the context of the calling thread.
        */
        void wait();

        Task      *task;
        Scheduler *scheduler;
    };

public:
    static constexpr int k_all{-1};
    static constexpr int k_invalid_thread_index{-1};

    Scheduler();
    ~Scheduler();
    Scheduler(const Scheduler &)            = delete; ///< non construction-copyable
    Scheduler &operator=(const Scheduler &) = delete; ///< non copyable

    // Return the global default scheduler, which is created upon the first call, and guarded by a mutex
    static Scheduler *singleton();

    /// Start a pool with a number of threads. \ref k_all means use the full hardware concurrency available.
    void start(int num_threads = k_all);

    /// Wait for any pending tasks to complete and terminate all threads in the pool.
    void stop();

    /// Get the number of threads in the pool.
    int size() const { return (int)m_workers.size(); }

    /**
        Retrieve the maximum value for a thread index. The value can be used to reserve space
        for parallel algorithms making use of per-thread resources. However, in the general
        case it is more practical to allocate resources using the num_threads value you pass
        to the \ref parallelize() calls and rely on the unit_index as index into those resources. See
        \ref parallelize() for more details.
    */
    int max_thread_index(bool include_caller = true) const
    {
        if (include_caller)
        {
            // If this is one of the worker threads, it should have a valid thread_index or
            // we may need to assign one to it. There is a change the caller thread never
            // entered the scheduler before.
            int thread_index = m_thread_index;
            if (thread_index == k_invalid_thread_index)
                thread_index = m_thread_index = m_next_guest_thread_index++;
        }

        return m_next_guest_thread_index - 1;
    }

    /**
        In theory not necessary since thread_index is passed as an argument to the task function. This
        can be used in the depth of a task callstack, in case you don't want to pass down the
        argument. If you need to use some index to access per-thread partials in your parallel
        algorithm, it is best to use the unit_index argument to the task function.
        That value is guaranteed to be within the task launch bounds, which tends to be smaller
        in size than the number you may obtain with \ref max_thread_index().
    */
    static int get_thread_index() { return m_thread_index; }

    /**
        To know the depth of task nested parallelism.

        If you are calling this you may be doing something exotic, like attempting at throttling inner parallelism, but
        be careful because such heuristics can easily backfire.
    */
    static int get_nesting_level();

    /**
        Parallelize a task over a number of threads and make the caller participate in the
        computation.

        The call only returns on task completion. If the task implementation will
        make any nested calls to execute parallel workload, the task will wait on completion of
        any related nested task. On completion, the task scheduler can executed an optional
        epilogue function where you can wrap up the computation, for example to sum the thread
        partials of a parallel reduction.
        The scheduler doesn't make any assumption on the workload complexity or its uniformity.
        It is up to you to partition the workload in reasonable units of work, or to use the
        WorkloadController utility to load balance between threads.

        \param num_threads defines how many threads you would like to wake up to execute the
               parallel workload. Each of the threads will execute a "unit" of work whose index
               will be between 0 and num_threads-1. Different from a "parellel_for", you don't
               specify how many elements to iterate over, or expect the scheduler to guess how
               many threads it is appropriate to partition the work. It sounds more laborious
               but it gives more control over the execution.
        \param data a generic pointer over the task data, see example.
        \param fn the task function in the format void (*TaskFn)(int unit_index, int thread_index,
               void* data).
        \param epilogue optional ask wrap-up function, executed when all the unit of works are
               completed. The type is the same as the task function except that the first arg
               is "int num_units" instead of the unit_index.

        Example, parallel reduction:
        \code{.cpp}
            struct TaskData
            {
                [...]
                // Be mindful of false-sharing, if you store partials this way, try to access
                // them only once.
                int partials[num_threads];
                int result;
            };
            TaskData data = {...};
            scheduler.parallelize(num_threads, &data, [](int unit_index, int thread_index, void* data)
            {
                TaskData& taskData = *(TaskData*)data;
                int partialResult = ...; //< compute over local symbols.
                [...]
                taskData.partials[unit_index] = partialResult; //< Store partials at the end.
            },
            [](int num_units, int thread_index, void* data)    //
            {                                                  //
                TaskData& taskData = *(TaskData*)data;         //
                taskData.result = taskData.partials[0];        //< Final reduction example.
                for (int i=1; i<num_units; ++i)                //
                    taskData.result += taskData.partials[i];   //
            });                                                //
        \endcode
    */
    void parallelize(int num_threads, void *data, TaskFn fn, TaskFn epilogue = nullptr)
    {
        if (num_threads == k_all)
            num_threads = size();

        // if zero worker threads were requested/exist, execute on the calling thread
        if (num_threads == 0 || size() == 0)
            return run_locally(num_threads, data, fn, epilogue);

        int thread_index = get_or_assign_thread_index();

        bool front = get_nesting_level() > 0; //< push nested parallelism to the front of the queue

        constexpr int local_run = 1; //< reserve 1 chunk to run in the current thread
        TaskTracker   result    = async(num_threads, data, fn, epilogue, local_run, front);

        // Run the first unit of work on the calling thread.
        int chunk_index = 0;
        run_task(result.task, chunk_index, thread_index);

        // While waiting for the workload to be consumed, the current thread may participate in other tasks.
        result.wait();
    }

    /**
        Similar to \ref parallelize(), but this call is non-blocking: it returns a TaskTracker on which
        to call \ref wait() if needed. This call can be used for set-and-forget async task launches,
        however some attention must be taken to make sure the task data persists for the duration
        of the task. Typically, you want to wait on the task completion, in which case the use of
        stack memory for the task data is all you need. Otherwise, allocate any task data on the
        heap and use the epilogue function to free it. If you run \ref parallelize_async() from within
        another task, make sure you call \ref wait() in the TaskTracker, unless the completion of the
        parent task epilogue doesn't depend on the completion of this async task.

        Example of set-and-forget launch:
        \code{.cpp}
            struct TaskData
            {
                [...]
            };
            TaskData* data = new TaskData;
            scheduler.parallelize_async(num_threads, data, [](int unit_index, int thread_index,
                                                              void* data)
            {
                TaskData* taskData = (TaskData*)data;
                [...]
            },
            [](int num_units, int thread_index, void* data)
            {
                TaskData* taskData = (TaskData*)data;
                [...]
                delete taskData;
            });
        \endcode
    */
    TaskTracker parallelize_async(int num_threads, void *data, TaskFn fn, TaskFn epilogue = nullptr)
    {
        if (num_threads == k_all)
            num_threads = size();

        // if zero worker threads were requested/exist, execute on the calling thread
        if (num_threads == 0 || size() == 0)
        {
            run_locally(num_threads, data, fn, epilogue);
            return TaskTracker(); //< return an empty TaskTracker that reports it is done
        }

        bool front = get_nesting_level() > 0; //< push nested parallelism to the front of the queue
        return async(num_threads, data, fn, epilogue, 0, front);
    }

private:
    // Run a task, if the task is complete, including any nested tasks, run the epilog function.
    void run_task(Task *task, int chunk_index, int thread_index);

    // Run a task locally on the calling thread, bypassing the thread pool
    void run_locally(int num_threads, void *data, TaskFn fn, TaskFn epilogue = nullptr) const
    {
        if (num_threads == 0)
            num_threads = 1;

        for (int i = 0; i < num_threads; ++i) fn(i, 0, data);

        if (epilogue)
            epilogue(num_threads, 0, data);
    }

    // Pick a workUnit from the queue. This is used internally for work stealing.
    void pick_work_unit(int nesting_level, int thread_index);

    // Memory management functions: tasks are allocated and freed by the same module, to safeguard
    // from heap corruption across DSO boundaries.
    Task *new_task(void *data, TaskFn f, TaskFn epilogue = nullptr, int num_units = 1);
    void  delete_task(Task *task);

    // Internal task ref-counting calls. Unbind may deallocate a task.
    void bind(Task *task);
    bool unbind(Task *task);

    static int get_or_assign_thread_index()
    {
        // If this is one of the worker threads, it has a valid thread_index. Otherwise, we may
        // need to assign one to it if this is the first time the thread enters the scheduler.
        // Note: there is a minor vulnerability if tasks are scheduled by many temporary threads
        //       which are spawned and allowed to terminate. Such threads will waste thread_indices
        //       since there is no recycling policy. It is rather rare to mix the use of a task
        //       scheduler and temporary threads, typically it's one or the other, and for this
        //       reason I didn't implement protection for it.
        int thread_index = m_thread_index;
        if (thread_index == k_invalid_thread_index)
            thread_index = m_thread_index = m_next_guest_thread_index++;
        return thread_index;
    }

    /**
        Internal method to launch a task. Extra arguments over \ref parallelize() are:

        \param reserved_units The number of units the task launch function may want to reserve
               to execute in the local thread. For example \ref parallelize() reserves one unit,
               \ref parallelize_async() reserves none.
        \param front Insert new tasks to the front of the queue or at the back. Typically,
               nested parallelism inserts at the front to complete as soon as possible, before
               outer parallelism is exhausted; while new outer parallelization is pushes at the
               back of the queue, to let existing workloads complete first.
    */
    TaskTracker async(int num_units, void *data, TaskFn f, TaskFn epilogue = nullptr, int reserved_units = 0,
                      bool front = false);

    struct WorkUnit
    {
        Task *task{nullptr};
        int   index{0}; //< the unit_index
    };
    std::vector<std::thread *> m_workers;     ///< Worker threads
    std::deque<WorkUnit>       m_work;        ///< Work queue, consumed front to back
    std::mutex                 m_work_mutex;  ///< Synchronization to access the work queue
    std::condition_variable    m_work_signal; ///< Signal to wake up threads

    static thread_local int   m_thread_index;
    static int                m_next_guest_thread_index;
    static thread_local Task *m_thread_task;
};

/**
    Utility to estimate how many threads are appropriate to execute some parallel computation based on a workload size.

    The function automatically caps the maximum number of threads to the count in the scheduler.

    \param workload_size Total size of the workload (e.g. number of elements to process).
    \param min_unit_size The number of elements per thread that are considered viable to mitigate scheduling overhead.
*/
inline size_t estimate_threads(size_t workload_size, size_t min_unit_size, const Scheduler &scheduler)
{
    size_t chunks = (workload_size + min_unit_size - 1) / min_unit_size;
    return std::min<size_t>(chunks, scheduler.size());
}

template <typename Int>
struct blocked_range
{
public:
    blocked_range(Int begin, Int end, Int block_size = 1) : m_begin(begin), m_end(end), m_block_size(block_size) {}

    struct iterator
    {
        Int value;

        iterator(Int value) : value(value) {}

        Int operator*() const { return value; }
        operator Int() const { return value; }

        void operator++() { value++; }
        bool operator==(const iterator &it) { return value == it.value; }
        bool operator!=(const iterator &it) { return value != it.value; }
    };

    uint32_t blocks() const { return (uint32_t)((m_end - m_begin + m_block_size - 1) / m_block_size); }

    iterator begin() const { return iterator(m_begin); }
    iterator end() const { return iterator(m_end); }
    Int      block_size() const { return m_block_size; }

private:
    Int m_begin;
    Int m_end;
    Int m_block_size;
};

template <typename Int>
struct AtomicLoadBalance
{
private:
    std::atomic<uint32_t>   &current_block; ///< An atomic counter for the progress made. Goes from 0 to range.blocks();
    const blocked_range<Int> range;

public:
    Int begin, end; ///< Range of elements to process, updated with each call to advance()

    AtomicLoadBalance(std::atomic<uint32_t> &workload, const blocked_range<Int> &r) : current_block(workload), range(r)
    {
    }

    // Threads call advance to obtain a new range of elements [start, end).
    // Returns false when the workload is consumed and there is nothing left to do.
    bool advance()
    {
        uint32_t block_index = current_block++;
        begin                = range.begin() + block_index * range.block_size();
        end                  = begin + range.block_size();
        if (end > range.end())
            end = range.end();
        return (block_index < range.blocks());
    }
};

template <typename Int, typename Func>
void parallel_for(const blocked_range<Int> &range, Func &&func, int num_threads = Scheduler::k_all,
                  Scheduler *scheduler = nullptr)
{
    if (!scheduler)
        scheduler = Scheduler::singleton();

    struct Payload
    {
        Func                 *f;
        blocked_range<Int>    range;
        std::atomic<uint32_t> workload = 0;
    };

    Payload payload{&func, range};

    auto callback = [](int unit_index, int thread_index, void *payload)
    {
        Payload          &p = *(Payload *)payload;
        AtomicLoadBalance workload(p.workload, p.range);

        while (workload.advance()) { (*p.f)(workload.begin, workload.end, unit_index, thread_index); }
    };

    scheduler->parallelize(num_threads, &payload, callback);
}

template <typename Int, typename Func1, typename Func2>
Scheduler::TaskTracker parallel_for_async(const blocked_range<Int> &range, Func1 &&func, Func2 &&epilogue,
                                          int num_threads = Scheduler::k_all, Scheduler *scheduler = nullptr)
{
    if (!scheduler)
        scheduler = Scheduler::singleton();

    using BaseFunc1 = typename std::decay<Func1>::type;
    using BaseFunc2 = typename std::decay<Func2>::type;

    struct Payload
    {
        BaseFunc1             f;
        BaseFunc2             e;
        blocked_range<Int>    range;
        std::atomic<uint32_t> workload = 0;
    };

    auto callback = [](int unit_index, int thread_index, void *payload)
    {
        Payload          &p = *(Payload *)payload;
        AtomicLoadBalance workload(p.workload, p.range);

        while (workload.advance()) { p.f(workload.begin, workload.end, unit_index, thread_index); }
    };
    auto deleter = [](int num_units, int thread_index, void *payload)
    {
        Payload *p = (Payload *)payload;
        p->e(num_units, thread_index);
        delete p;
    };

    Payload *payload = new Payload{std::forward<Func1>(func), std::forward<Func2>(epilogue), range};

    return scheduler->parallelize_async(num_threads, payload, callback, deleter);
}

template <typename Int, typename Func1>
Scheduler::TaskTracker parallel_for_async(const blocked_range<Int> &range, Func1 &&func,
                                          int num_threads = Scheduler::k_all, Scheduler *scheduler = nullptr)
{
    return parallel_for_async(range, func, [](int, int) {}, num_threads, scheduler);
}

template <typename Func>
Scheduler::TaskTracker do_async(Func &&func, Scheduler *scheduler = nullptr)
{
    if (!scheduler)
        scheduler = Scheduler::singleton();

    using BaseFunc = typename std::decay<Func>::type;

    struct Payload
    {
        BaseFunc f;
    };

    auto callback = [](int, int, void *payload) { ((Payload *)payload)->f(); };
    auto deleter  = [](int, int, void *payload) { delete (Payload *)payload; };

    Payload *payload = new Payload{std::forward<Func>(func)};

    return scheduler->parallelize_async(1, payload, callback, deleter);
}

template <typename Func>
Scheduler::TaskTracker do_async(Func &&func, AtomicProgress &progress, Scheduler *scheduler = nullptr)
{
    if (!scheduler)
        scheduler = Scheduler::singleton();

    using BaseFunc = typename std::decay<Func>::type;

    struct Payload
    {
        BaseFunc        f;
        AtomicProgress &progress;
    };

    auto callback = [](int, int, void *payload) { ((Payload *)payload)->f(((Payload *)payload)->progress); };
    auto deleter  = [](int, int, void *payload) { delete (Payload *)payload; };

    Payload *payload = new Payload{std::forward<Func>(func), progress};

    return scheduler->parallelize_async(1, payload, callback, deleter);
}
