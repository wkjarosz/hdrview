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
void parallel_for(int begin, int end, int step, function<void(int, size_t)> body, bool serial)
{
    atomic<int> nextIndex;
    nextIndex = begin;

#if defined(__EMSCRIPTEN__) && !defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    serial = true;
#endif

    auto                 policy  = serial ? std::launch::deferred : std::launch::async;
    size_t               numCPUs = thread::hardware_concurrency();
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
    for (auto &f : futures)
        f.get();
}

void parallel_for(int begin, int end, int step, function<void(int)> body, bool serial)
{
    parallel_for(
        begin, end, step, [&body](int i, size_t) { body(i); }, serial);
}