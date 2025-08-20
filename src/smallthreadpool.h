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
// Modifications by Wojciech Jarosz (c) 2024, released under the same Apache License as above
// - Renamed Scheduler to ThreadPool
// - Converted to a single-header library.
//   - #define SMALL_THREAD_POOL in *one* translation unit before #including this file
// - Added ability to run tasks in serial, bypassing the thread pool
//   - Using num_threads == 0 doesn't create any workers, and all tasks run in serial immediately by the calling thread
//   - k_all is now defined as -1
// - Added ThreadPool::singleton() which returns a global instance of a ThreadPool. Upon first being called, the
//   thread pool is created and started with num_threads = k_all. The function is guarded by a mutex.
// - Tasks now store an exception which is re-thrown in TaskTracker::wait()
// - Added ready() function to TaskTracker
// - Added various wrapper/utility templates (parallel_for, blocked_range, do_async) which provide a simpler,
//   higher-level API and allow creating tasks with lambdas containing captures.
// - Fixed a few type inconsistencies (mixing uint32_t with int)
// - Fixed minor spelling mistakes
// - Converted to snake_casing, and added some more comments.
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

//! Small Thread Pool namespace
namespace stp
{

/** Implementation of a simple but versatile thread pool/task scheduler.

    The pool allows parallelizing workloads with control over how many threads to burst compute to. Nested parallelism
    is fully supported, with priority to inner parallelism. Compared to a typical work stealing implementation, there is
    no spinning, and when there is not enough workload, some threads will go idle instead of spinning, making it obvious
    when the CPU runs underutilized. Launching a task incurs a small memory allocation (48 bytes + sizeof(atomic<bool>)
    + sizeof(exception_ptr)) for the task itself, that is it. The scheduler can be instantiated multiple times, to
    create multiple isolated thread pools, or \ref singleton() returns a pointer to a single global instance
    (constructed on first use). Thread pools can be instantiated on demand and destroyed if needed. Use \ref start() and
    \ref stop() methods to initialize and teardown a thread pool.

    For examples, look at the documentation of the \ref parallelize() and \ref parallelize_async() methods.

    ThreadPool is relatively low-level, and only supports calling a function pointer matching the signature of \ref
    TaskFn (this cannot be a stateful lambda). User-supplied data can be accessed by the callback via the supplied void
    pointer, which can be cast to the appropriate type within the callback body.

    Higher-level template wrapper functions are provided at the bottom (see \ref parallel_for, \ref parallel_for_async,
    \ref do_async) which do the required gymnastics to allow passing in stateful lambda functions.
*/
class ThreadPool
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
        TaskTracker(Task *task, ThreadPool *scheduler) : task(task), scheduler(scheduler)
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

        Task       *task;
        ThreadPool *scheduler;
    };

public:
    static constexpr int k_all{-1};
    static constexpr int k_invalid_thread_index{-1};

    ThreadPool();
    ~ThreadPool();
    ThreadPool(const ThreadPool &)            = delete; ///< non construction-copyable
    ThreadPool &operator=(const ThreadPool &) = delete; ///< non copyable

    // Return the global default scheduler, which is created upon the first call, and guarded by a mutex
    static ThreadPool *singleton();

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
inline size_t estimate_threads(size_t workload_size, size_t min_unit_size, const ThreadPool &scheduler)
{
    size_t chunks = (workload_size + min_unit_size - 1) / min_unit_size;
    return std::min<size_t>(chunks, scheduler.size());
}

/** Represents a contiguous integer range split into fixed-size blocks.

   @tparam Int An integer type (e.g. int, long).

   The blocked_range encapsulates [begin, end) and a block size used by the scheduler
   to partition work into coarse-grained chunks. It provides a simple iterator over
   element indices and helpers for computing the number of blocks and the block size.
*/
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

    /** Number of blocks of size block_size needed to cover [begin,end).

        This is computed as ceil((end-begin)/block_size). The value is suitable for
        use with AtomicLoadBalance which atomically hands out block indices to threads.
     */
    uint32_t blocks() const { return (uint32_t)((m_end - m_begin + m_block_size - 1) / m_block_size); }

    iterator begin() const { return iterator(m_begin); }
    iterator end() const { return iterator(m_end); }

    /** Size of each block used to split the overall range.

        A block represents a contiguous sub-range of elements that will be handed to a worker.
     */
    Int block_size() const { return m_block_size; }

private:
    Int m_begin;
    Int m_end;
    Int m_block_size;
};

/** Helper to atomically load-balance blocked_range work between threads.

    AtomicLoadBalance wraps an atomic<uint32_t> counter (shared among workers) and a
    blocked_range. Threads repeatedly call advance() to obtain the next block to process.

    Usage pattern:
    - Construct with a shared atomic counter and the target blocked_range.
    - Repeatedly call advance(); when it returns true, the public members 'begin' and 'end'
        describe the next [begin,end) sub-range to process. When advance() returns false,
        there is no more work.

    Thread-safety: advance() performs an atomic increment on the provided counter and is
    safe to call concurrently from multiple threads.

    Example:
    @code{.cpp}
    std::atomic<uint32_t> counter{0};
    AtomicLoadBalance<int> workload(counter, range);
    while (workload.advance()) {
        process(workload.begin, workload.end);
    }
    @endcode
*/
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

/** Parallelize work over a blocked_range by repeatedly invoking a user provided callable.

    The callable must be invokable as:
    void func(Int begin, Int end, int unit_index, int thread_index)

    @tparam Int Integer type used for the blocked_range.
    @tparam Func Callable type.
    @param range Range of work to split into blocks (see \ref blocked_range).
    @param func Callable invoked by each worker for each assigned block. Signature: (begin,end,unit_index,thread_index).
    @param num_threads Number of threads to use. Defaults to ThreadPool::k_all (use scheduler size).
    @param scheduler Optional ThreadPool pointer; when null ThreadPool::singleton() is used.

    Example:
    @code{.cpp}
    // compute over rows [0, height) in blocks of block_size using num_threads:
    parallel_for(blocked_range<int>(0, height, block_size),
                [&](int begin, int end, int unit_index, int thread_index)
                {
                    for (int y = begin; y < end; ++y)
                        // process row y
                },
                (int)num_threads);
    @endcode
*/
template <typename Int, typename Func>
void parallel_for(const blocked_range<Int> &range, Func &&func, int num_threads = ThreadPool::k_all,
                  ThreadPool *scheduler = nullptr)
{
    if (!scheduler)
        scheduler = ThreadPool::singleton();

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

/** Asynchronously run a parallel_for with an epilogue.

    The main callable must be invokable as:
    void func(Int begin, Int end, int unit_index, int thread_index)

    The epilogue must be invokable as:
    void epilogue(int num_units, int thread_index)

    @tparam Int Integer type for blocked_range.
    @tparam Func1 Main callable type.
    @tparam Func2 Epilogue callable type.
    @param range Range to process.
    @param func Main callable (copied into heap payload).
    @param epilogue Epilogue callable to run when task completes (copied into heap payload).
    @param num_threads Number of threads or ThreadPool::k_all.
    @param scheduler Optional scheduler pointer (defaults to ThreadPool::singleton()).
    @return ThreadPool::TaskTracker A TaskTracker that can be waited on.

    Example:
    @code{.cpp}
    auto tracker = parallel_for_async(blocked_range<int>(0, N, block_size),
        [=](int b, int e, int unit, int thread)
        {
            for (int i = b; i < e; ++i)
                // process element i
        },
        [](int num_units, int thread)
        {
            // final reduce / cleanup
        },
        (int)num_threads);

    // perform some other tasks concurrently to the above async loop

    tracker.wait(); // wait for the parallel_for_async to complete
    @endcode
*/
template <typename Int, typename Func1, typename Func2>
ThreadPool::TaskTracker parallel_for_async(const blocked_range<Int> &range, Func1 &&func, Func2 &&epilogue,
                                           int num_threads = ThreadPool::k_all, ThreadPool *scheduler = nullptr)
{
    if (!scheduler)
        scheduler = ThreadPool::singleton();

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

/** Convenience overload: async parallel_for without an epilogue.

    @tparam Int Integer type for blocked_range.
    @tparam Func1 Main callable type.
    @param range Range to process.
    @param func Main callable.
    @param num_threads Number of threads or ThreadPool::k_all.
    @param scheduler Optional scheduler pointer.

    @see parallel_for_async(range, func, epilogue, ...)
*/
template <typename Int, typename Func1>
ThreadPool::TaskTracker parallel_for_async(const blocked_range<Int> &range, Func1 &&func,
                                           int num_threads = ThreadPool::k_all, ThreadPool *scheduler = nullptr)
{
    return parallel_for_async(range, func, [](int, int) {}, num_threads, scheduler);
}

/** Launch a single-unit asynchronous task that invokes a user-provided callable.

    The callable must be invokable with no parameters:
        void func()

    @tparam Func Callable type.
    @param func Callable to execute asynchronously.
    @param scheduler Optional ThreadPool pointer. If null, ThreadPool::singleton() is used.
    @return ThreadPool::TaskTracker TaskTracker that can be used to wait() for completion.

    Example:
    @code{.cpp}
    int output = 0;
    // schedule a background computation
    auto async_tracker = do_async(
        [&output]() {
            // perform some computation
            // if you want to return a result, do so via a lambda capture like output/
            // output must remain valid until after async_tracker.wait() is called below
        });

    // do some other computation here while the above runs in the background
    // it is not safe to access output until after async_tracker.wait() is called

    async_tracker.wait(); // wait for the background computation to complete (if it hasn't already)
    @endcode
*/
template <typename Func>
ThreadPool::TaskTracker do_async(Func &&func, ThreadPool *scheduler = nullptr)
{
    if (!scheduler)
        scheduler = ThreadPool::singleton();

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

} // namespace stp

#ifdef SMALL_THREADPOOL_IMPLEMENTATION

#include <assert.h>
#include <spdlog/spdlog.h>

namespace stp
{

struct ThreadPool::Task
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
// ThreadPool static members and globals
thread_local int               ThreadPool::m_thread_index            = ThreadPool::k_invalid_thread_index;
int                            ThreadPool::m_next_guest_thread_index = 0;
thread_local ThreadPool::Task *ThreadPool::m_thread_task             = nullptr;

static std::unique_ptr<ThreadPool> s_singleton;
static std::mutex                  s_singleton_lock;

ThreadPool *ThreadPool::singleton()
{
    std::unique_lock<std::mutex> guard(s_singleton_lock);

    if (!s_singleton)
    {
        s_singleton = std::make_unique<ThreadPool>();
        s_singleton->start();
    }

    return s_singleton.get();
}

ThreadPool::ThreadPool() {}

ThreadPool::~ThreadPool() { stop(); }

static int get_nesting_level(const ThreadPool::Task *task)
{
    int level = 0;
    while (task)
    {
        task = task->parent;
        level++;
    }
    return level;
}

int ThreadPool::get_nesting_level() { return stp::get_nesting_level(m_thread_task); }

void ThreadPool::bind(Task *task)
{
    if (!task)
        return;
    task->refcount++;
}

// Unbind is where tasks are deallocated
bool ThreadPool::unbind(Task *task)
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
ThreadPool::Task *ThreadPool::new_task(void *data, TaskFn fn, TaskFn epilogue, int num_units)
{
    return new Task(num_units, data, fn, epilogue);
}

void ThreadPool::delete_task(Task *task)
{
    if (!task)
        return;

    assert(task->refcount.load() == 0);

    delete task;
}

// Internal function to track dependencies between nested tasks. By binding a parent task, we make it wait on the
// completion of nested task.
static void bind_parents(ThreadPool::Task *task)
{
    while (task)
    {
        task->dependencies++;
        task = task->parent;
    }
}
static void unbind_parents(ThreadPool::Task *task)
{
    while (task)
    {
        task->dependencies--;
        task = task->parent;
    }
}

void ThreadPool::start(int num_threads)
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

void ThreadPool::run_task(Task *task, int unit_index, int thread_index)
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

void ThreadPool::pick_work_unit(int nesting_level, int thread_index)
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
        int work_unit_nesting_level = stp::get_nesting_level(work_unit.task);
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

void ThreadPool::stop()
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

ThreadPool::TaskTracker ThreadPool::async(int num_units, void *data, TaskFn f, TaskFn epilogue, int reserved_units,
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

bool ThreadPool::TaskTracker::ready() const { return !task || task->dependencies.load() == 0; }

void ThreadPool::TaskTracker::wait()
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

} // namespace stp

#endif
