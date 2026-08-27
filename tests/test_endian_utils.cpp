//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"

TEST_CASE("read_partial_as assembles a value narrower than its destination")
{
    // A DDS bitmasked pixel is 1 to 4 bytes wide, so the stored value is narrower than the uint32_t it
    // is assembled into -- read_as<uint32_t> would read four bytes whatever the width.
    const unsigned char bytes[] = {0x78, 0x56, 0x34, 0x12};

    CHECK(read_partial_as<uint32_t>(bytes, 1, Endian::Little) == 0x78u);
    CHECK(read_partial_as<uint32_t>(bytes, 2, Endian::Little) == 0x5678u);
    CHECK(read_partial_as<uint32_t>(bytes, 3, Endian::Little) == 0x345678u);
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Little) == 0x12345678u);

    CHECK(read_partial_as<uint32_t>(bytes, 1, Endian::Big) == 0x78u);
    CHECK(read_partial_as<uint32_t>(bytes, 2, Endian::Big) == 0x7856u);
    CHECK(read_partial_as<uint32_t>(bytes, 3, Endian::Big) == 0x785634u);
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Big) == 0x78563412u);

    // At the full width it has to agree with read_as, which is what the four-byte formats used before.
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Little) == read_as<uint32_t>(bytes, Endian::Little));
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Big) == read_as<uint32_t>(bytes, Endian::Big));

    // Reading nothing yields nothing rather than touching the pointer.
    CHECK(read_partial_as<uint32_t>(bytes, 0, Endian::Little) == 0u);

    // Assembling into a wider destination zero-extends rather than sign-extends.
    const unsigned char high[] = {0xff, 0xff};
    CHECK(read_partial_as<uint64_t>(high, 2, Endian::Little) == 0xffffu);
}
