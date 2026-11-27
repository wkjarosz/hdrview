//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"

TEST_CASE("read_partial_as and read_as agree with a direct assembly at every width and byte order")
{
    // A DDS bitmasked pixel is 1 to 4 bytes wide, so read_as<uint32_t> is not what reads one. The reference is
    // spelled out per byte, so it does not depend on the host's own endianness, and covers every width each type
    // holds at every offset the array allows, over bytes from 0x00 to 0xff with the high bit both clear and set. At
    // full width, read_as must give the same value.
    const unsigned char bytes[] = {0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xff};

    auto sweep = [&](auto zero)
    {
        using T = decltype(zero);
        CAPTURE(sizeof(T));
        for (size_t width = 0; width <= sizeof(T); ++width)
            for (size_t offset = 0; offset + width <= sizeof(bytes); ++offset)
            {
                CAPTURE(width);
                CAPTURE(offset);
                T little = 0, big = 0;
                for (size_t i = 0; i < width; ++i)
                {
                    little |= T(bytes[offset + i]) << (8 * i);
                    big = T(big << 8) | bytes[offset + i];
                }
                CHECK(read_partial_as<T>(bytes + offset, width, Endian::Little) == little);
                CHECK(read_partial_as<T>(bytes + offset, width, Endian::Big) == big);
                if (width == sizeof(T))
                {
                    CHECK(read_as<T>(bytes + offset, Endian::Little) == little);
                    CHECK(read_as<T>(bytes + offset, Endian::Big) == big);
                }
            }
    };
    sweep(uint8_t{0});
    sweep(uint16_t{0});
    sweep(uint32_t{0});
    sweep(uint64_t{0});
}

