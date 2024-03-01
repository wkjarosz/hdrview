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

struct Scheduler::Task
{
    inline Task(int numUnits, void *data, TaskFn fn, TaskFn epilogue = nullptr) :
        data(data), fn(fn), epilogue(epilogue), parent(nullptr), numUnits(numUnits)
    {
    }

    // These fields are read-only, they are defined on task creation and never change thereafter.
    void  *data;     //< The task data to be passed as argument to the task function.
    TaskFn fn;       //< The task function, executed by as many threads as numUnits.
    TaskFn epilogue; //< The optional task epilogue function, executed only once upon task completion.
    Task  *parent;   //< The parent task in case of nested parallelism.
    int    numUnits; //< This is number of units of work. Ideally, this shouldn't exceed the width
                     //  of the hardware concurrency.

    // The following fields change value during the lifetime of the task.
    std::atomic<int> completed    = 0; //< How many units of work are completed.
    std::atomic<int> refcount     = 0; //< Traditional ref-counting to govern the task lifetime.
    std::atomic<int> dependencies = 1; //< How many nested tasks are still running. Set to one because
                                       //  each task is considered to depend on its own completion too.

    // The insertion of an invalid task in the scheduler queue causes one of its threads to terminate.
    // Besides that, tasks are never invalid by design.
    bool valid() const { return numUnits != 0; }
};

/////////////////////////////////////////////////////////////////////////////////
// Scheduler static members and globals
thread_local int              Scheduler::m_threadIndex          = Scheduler::k_invalidThreadIndex;
int                           Scheduler::m_nextGuestThreadIndex = 0;
thread_local Scheduler::Task *Scheduler::m_threadTask           = nullptr;

static Scheduler *s_singleton = nullptr;
static std::mutex s_singleton_lock;

Scheduler *Scheduler::singleton()
{
    std::unique_lock<std::mutex> guard(s_singleton_lock);

    if (!s_singleton)
    {
        s_singleton = new Scheduler();
        s_singleton->start();
    }

    return s_singleton;
}

Scheduler::Scheduler() {}

Scheduler::~Scheduler() {}

static int getNestingLevel(const Scheduler::Task *task)
{
    int level = 0;
    while (task)
    {
        task = task->parent;
        level++;
    }
    return level;
}

int Scheduler::getNestingLevel() { return ::getNestingLevel(m_threadTask); }

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
        deleteTask(task);

        // recursion
        if (parent)
            unbind(parent);
        return true;
    }

    return false;
}

// Memory management functions: tasks are allocated and freed by the same module, to safeguard
// from heap corruption across DSO boundaries.
Scheduler::Task *Scheduler::newTask(void *data, TaskFn fn, TaskFn epilogue, int numUnits)
{
    Task *task = new Task(numUnits, data, fn, epilogue);
    return task;
}

void Scheduler::deleteTask(Task *task)
{
    if (!task)
        return;
    assert(task->refcount.load() == 0);

    delete task;
}

// Internal function to track dependencies between nested tasks. By binding a parent task,
// we make it wait on the completion of nested task.
static void bindParents(Scheduler::Task *task)
{
    while (task)
    {
        task->dependencies++;
        task = task->parent;
    }
}
static void unbindParents(Scheduler::Task *task)
{
    while (task)
    {
        task->dependencies--;
        task = task->parent;
    }
}

void Scheduler::start(int nThreads)
{
    assert(m_workers.empty() && "Assure scheduler is not initialized twice!");
    {
        auto nLogicalThreads = std::thread::hardware_concurrency();
        if (nThreads == k_all)
            nThreads = nLogicalThreads;
        else
            nThreads = std::min<int>(nThreads, nLogicalThreads);
        // The reason we cap nThreads to the number of logical threads in the system is to avoid
        // any conflict in the threadIndex assignment for guest threads (calling threads) entering
        // the scheduler during calls to TaskTracker::wait().
        // If the thread index is above the hardware concurrency, it means it is a guest thread,
        // independently on how many threads a scheduler has. This is not important with just one
        // scheduler, but it becomes important when there are more, each with a different count of
        // threads.
    }

    m_nextGuestThreadIndex = nThreads;
    m_workers.resize(nThreads, nullptr);

    // Spawn worker threads
    for (int threadIndex = 0; threadIndex < (int)nThreads; ++threadIndex)
    {
        m_workers[threadIndex] = new std::thread(
            [&, threadIndex]
            {
                // Initialize thread index, worker threads have numbers in the range [0, nThreads-1]
                // Guest threads will get their own unique indices.
                m_threadIndex = threadIndex;

                // printf("Spawn thread %d\n", threadIndex);
                while (true)
                {
                    TaskUnit workUnit;
                    // bool     removed = false;
                    {
                        // usual thread-safe queue code:
                        std::unique_lock<std::mutex> lock(m_workMutex);
                        m_workSignal.wait(lock, [&] { return !m_work.empty(); });

                        // Transfer ownership to this thread, unbind tasks after running
                        workUnit = m_work.front();
                        m_work.pop_front();
                    }

                    // if the task is invalid, it means we are asked to abort:
                    if (workUnit.task && !workUnit.task->valid())
                    {
                        unbind(workUnit.task);
                        break;
                    }

                    runTask(workUnit.task, workUnit.index, threadIndex);
                    unbind(workUnit.task);
                }
            });
    }
}

void Scheduler::runTask(Task *task, int unitIndex, int threadIndex)
{
    Task *oldTask = m_threadTask;
    m_threadTask  = task;

    task->fn(unitIndex, threadIndex, task->data);

    int done = ++task->completed;
    if (done == task->numUnits)
    {
        if (task->epilogue)
            task->epilogue(task->numUnits, threadIndex, task->data);

        unbindParents(task);
    }

    m_threadTask = oldTask;
}

void Scheduler::pickWorkUnit(int nestingLevel, int threadIndex)
{
    TaskUnit workUnit;
    {
        std::unique_lock<std::mutex> lock(m_workMutex);
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
        //       pick only workUnits that are in the current branch; recursively looking at the
        //       Task::parent should do the trick. We could also look ahead in the queue to
        //       search for such a workUnit. Up to now this proved not to be needed; I am post-
        //       poning any feature adding significant complexity to the system if not strictly
        //       required.
        workUnit                 = m_work.front();
        int workUnitNestingLevel = ::getNestingLevel(workUnit.task);
        if (workUnitNestingLevel < nestingLevel)
            return;

        m_work.pop_front();
    }

    // if the task is invalid, it means we are asked to abort:
    if (workUnit.task && !workUnit.task->valid())
    {
        assert(false);
        unbind(workUnit.task);
        return;
    }

    runTask(workUnit.task, workUnit.index, threadIndex);
    unbind(workUnit.task);
}

void Scheduler::stop()
{
    if (m_workers.empty())
        return;

    // Push invalid tasks, one for each thread. The invalid task will make a thread to terminate
    {
        std::unique_lock<std::mutex> lock(m_workMutex);
        for (size_t i = 0; i < m_workers.size(); ++i)
        {
            Task *task = new Task(0, nullptr, nullptr);
            bind(task);
            m_work.push_back({task, 0});
        }
    }
    m_workSignal.notify_all();

    // Wait for threads to terminate
    for (std::thread *thread : m_workers) thread->join();

    m_workers.clear();
    assert(m_work.empty() && "Work queue should be empty");
}

Scheduler::TaskTracker Scheduler::async(int numUnits, void *data, TaskFn f, TaskFn epilogue, int reservedUnits,
                                        bool front)
{
    Task *task   = newTask(data, f, epilogue, numUnits);
    task->parent = m_threadTask;
    if (task->parent)
        bind(task->parent);
    bindParents(task->parent);

    // Get the future return value before we hand off the task
    TaskTracker result(task, this);

    numUnits -= reservedUnits;
    task->refcount += numUnits;
    // Store the task in the queue
    if (numUnits > 0)
    {
        std::unique_lock<std::mutex> lock(m_workMutex);
        if (front)
            while (--numUnits >= 0) m_work.push_front({task, reservedUnits + numUnits});
        else
            while (--numUnits >= 0) m_work.push_back({task, reservedUnits + numUnits});
    }

    // Wake as many thread as we need to work on the task
    m_workSignal.notify_all();

    return result;
}

void Scheduler::TaskTracker::wait()
{
    if (!task)
        return;

    int threadIndex = getOrAssignThreadIndex();

    // It is not allowed
    int nestingLevel = getNestingLevel();

    while (true)
    {
        if (task->dependencies.load() == 0)
            break;

        // Work stealing
        scheduler->pickWorkUnit(nestingLevel, threadIndex);
    }
    scheduler->unbind(task);
    task = nullptr, scheduler = nullptr;
}
