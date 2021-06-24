//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/vector.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace nanogui
{

template <typename Value, size_t Size>
inline void from_json(const json &j, Array<Value, Size> &a)
{
    if (j.is_object())
        throw std::runtime_error(
            fmt::format("Can't parse length {} Array. Expecting a json array, but got a json object.", Size));

    if (j.size() == 1)
    {
        if (j.is_array())
            spdlog::info(fmt::format("Incorrect array size when trying to parse an Array. "
                                     "Expecting {} values but only found 1. "
                                     "Creating an Array of all '{}'s.\n",
                                     Size, j.get<Value>()));
        a = Array<Value, Size>(j.get<Value>());
        return;
    }
    else if (Size != j.size())
        throw std::runtime_error(fmt::format("Incorrect array size when trying to parse an Array. "
                                             "Expecting {} values but found {} here:\n{}",
                                             Size, (int)j.size(), j.dump(4)));

    for (size_t i = 0; i < j.size(); ++i) j.at(i).get_to(a[i]);
}

template <typename Value, size_t Size>
inline void to_json(json &j, const Array<Value, Size> &a)
{
    j = std::vector<Value>(&a[0], &a[0] + Size);
}

} // namespace nanogui