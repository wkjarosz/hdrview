//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <vector>

//! Generic, resizable, 2D array class.
template <typename T>
class Array2D
{
public:
    //@{ \name Constructors and destructors
    Array2D() : m_data(), m_size(0) {} // empty array, 0 by 0 elements
    Array2D(int2 size, const T &value = T(0.)) :
        m_data(size.x * size.y, value), m_size(size) {} // size.x by size.y elements
    Array2D(int size_x, int size_y, const T &value = T(0.)) :
        Array2D(int2{size_x, size_y}, value) {} // size.x by size.y elements

    Array2D(const Array2D &o) : m_data(o.m_data), m_size(o.m_size) {}
    Array2D &operator=(const Array2D &o)
    {
        m_data = o.m_data;
        m_size = o.m_size;
        return *this;
    };

    // // prevent copying by deleting the copy constructor and copy assignment operator
    // Array2D(const Array2D &)            = delete;
    // Array2D &operator=(const Array2D &) = delete;

    // Move constructor
    Array2D(Array2D &&other) noexcept : m_data(std::move(other.m_data)), m_size(other.m_size)
    {
        // Reset the source object
        other.m_size = int2{0};
    }

    // Move assignment operator
    Array2D &operator=(Array2D &&other) noexcept
    {
        if (this != &other)
        {
            // Use move semantics for the vector
            m_data = std::move(other.m_data);

            // Copy other members
            m_size = other.m_size;

            // Reset the source object
            other.m_size = int2{0};
        }
        return *this;
    }

    //@}

    //@{ \name Element access
    T       &operator()(int x, int y) { return m_data[y * m_size.x + x]; }
    const T &operator()(int x, int y) const { return m_data[y * m_size.x + x]; }
    T       &operator()(int2 p) { return operator()(p.x, p.y); }
    const T &operator()(int2 p) const { return operator()(p.x, p.y); }
    T       &operator()(int i) { return m_data[i]; }
    const T &operator()(int i) const { return m_data[i]; }
    const T *data() const { return &(m_data[0]); }
    T       *data() { return &(m_data[0]); }
    //@}

    //@{ \name Dimension sizes
    int  num_elements() const { return m_size.x * m_size.y; }
    int2 size() const { return m_size; }
    int  width() const { return m_size.x; }
    int  height() const { return m_size.y; }
    //@}

    void resize(int2 size, const T &value = T(0.));
    void reset(const T &value = T(0.));
    void operator=(const T &);

protected:
    std::vector<T> m_data;
    int2           m_size;
};

template <typename T>
inline void Array2D<T>::resize(int2 size, const T &value)
{
    if (size == m_size)
        return;

    m_data.resize(size.x * size.y, value);
    m_size = size;
}

template <typename T>
inline void Array2D<T>::reset(const T &value)
{
    for (int i = 0; i < num_elements(); ++i)
        m_data[i] = value;
}

template <typename T>
inline void Array2D<T>::operator=(const T &value)
{
    reset(value);
}

using Array2Di = Array2D<int>;
using Array2Dd = Array2D<double>;
using Array2Df = Array2D<float>;
