//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"

#include <initializer_list>

TEST_CASE("read_partial_as agrees with a direct assembly at every width and byte order")
{
    // A DDS bitmasked pixel is 1 to 4 bytes wide, so read_as<uint32_t> is not what reads one. The reference is
    // spelled out per byte, so it does not depend on the host's own endianness, and covers every width each type
    // holds at every offset the array allows, over bytes from 0x00 to 0xff with the high bit both clear and set.
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
            }
    };
    sweep(uint32_t{0});
    sweep(uint64_t{0});
}

TEST_CASE("read_partial_as at full width matches read_as for every type it supports")
{
    // at sizeof(T) the two are the same read, for both byte orders and every width read_as handles
    const unsigned char bytes[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    for (auto e : {Endian::Little, Endian::Big})
    {
        CHECK(read_partial_as<uint8_t>(bytes, 1, e) == read_as<uint8_t>(bytes, e));
        CHECK(read_partial_as<uint16_t>(bytes, 2, e) == read_as<uint16_t>(bytes, e));
        CHECK(read_partial_as<uint32_t>(bytes, 4, e) == read_as<uint32_t>(bytes, e));
        CHECK(read_partial_as<uint64_t>(bytes, 8, e) == read_as<uint64_t>(bytes, e));
    }
}
