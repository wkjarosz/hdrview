//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "json.h"
#include <cstdint>
#include <optional>

/// Whether a maker-note tag's [offset, offset + size) lies inside a note of \p bound bytes.
/**
    Compared by subtraction: these are 32-bit file fields, and offset + size wraps where size_t is no wider
    (wasm32).
*/
bool maker_note_range_within(uint32_t offset, uint32_t size, uint32_t bound);

json        entry_to_json(void *entry, int boi, unsigned int ifd_idx_i = 0);
json        exif_to_json(const uint8_t *data_ptr, size_t data_size);
inline json exif_to_json(const std::vector<uint8_t> &data) { return exif_to_json(data.data(), data.size()); }

class Exif
{
public:
    Exif(const uint8_t *data_ptr = nullptr, size_t data_size = 0U);
    Exif(const std::vector<uint8_t> &data) : Exif(data.data(), data.size()) {}
    Exif(const Exif &other); /// Performs a deep copy
    Exif(Exif &&other) noexcept;
    Exif &operator=(const Exif &); /// Performs a deep copy
    Exif &operator=(Exif &&) noexcept;
    ~Exif();

    bool valid() const;
    void reset();

    size_t         size() const;
    const uint8_t *data() const;

    json to_json() const;

    /// Value of Apple maker-note tag \p wanted_tag, or nullopt if it is absent or not numeric.
    /**
        libexif has no decoder for Apple's maker note, so these tags never reach the ordinary EXIF accessors.
    */
    std::optional<double> apple_makernote_value(uint16_t wanted_tag) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};