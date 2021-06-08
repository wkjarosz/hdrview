//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <functional>

/*!
 * @brief 			Executes the body of a for loop in parallel
 * @param begin		The starting index of the for loop
 * @param end 		One past the ending index of the for loop
 * @param step 		How much to increment at each iteration when moving from begin to end
 * @param body 		The body of the for loop as a lambda, taking two parameters: the iterator index in [begin,end), and the
 * CPU number
 * @param serial 	Force the loop to execute in serial instead of parallel
 */
void parallel_for(int begin, int end, int step, std::function<void(int, size_t)> body, bool serial = false);

/*!
 * @brief	A version of the parallel_for accepting a body lambda that only takes the iterator index as a parameter
 */
void parallel_for(int begin, int end, int step, std::function<void(int)> body, bool serial = false);

// adapted from http://www.andythomason.com/2016/08/21/c-multithreading-an-effective-parallel-for-loop/
// license unknown, presumed public domain
inline void parallel_for(int begin, int end, std::function<void(int, size_t)> body, bool serial = false)
{
    parallel_for(begin, end, 1, body);
}

inline void parallel_for(int begin, int end, std::function<void(int)> body, bool serial = false)
{
    parallel_for(begin, end, 1, body, serial);
}