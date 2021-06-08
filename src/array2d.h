//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <vector>

//! Generic, resizable, 2D array class.
template <typename T>
class Array2D
{
public:
    //@{ \name Constructors and destructors
    Array2D();                                               // empty array, 0 by 0 elements
    Array2D(int size_x, int size_y, const T &value = T(0.)); // size_x by size_y elements

    Array2D(const Array2D &o)
    {
        m_data     = o.m_data;
        m_sizes[0] = o.m_sizes[0];
        m_sizes[1] = o.m_sizes[1];
    }
    Array2D &operator=(const Array2D &o)
    {
        m_data     = o.m_data;
        m_sizes[0] = o.m_sizes[0];
        m_sizes[1] = o.m_sizes[1];
        return *this;
    };
    //@}

    //@{ \name Element access
    T &      operator()(int x, int y) { return m_data[y * m_sizes[0] + x]; }
    const T &operator()(int x, int y) const { return m_data[y * m_sizes[0] + x]; }
    T &      operator()(int i) { return m_data[i]; }
    const T &operator()(int i) const { return m_data[i]; }
    const T *data() const { return &(m_data[0]); }
    T *      data() { return &(m_data[0]); }
    //@}

    //@{ \name Dimension sizes
    int width() const { return m_sizes[0]; }
    int height() const { return m_sizes[1]; }
    int size() const { return m_sizes[0] * m_sizes[1]; }
    int size(int d) const { return m_sizes[d]; }
    int size_x() const { return m_sizes[0]; }
    int size_y() const { return m_sizes[1]; }

    Array2D &swapped_dims()
    {
        std::swap(m_sizes[0], m_sizes[1]);
        return *this;
    }
    //@}

    void resize(int size_x, int size_y, const T &value = T(0.));
    void reset(const T &value = T(0.));
    void operator=(const T &);

protected:
    std::vector<T> m_data;
    int            m_sizes[2];
};

template <typename T>
inline Array2D<T>::Array2D() : m_data()
{
    m_sizes[0] = m_sizes[1] = 0;
}

template <typename T>
inline Array2D<T>::Array2D(int size_x, int size_y, const T &value) : m_data(size_x * size_y, value)
{
    m_sizes[0] = size_x;
    m_sizes[1] = size_y;
}

template <typename T>
inline void Array2D<T>::resize(int size_x, int size_y, const T &value)
{
    if (size_x == m_sizes[0] && size_y == m_sizes[1])
        return;

    m_data.resize(size_x * size_y, value);
    m_sizes[0] = size_x;
    m_sizes[1] = size_y;
}

template <typename T>
inline void Array2D<T>::reset(const T &value)
{
    for (int i = 0; i < size(); ++i) m_data[i] = value;
}

template <typename T>
inline void Array2D<T>::operator=(const T &value)
{
    reset(value);
}

using Array2Di = Array2D<int>;
using Array2Dd = Array2D<double>;
using Array2Df = Array2D<float>;
