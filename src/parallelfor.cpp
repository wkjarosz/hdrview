//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "parallelfor.h"
#include <future>
#include <vector>

using namespace std;

// adapted from http://www.andythomason.com/2016/08/21/c-multithreading-an-effective-parallel-for-loop/
// license unknown, presumed public domain
void parallel_for(int begin, int end, int step, function<void(int, size_t)> body, size_t num_threads)
{
    atomic<int> nextIndex;
    nextIndex = begin;

#if defined(__EMSCRIPTEN__)
    // shouldn't use this simple async-based parallel_for with emscripten since, even if compiled with pthread support,
    // the async would block on the mail thread, which is a no-no
    num_threads = 1;
#endif

    auto                 policy  = num_threads == 1 ? std::launch::deferred : std::launch::async;
    size_t               numCPUs = num_threads == 0 ? thread::hardware_concurrency() : num_threads;
    vector<future<void>> futures(numCPUs);
    for (size_t cpu = 0; cpu != numCPUs; ++cpu)
    {
        futures[cpu] = async(policy,
                             [cpu, &nextIndex, end, step, &body]()
                             {
                                 // just iterate, grabbing the next available atomic index in the range [begin, end)
                                 while (true)
                                 {
                                     int i = nextIndex.fetch_add(step);
                                     if (i >= end)
                                         break;
                                     body(i, cpu);
                                 }
                             });
    }
    for (auto &f : futures) f.get();
}

void parallel_for(int begin, int end, int step, function<void(int)> body, size_t num_threads)
{
    parallel_for(
        begin, end, step, [&body](int i, size_t) { body(i); }, num_threads);
}