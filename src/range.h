#pragma once

/*!
 * @class Range range.h
 * @brief Python-style range: iterates from min to max in range-based for loops
 *
 * To use:
 *  @code
 *     for(int i = 0; i < 100; i++) { ... }             // old way
 *     for(auto i : range(100))     { ... }             // new way
 *
 *     for(int i = 10; i < 100; i+=2)  { ... }          // old way
 *     for(auto i : range(10, 100, 2)) { ... }          // new way
 *
 *     for(float i = 3.5f; i > 1.5f; i-=0.01f) { ... } // old way
 *     for(auto i : range(3.5f, 1.5f, -0.01f)) { ... } // new way
 * @endcode
*/
template<typename T>
class Range
{
public:
	class Iterator
	{
	public:
		Iterator(T pos, T step) : m_pos(pos), m_step(step) {}

		bool operator!=(const Iterator &o) const { return (o.m_pos - m_pos) * m_step > T(0);}
		Iterator &operator++() {m_pos += m_step; return *this;}
		Iterator operator++(int) {Iterator copy(*this); operator++(); return copy;}
		T operator*() const {return m_pos;}
	private:
		T m_pos, m_step;
	};

	Range(T start, T end, T step = T(1))
		: m_start(start), m_end(end), m_step(step) {}

	Iterator begin() const {return Iterator(m_start, m_step);}
	Iterator end() const {return Iterator(m_end, m_step);}

private:
	T m_start, m_end, m_step;
};

template<typename T>
Range<T> range(T end) {return Range<T>(T(0), end, T(1));}

template<typename T>
Range<T> range(T start, T end, T step = T(1)) {return Range<T>(start, end, step);}

