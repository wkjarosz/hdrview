//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "linalg.h"
#include "nlohmann/json.hpp"
#include <fmt/core.h>

namespace linalg
{

/// Serialize a Vec3<N,T> to json
template <class T, int N>
inline void to_json(nlohmann::json &j, const vec<T, N> &v)
{
    j = std::vector<T>(&(v[0]), &(v[0]) + N);
}

/// parse a Vec<N,T> from json
template <class T, int N>
inline void from_json(const nlohmann::json &j, vec<T, N> &v)
{
    if (j.is_object())
        throw std::invalid_argument(
            fmt::format("Can't parse a vec{}. Expecting a json array, but got a json object.", N));

    size_t size = std::min(j.size(), (size_t)N);
    if (size != j.size())
        spdlog::error(fmt::format("Incorrect array size when trying to parse a vec{}. "
                                  "Expecting {} values but found {}. Will only read the first {} elements here:\n{}",
                                  N, N, (int)j.size(), size, j.dump(4)));

    for (size_t i = 0; i < size; ++i) j.at(i).get_to(v[i]);
}

} // namespace linalg