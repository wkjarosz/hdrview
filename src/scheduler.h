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
// - Added ability to run tasks in serial, bypassing the thread pool
//   - Using numThreads == 0 doesn't create any workers, and all tasks run in serial immediately by the calling thread
//   - k_all is now defined as -1
// - Added Scheduler::singleton() which returns a global instance of a Scheduler. Upon first being called, the scheduler
//   is created and started with numThreads = k_all. The function is guarded by a mutex.
// - Added ready() function to TaskTracker
// - Added various wrapper/utility templates (parallel_for, blocked_range, do_async) which provide a simpler,
//   higher-level API and allow creating tasks with lambdas containing captures Fixed a few type inconsistencies (mixing
//   uint32_t with int)
// - fixed minor spelling mistakes
//

// scheduler.h

#pragma once

#include "progress.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Implementation of a simple but versatile task scheduler. The scheduler allows to parallelize
// workload and gives full control over how many threads to burst compute to. Nested parallelism
// is fully supported, with priority to inner parallelism. Compared to a typical work stealing
// implementation, there is no spinning, and when there is not enough workload, some threads
// will go idle instead of spinning, making it obvious when the CPU runs underutilized.
// Launching a task incur into a small memory allocation of 48 bytes for the task itself, that
// is it. Lambda captures are not allowed to contain the overhead and the system implementation
// nimble. The scheduler can be instantiated multiple times, to create multiple isolated thread
// pools. There is no singleton that may prescribe how to access the scheduler. Schedulers can
// be instantiated on demand and destroyed if needed.
// Use start() and stop() methods to initialize and teardown a scheduler.
// For examples, look at the documentation of the parallelize() and parallelizeAsync() methods.
class Scheduler
{
public:
    // The task function prototype. The arguments are the same for the task function and epilogue,
    // however for the latter the first int argument is not a unitIndex, but the number of units.
    using TaskFn = void (*)(int unitIndex, int threadIndex, void *data);

    // Opaque task type.
    struct Task;

    // The task scheduler returns a TaskTracker for any async launch. Use the method wait() to
    // synchronize on the task completion, or ready() for a non-blocking check whether the computation is done.
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

        // Non-blocking check whether the computation is finished.
        bool ready() const;

        // Wait for the task to complete. Calling wait will make the calling thread to temporarily
        // enter the task scheduler and participate to the computation.
        void wait();

        Task      *task;
        Scheduler *scheduler;
    };

public:
    static constexpr int k_all                = -1;
    static constexpr int k_invalidThreadIndex = -1;

    Scheduler();
    ~Scheduler();
    Scheduler(const Scheduler &)            = delete; // non construction-copyable
    Scheduler &operator=(const Scheduler &) = delete; // non copyable

    // Return the global default scheduler, which is created upon the first call, and guarded by a mutex
    static Scheduler *singleton();

    // Start a scheduler with a number of threads. k_all means use the full hardware
    // concurrency available.
    void start(int numThreads = k_all);

    // Wait for any pending tasks to complete and terminate all threads in the pool.
    void stop();

    // Get the number of threads in the scheduler pool.
    int getNumThreads() const { return (int)m_workers.size(); }

    // Retrieve the maximum value for a thread index. The value can be used to reserve space
    // for parallel algorithms making use of per-thread resources. However, in the general
    // case it is more practical to allocate resources using the numThreads value you pass
    // to the parallelize calls and rely on the unitIndex as index into those resources. See
    // parallelize for more details.
    int getMaxThreadIndex(bool includeCaller = true) const
    {
        if (includeCaller)
        {
            // If this is one of the worker threads, it should have a valid threadIndex or
            // we may need to assign one to it. There is a change the caller thread never
            // entered the scheduler before.
            int threadIndex = m_threadIndex;
            if (threadIndex == k_invalidThreadIndex)
                threadIndex = m_threadIndex = m_nextGuestThreadIndex++;
        }

        return m_nextGuestThreadIndex - 1;
    }

    // In theory not necessary, threadIndex is passed as argument to the task function. This
    // can be used in the depth of a task callstack, in case you don't want to pass down the
    // argument. If you need to use some index to access per-thread partials in your parallel
    // algorithm, it is best to use the unitIndex argument to the task function.
    // That value is guaranteed to be within the task launch bounds, which tends to be smaller
    // in size than the number you may obtain with getMaxThreadIndex.
    static int getThreadIndex() { return m_threadIndex; }

    // To know the depth of task nested parallelism. If you are calling this you may be doing
    // something exotic, like attempting at throttling inner parallelism, but be careful because
    // such heuristics can easily backfire.
    static int getNestingLevel();

    // Parallelize a task over a number of threads and make the caller to participate in the
    // computation. The call only returns on task completion. If the task implementation will
    // make any nested calls to execute parallel workload, the task will wait on completion of
    // any related nested task. On completion, the task scheduler can executed an optional
    // epilogue function where you can wrap up the computation, for example to sum the thread
    // partials of a parallel reduction.
    // The scheduler doesn't make any assumption on the workload complexity or its uniformity.
    // It is up to you to partition the workload in reasonable units of work, or to use the
    // WorkloadController utility to load balance between threads.
    // @param numThreads defines how many threads you would like to wake up to execute the
    //        parallel workload. Each of the threads will execute a "unit" of work whose index
    //        will be between 0 and numThreads-1. Different from a "parellel_for", you don't
    //        specify how many elements to iterate over, or expect the scheduler to guess how
    //        many threads it is appropriate to partition the work. It sounds more laborious
    //        but it gives more control over the execution.
    // @param data a generic pointer over the task data, see example.
    // @param fn the task function in the format void (*TaskFn)(int unitIndex, int threadIndex,
    //        void* data).
    // @param epilogue optional ask wrap-up function, executed when all the unit of works are
    //        completed. The type is the same as the task function except that the first arg
    //        is "int numUnits" instead of the unitIndex.
    // Example, parallel reduction:
    //     struct TaskData
    //     {
    //         [...]
    //         // Be mindful of false-sharing, if you store partials this way, try to access
    //         // them only once.
    //         int partials[numThreads];
    //         int result;
    //     };
    //     TaskData data = {...};
    //     scheduler.parallelize(numThreads, &data, [](int unitIndex, int threadIndex, void* data)
    //     {
    //         TaskData& taskData = *(TaskData*)data;
    //         int partialResult = ...; //< compute over local symbols.
    //         [...]
    //         taskData.partials[unitIndex] = partialResult; //< Store partials at the end.
    //     },
    //     [](int numUnits, int threadIndex, void* data)      //
    //     {                                                  //
    //         TaskData& taskData = *(TaskData*)data;         //
    //         taskData.result = taskData.partials[0];        //< Final reduction example.
    //         for (int i=1; i<numUnits; ++i)                 //
    //             taskData.result += taskData.partials[i];   //
    //     });                                                //
    void parallelize(int numThreads, void *data, TaskFn fn, TaskFn epilogue = nullptr)
    {
        if (numThreads == k_all)
            numThreads = getNumThreads();

        // if zero worker threads were requested/exist, execute on the calling thread
        if (numThreads == 0 || getNumThreads() == 0)
            return runOnCallingThread(numThreads, data, fn, epilogue);

        int threadIndex = getOrAssignThreadIndex();

        bool front = getNestingLevel() > 0; //< push nested parallelism to the front of the queue

        constexpr int localRun = 1; //< reserve 1 chunk to run in the current thread
        TaskTracker   result   = async(numThreads, data, fn, epilogue, localRun, front);

        // Run the first unit of work on the calling thread.
        int chunkIndex = 0;
        runTask(result.task, chunkIndex, threadIndex);

        // While waiting for the workload to be consumed, the current thread may participate in
        // other tasks.
        result.wait();
    }

    // Similar to parallelize, but this call is non-blocking: it returns a TaskTracker on which
    // to call wait if needed. This call can be used for set-and-forget async task launches,
    // however some attention must be taken to make sure the task data persists for the duration
    // of the task. Typically, you want to wait on the task completion, in which case the use of
    // stack memory for the task data is all you need. Otherwise, allocate any task data on the
    // heap and use the epilogue function to free it. If you run parallelizeAsync from within
    // another task, make sure you call wait in the TaskTracker, unless the completion of the
    // parent task epilogue doesn't depend on the completion of this async task.
    // Example of set-and-forget lauch:
    //     struct TaskData
    //     {
    //         [...]
    //     };
    //     TaskData* data = new TaskData;
    //     scheduler.parallelizeAsync(numThreads, data, [](int unitIndex, int threadIndex,
    //                                                       void* data)
    //     {
    //         TaskData* taskData = (TaskData*)data;
    //         [...]
    //     },
    //     [](int numUnits, int threadIndex, void* data)
    //     {
    //         TaskData* taskData = (TaskData*)data;
    //         [...]
    //         delete taskData;
    //     });
    TaskTracker parallelizeAsync(int numThreads, void *data, TaskFn fn, TaskFn epilogue = nullptr)
    {
        if (numThreads == k_all)
            numThreads = getNumThreads();

        // if zero worker threads were requested/exist, execute on the calling thread
        if (numThreads == 0 || getNumThreads() == 0)
        {
            runOnCallingThread(numThreads, data, fn, epilogue);
            return TaskTracker(); //< return an empty TaskTracker that reports it is done
        }

        bool front = getNestingLevel() > 0; //< push nested parallelism to the front of the queue
        return async(numThreads, data, fn, epilogue, 0, front);
    }

private:
    // Run a task, if the task is complete, including any nested tasks, run the epilog function.
    void runTask(Task *task, int chunkIndex, int threadIndex);

    // Run a task serially on the calling thread bypassing the thread pool
    void runOnCallingThread(int numThreads, void *data, TaskFn fn, TaskFn epilogue = nullptr) const
    {
        if (numThreads == 0)
            numThreads = 1;

        for (int i = 0; i < numThreads; ++i) fn(i, 0, data);
        if (epilogue)
            epilogue(numThreads, 0, data);
    }

    // Pick a workUnit from the queue. This is used internally for work stealing.
    void pickWorkUnit(int nestingLevel, int threadIndex);

    // Memory management functions: tasks are allocated and freed by the same module, to safeguard
    // from heap corruption across  dso boundaries.
    Task *newTask(void *data, TaskFn f, TaskFn epilogue = nullptr, int numUnits = 1);
    void  deleteTask(Task *task);

    // Internal task ref-counting calls. Unbind may deallocate a task.
    void bind(Task *task);
    bool unbind(Task *task);

    static int getOrAssignThreadIndex()
    {
        // If this is one of the worker threads, it has a valid threadIndex. Otherwise, we may
        // need to assign one to it if this is the first time the thread enters the scheduler.
        // Note: there is a minor vulnerability if tasks are scheduled by many temporary threads
        //       which are spawned and let to terminate, such threads will waste threadIndices
        //       given there is no recycling policy. It is rather rare to mix the use of a task
        //       scheduler and temporary threads, typically it's one or the other, and for this
        //       reason I didn't implement protection for it.
        int threadIndex = m_threadIndex;
        if (threadIndex == k_invalidThreadIndex)
            threadIndex = m_threadIndex = m_nextGuestThreadIndex++;
        return threadIndex;
    }

    // Internal method to launch a task. Extra arguments over parallelize are:
    // @param reservedUnits, the number of units the task launch function may want to reserve
    //        to execute in the local thread. For example parallelize reserves one unit,
    //        parallelizeAsync reserves none.
    // @param front, insert new tasks to the front of the queue or at the back. Typically,
    //        nested parallelism inserts at the front to complete as soon as possible, before
    //        outer parallelism is exhausted; while new outer parallelization is pushes at the
    //        back of the queue, to let existing workload to complete first.
    TaskTracker async(int numUnits, void *data, TaskFn f, TaskFn epilogue = nullptr, int reservedUnits = 0,
                      bool front = false);

    struct TaskUnit
    {
        Task *task  = nullptr;
        int   index = 0; //< the unitIndex
    };
    std::vector<std::thread *> m_workers;    //< Worker threads
    std::deque<TaskUnit>       m_work;       //< Work queue, consumed front to back
    std::mutex                 m_workMutex;  //< Synchronization to access the work queue
    std::condition_variable    m_workSignal; //< Signal to wake up threads

    static thread_local int   m_threadIndex;
    static int                m_nextGuestThreadIndex;
    static thread_local Task *m_threadTask;
};

// Utility to estimate how many threads are appropriate to execute some parallel computation
// based on a workload size. Template argument k_unitSize is the number of elements/thread
// that are considered viable to mitigate scheduling overhead. k_maxThreads is an optional
// argument for the maximum number of threads (in case it is desirable to limit parallelism
// and control scaling). The function automatically caps the maximum number of threads to
// the count in the scheduler.
template <int k_unitSize, int k_maxThreads = 1 << 16>
inline size_t estimateThreads(size_t workloadSize, const Scheduler &scheduler)
{
    size_t nChunks    = (workloadSize + k_unitSize - 1) / k_unitSize;
    size_t numThreads = std::min<size_t>(nChunks, std::min<size_t>(k_maxThreads, scheduler.getNumThreads()));
    return numThreads;
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
    std::atomic<uint32_t>   &current_block; //< An atomic counter for the progress made. Goes from 0 to range.blocks();
    const blocked_range<Int> range;

public:
    Int begin, end; //< range of elements to process, updated with each call to advance()

    AtomicLoadBalance(std::atomic<uint32_t> &workload, const blocked_range<Int> &r) : current_block(workload), range(r)
    {
    }

    // Threads call advance to obtain a new range of elements [start, end).
    // Returns false when the workload is consumed and nothing else is left to do.
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

    auto callback = [](int unitIndex, int threadIndex, void *payload)
    {
        Payload          &p = *(Payload *)payload;
        AtomicLoadBalance workload(p.workload, p.range);

        while (workload.advance()) { (*p.f)(workload.begin, workload.end, unitIndex, threadIndex); }
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

    auto callback = [](int unitIndex, int threadIndex, void *payload)
    {
        Payload          &p = *(Payload *)payload;
        AtomicLoadBalance workload(p.workload, p.range);

        while (workload.advance()) { p.f(workload.begin, workload.end, unitIndex, threadIndex); }
    };
    auto deleter = [](int numUnits, int threadIndex, void *payload)
    {
        Payload *p = (Payload *)payload;
        p->e(numUnits, threadIndex);
        delete p;
    };

    Payload *payload = new Payload{std::forward<Func1>(func), std::forward<Func2>(epilogue), range};

    return scheduler->parallelizeAsync(num_threads, payload, callback, deleter);
}

template <typename Int, typename Func1>
Scheduler::TaskTracker parallel_for_async(const blocked_range<Int> &range, Func1 &&func,
                                          int num_threads = Scheduler::k_all, Scheduler *scheduler = nullptr)
{
    return parallel_for_async(
        range, func, [](int, int) {}, num_threads, scheduler);
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

    return scheduler->parallelizeAsync(1, payload, callback, deleter);
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

    return scheduler->parallelizeAsync(1, payload, callback, deleter);
}
