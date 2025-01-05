//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <chrono>

//! Simple timer with millisecond precision
/*!
    This class is convenient for collecting performance data
*/
class Timer
{
public:
    //! Create a new timer and reset it
    Timer() { reset(); }

    //! Reset the timer to the current time
    void reset() { start = std::chrono::system_clock::now(); }

    //! Return the number of milliseconds elapsed since the timer was last reset
    double elapsed() const
    {
        auto now      = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
        return (double)duration.count();
    }

    //! Return the number of milliseconds elapsed since the timer was last reset and then reset it
    double lap()
    {
        auto now      = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
        start         = now;
        return (double)duration.count();
    }

private:
    std::chrono::system_clock::time_point start;
};
