//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <algorithm> // for min, max
#include <iostream>
#include <limits> // for numeric_limits

//! Represents a bounded interval in higher dimensions.
/*!
    Box is an N-D interval.
*/
template <typename Vec_, typename Value_, size_t Dims_>
class Box
{
public:
    static constexpr size_t Dims = Dims_;
    using Value                  = Value_;
    using Vec                    = Vec_;
    using BoxT                   = Box<Vec, Value, Dims>;

    Vec min; //!< The lower-bound of the interval
    Vec max; //!< The upper-bound of the interval

    //-----------------------------------------------------------------------
    //@{ \name Constructors
    //-----------------------------------------------------------------------
    Box() { make_empty(); }
    Box(const Vec &point) : min(point), max(point) {}
    Box(const Vec &minT, const Vec &maxT) : min(minT), max(maxT) {}

    template <typename T>
    Box(const Box<T> &box) : min((Vec)box.min), max((Vec)box.max)
    {
    }

    template <typename T, typename S>
    Box(const Box<T> &box1, const Box<S> &box2) : min((Vec)box1.min), max((Vec)box1.max)
    {
        enclose((BoxT)box2);
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Equality and inequality
    //-----------------------------------------------------------------------
    bool operator==(const BoxT &other) const { return (min == other.min && max == other.max); }
    bool operator!=(const BoxT &other) const { return (min != other.min || max != other.max); }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Box manipulation
    //-----------------------------------------------------------------------
    BoxT &make_empty()
    {
        min = Vec(std::numeric_limits<Value>::max());
        max = Vec(std::numeric_limits<Value>::lowest());
        return *this;
    }

    /// Ensures that min[i] <= max[i] for each dimension i.
    BoxT &make_valid()
    {
        for (size_t i = 0; i < Dims; ++i)
            if (min[i] > max[i])
                std::swap(min[i], max[i]);
        return *this;
    }
    BoxT &expand(Value d)
    {
        min -= d;
        max += d;
        return *this;
    }
    BoxT &expand(const Vec &d)
    {
        min -= d;
        max += d;
        return *this;
    }
    BoxT &expand(const BoxT &d)
    {
        min -= d.min;
        max += d.max;
        return *this;
    }
    BoxT &set_size(const Vec &s)
    {
        max = min + s;
        return *this;
    }
    BoxT &enclose(const Vec &point)
    {
        for (size_t i = 0; i < Dims; ++i)
        {
            min[i] = std::min(point[i], min[i]);
            max[i] = std::max(point[i], max[i]);
        }
        return *this;
    }
    BoxT &enclose(const BoxT &box)
    {
        for (size_t i = 0; i < Dims; ++i)
        {
            min[i] = std::min(box.min[i], min[i]);
            max[i] = std::max(box.max[i], max[i]);
        }
        return *this;
    }
    BoxT &intersect(const BoxT &box)
    {
        for (size_t i = 0; i < Dims; ++i)
        {
            min[i] = std::max(box.min[i], min[i]);
            max[i] = std::min(box.max[i], max[i]);
        }
        return *this;
    }
    BoxT &move_by(const Vec &offset)
    {
        min += offset;
        max += offset;
        return *this;
    }
    BoxT &move_min_to(const Vec &newMin)
    {
        Vec diff(newMin - min);
        min = newMin;
        max += diff;
        return *this;
    }
    BoxT &move_max_to(const Vec &newMax)
    {
        Vec diff(newMax - max);
        max = newMax;
        min += diff;
        return *this;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Query functions
    //-----------------------------------------------------------------------
    Vec size() const { return max - min; }
    Vec center() const { return (max + min) / Value(2); }
    Vec clamp(Vec point) const
    {
        for (size_t i = 0; i < Dims; ++i) point[i] = std::min(std::max(point[i], min[i]), max[i]);
        return point;
    }
    template <bool Inclusive = false>
    bool contains(const Vec &point) const
    {
        if constexpr (Inclusive)
        {
            for (size_t i = 0; i < Dims; ++i)
                if (point[i] <= min[i] || point[i] >= max[i])
                    return false;
            return true;
        }
        else
        {
            for (size_t i = 0; i < Dims; ++i)
                if (point[i] < min[i] || point[i] > max[i])
                    return false;
            return true;
        }
    }
    template <bool Inclusive = false>
    bool intersects(const BoxT &box) const
    {
        if constexpr (Inclusive)
        {
            for (size_t i = 0; i < Dims; ++i)
                if (box.max[i] <= min[i] || box.min[i] >= max[i])
                    return false;
            return true;
        }
        else
        {
            for (size_t i = 0; i < Dims; ++i)
                if (box.max[i] < min[i] || box.min[i] > max[i])
                    return false;
            return true;
        }
    }
    Value volume() const
    {
        Value ret_val(1);
        Vec   s = size();
        for (size_t i = 0; i < Dims; ++i) ret_val *= s[i];
        return ret_val;
    }
    Value area() const
    {
        Value ret_val(0);
        Vec   s = size();
        for (size_t i = 0; i < Dims; ++i)
            for (size_t j = i + 1; j < Dims; j++) ret_val += s[i] * s[j];
        return 2 * ret_val;
    }
    size_t major_axis() const
    {
        size_t major = 0;
        Vec    s     = size();

        for (size_t i = 1; i < Dims; ++i)
            if (s[i] > s[major])
                major = i;
        return major;
    }
    void bounding_sphere(Vec *center, Value *radius) const
    {
        *center = center();
        *radius = intersects(*center) ? (*center - max).length() : 0.0f;
    }
    //@}

    //-----------------------------------------------------------------------
    //@{ \name Classification
    //-----------------------------------------------------------------------
    bool has_volume() const
    {
        for (size_t i = 0; i < Dims; ++i)
            if (max[i] <= min[i])
                return false;
        return true;
    }
    bool is_empty() const
    {
        for (size_t i = 0; i < Dims; ++i)
            if (max[i] < min[i])
                return true;
        return false;
    }
    //@}
};

template <typename Vec, typename Value, size_t Dims>
inline std::ostream &operator<<(std::ostream &o, const Box<Vec, Value, Dims> &b)
{
    return o << "[(" << b.min << "),(" << b.max << ")]";
}

// define some common types
using Box1f = Box<float1>;
using Box1d = Box<double1>;
using Box1i = Box<int1>;

using Box2f = Box<float2>;
using Box2d = Box<double2>;
using Box2i = Box<int2>;

using Box3f = Box<float3>;
using Box3d = Box<double3>;
using Box3i = Box<int3>;

using Box4f = Box<float4>;
using Box4d = Box<double4>;
using Box4i = Box<int4>;
