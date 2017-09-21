//
// Created by Wojciech Jarosz on 9/13/17.
//

#include "parallelfor.h"
#include <future>
#include <vector>

using namespace std;

// adapted from http://www.andythomason.com/2016/08/21/c-multithreading-an-effective-parallel-for-loop/
// license unknown, presumed public domain
void parallel_for(int begin, int end, int step, function<void(int, size_t)> body)
{
	atomic<int> nextIndex;
	nextIndex = begin;

	size_t numCPUs = thread::hardware_concurrency();
	vector< future<void> > futures(numCPUs);
	for (size_t cpu = 0; cpu != numCPUs; ++cpu)
	{
		futures[cpu] = async(
			launch::async,
			[cpu, &nextIndex, end, step, &body]()
			{
				// just iterate, grabbing the next available atomic index in the range [begin, end)
				while (true)
				{
					int i = nextIndex+=step;
					if (i >= end) break;
					body(i, cpu);
				}
			});
	}
	for (size_t cpu = 0; cpu != numCPUs; ++cpu)
		futures[cpu].get();
}

void parallel_for(int begin, int end, int step, function<void(int)> body)
{
	parallel_for(begin, end, step, [&body](int i, size_t){body(i);});
}