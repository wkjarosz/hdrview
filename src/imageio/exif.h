//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "json.h"
#include <cstdint>
#include <optional>

/*!
    Whether a maker-note tag's [offset, offset + size) lies inside a note of \p bound bytes.

    All three are 32-bit fields read straight out of the file, so the arithmetic is 32-bit by nature and
    adding the first two can wrap. Widening to size_t hides that only where size_t is wider -- not on the
    wasm32 build, where an offset near 4 GB wrapped past the check and the read landed outside the note.
    Compared by subtraction instead, which cannot wrap at any width.
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

    //! Value of an Apple maker-note tag, when this file carries an Apple maker note holding it.
    /*!
        libexif has no decoder for Apple's maker note, so these tags never reach the ordinary EXIF
        accessors. Tags 0x21 (HDR headroom) and 0x30 (HDR gain) parameterize Apple's HDR gain maps.

        \param wanted_tag  Maker-note tag to look up
        \return            The tag's value as a double, or nullopt if the tag is absent or not numeric
    */
    std::optional<double> apple_makernote_value(uint16_t wanted_tag) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};