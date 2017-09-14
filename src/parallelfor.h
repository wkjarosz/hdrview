//
// Created by Wojciech Jarosz on 9/13/17.
//
#pragma once

#include <functional>

/*!
 * @brief 		Executes the body of a for loop in parallel
 * @param begin	The starting index of the for loop
 * @param end 	One past the ending index of the for loop
 * @param body 	The body of the for loop as a lambda, taking two parameters: the iterator index in [begin,end), and the CPU number
 */
void parallel_for(size_t begin, size_t end, std::function<void(size_t, size_t)> body);

/*!
 * @brief	A version of the parallel_for accepting a body lambda that only takes the iterator index as a parameter
 */
void parallel_for(size_t begin, size_t end, std::function<void(size_t)> body);