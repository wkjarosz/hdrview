//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "linalg.h"
#include <ImathMatrix.h>
#include <ImathVec.h>

// linalg and Imath use the same memory layout, so we can convert via linalg's pointer constructors
// Conversion function for Imath::Vec2 to linalg::vec
template <typename T>
linalg::vec<T, 2> to_linalg(const Imath::Vec2<T> &v)
{
    return linalg::vec<T, 2>(&v.x);
}

// Conversion function for Imath::Vec3 to linalg::vec
template <typename T>
linalg::vec<T, 3> to_linalg(const Imath::Vec3<T> &v)
{
    return linalg::vec<T, 3>(&v.x);
}

// Conversion function for Imath::Vec4 to linalg::vec
template <typename T>
linalg::vec<T, 4> to_linalg(const Imath::Vec4<T> &v)
{
    return linalg::vec<T, 4>(&v.x);
}

// Conversion function for Imath::Matrix22 to linalg::mat
template <typename T>
linalg::mat<T, 2, 2> to_linalg(const Imath::Matrix22<T> &m)
{
    return linalg::mat<T, 2, 2>(&m.x[0][0]);
}

// Conversion function for Imath::Matrix33 to linalg::mat
template <typename T>
linalg::mat<T, 3, 3> to_linalg(const Imath::Matrix33<T> &m)
{
    return linalg::mat<T, 3, 3>(&m.x[0][0]);
}

// Conversion function for Imath::Matrix44 to linalg::mat
template <typename T>
linalg::mat<T, 4, 4> to_linalg(const Imath::Matrix44<T> &m)
{
    return linalg::mat<T, 4, 4>(&m.x[0][0]);
}
