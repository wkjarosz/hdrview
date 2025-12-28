//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "json.h"
#include <cstdint>

json        entry_to_json(void *entry, int boi, unsigned int ifd_idx_i = 0);
json        exif_to_json(const uint8_t *data_ptr, size_t data_size);
inline json exif_to_json(const std::vector<uint8_t> &data) { return exif_to_json(data.data(), data.size()); }

class Exif
{
public:
    Exif(const uint8_t *data_ptr = nullptr, size_t data_size = 0U);
    Exif(const std::vector<uint8_t> &data) : Exif(data.data(), data.size()) {}
    Exif(Exif &&other) noexcept;
    Exif &operator=(const Exif &);
    Exif &operator=(Exif &&) noexcept;
    ~Exif();

    bool valid() const;
    void reset();

    size_t         size() const;
    const uint8_t *data() const;

    json to_json() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};