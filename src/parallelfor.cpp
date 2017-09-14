//
// Created by Wojciech Jarosz on 9/13/17.
//

#include "parallelfor.h"
#include <future>
#include <vector>

using namespace std;

// adapted from http://www.andythomason.com/2016/08/21/c-multithreading-an-effective-parallel-for-loop/
// license unknown, presumed public domain
void parallel_for(size_t begin, size_t end, function<void(size_t, size_t)> body)
{
	atomic<size_t> nextIndex;
	nextIndex = begin;

	size_t numCPUs = thread::hardware_concurrency();
	vector< future<void> > futures(numCPUs);
	for (size_t cpu = 0; cpu != numCPUs; ++cpu)
	{
		futures[cpu] = async(
			launch::async,
			[cpu, &nextIndex, end, &body]()
			{
				// just iterate, grabbing the next available atomic index in the range [begin, end)
				while (true)
				{
					size_t i = nextIndex++;
					if (i >= end) break;
					body(i, cpu);
				}
			});
	}
	for (size_t cpu = 0; cpu != numCPUs; ++cpu)
		futures[cpu].get();
}

void parallel_for(size_t begin, size_t end, function<void(size_t)> body)
{
	return parallel_for(begin, end, [&body](size_t i, size_t){body(i);});
}