//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <limits>    // for numeric_limits
#include <algorithm> // for min, max


//! Represents a bounded interval in higher dimensions.
/*!
    Box is an N-D interval.
*/
template <typename Vec, typename Value, size_t Dims>
class Box
{
public:
    using BoxT = Box<Vec, Value, Dims>;

    Vec min;            //!< The lower-bound of the interval
    Vec max;            //!< The upper-bound of the interval


    //-----------------------------------------------------------------------
    //@{ \name Constructors
    //-----------------------------------------------------------------------
    Box() {make_empty();}
    Box(const Vec & point) : min(point), max(point) {}
    Box(const Vec & minT, const Vec & maxT) : min(minT), max(maxT) {}
    template <typename T>
    Box(const Box<T> & box) : min((Vec)box.min), max((Vec)box.max) {}
    template <typename T, typename S>
    Box(const Box<T> & box1, const Box<S> & box2) :
        min((Vec)box1.min), max((Vec)box1.max)
    {
        enclose((BoxT)box2);
    }
    //@}

    
    //-----------------------------------------------------------------------
    //@{ \name Equality and inequality
    //-----------------------------------------------------------------------
    bool operator==(const BoxT & other) const
        {return(min == other.min && max == other.max);}
    bool operator!=(const BoxT & other) const
        {return(min != other.min || max != other.max);}
    //@}

    
    //-----------------------------------------------------------------------
    //@{ \name Box manipulation
    //-----------------------------------------------------------------------
    void make_empty()
    {
        min = Vec(std::numeric_limits<Value>::max());
        max = Vec(std::numeric_limits<Value>::lowest());
    }
    void enclose(const Vec & point)
    {
        for (size_t i = 0; i < Dims; ++i)
        {
            min[i] = std::min(point[i], min[i]);
            max[i] = std::max(point[i], max[i]);
        }
    }
    BoxT expanded(Value d) const
    {
        return BoxT(min-d, max+d);
    }
    BoxT expanded(const Vec & d) const
    {
        return BoxT(min-d, max+d);
    }
    BoxT expanded(const BoxT & d) const
    {
        return BoxT(min-d.min, max+d.max);
    }
    void enclose(const BoxT & box)
    {
        for (size_t i = 0; i < Dims; ++i)
        {
            min[i] = std::min(box.min[i], min[i]);
            max[i] = std::max(box.max[i], max[i]);
        }
    }
    void intersect(const BoxT & box)
    {
        for (size_t i = 0; i < Dims; ++i)
        {
            min[i] = std::max(box.min[i], min[i]);
            max[i] = std::min(box.max[i], max[i]);
        }
    }
    void move_min_to(const Vec &newMin)
    {
        Vec diff(newMin - min);
        min = newMin;
        max += diff;
    }
    void move_max_to(const Vec &newMax)
    {
        Vec diff(newMax - max);
        max = newMax;
        min += diff;
    }
    void make_valid()
    {
        for (size_t i = 0; i < Dims; ++i)
        {
            if (min[i] > max[i])
                std::swap(min[i], max[i]);
        }
    }
    //@}

    
    //-----------------------------------------------------------------------
    //@{ \name Query functions
    //-----------------------------------------------------------------------
    Vec  size() const {return max-min;}
    Vec  center() const {return (max+min)/Value(2);}
    Vec  clamp(Vec point) const
    {
        for (size_t i = 0; i < Dims; ++i)
            point[i] = std::min(std::max(point[i], min[i]), max[i]);
        return point;
    }
    bool contains(const Vec & point, bool proper = false) const
    {
        if (proper)
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
    bool intersects(const BoxT & box, bool proper = false) const
    {
        if (proper)
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
        Vec s = size();
        for (size_t i = 0; i < Dims; ++i)
            ret_val *= s[i];
        return ret_val;
    }
    Value area() const
    {
        Value ret_val(0);
        Vec s = size();
        for (size_t i = 0; i < Dims; ++i)
            for (size_t j = i+1; j < Dims; j++)
                ret_val += s[i]*s[j];
        return 2*ret_val;
    }
    size_t major_axis() const
    {
        size_t major = 0;
        Vec s = size();
    
        for (size_t i = 1; i < Dims; ++i)
            if (s[i] > s[major])
                major = i;
        return major;
    }
    void bounding_sphere(Vec* center, Value* radius) const
    {
        *center = center();
        *radius = intersects(*center) ?(*center - max).length() : 0.0f;
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
inline std::ostream&
operator<<(std::ostream &o, const Box<Vec,Value,Dims> &b)
{
    return o << "[(" << b.min << "),(" << b.max << ")]";
}

