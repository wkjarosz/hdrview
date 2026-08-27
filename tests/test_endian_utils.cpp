//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"

#include <initializer_list>

TEST_CASE("read_partial_as assembles a value narrower than its destination")
{
    // A DDS bitmasked pixel is 1 to 4 bytes wide, so the stored value is narrower than the uint32_t it
    // is assembled into. read_as<uint32_t> reads four bytes whatever the width, which is the distinction
    // between the two.
    const unsigned char bytes[] = {0x78, 0x56, 0x34, 0x12};

    CHECK(read_partial_as<uint32_t>(bytes, 1, Endian::Little) == 0x78u);
    CHECK(read_partial_as<uint32_t>(bytes, 2, Endian::Little) == 0x5678u);
    CHECK(read_partial_as<uint32_t>(bytes, 3, Endian::Little) == 0x345678u);
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Little) == 0x12345678u);

    CHECK(read_partial_as<uint32_t>(bytes, 1, Endian::Big) == 0x78u);
    CHECK(read_partial_as<uint32_t>(bytes, 2, Endian::Big) == 0x7856u);
    CHECK(read_partial_as<uint32_t>(bytes, 3, Endian::Big) == 0x785634u);
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Big) == 0x78563412u);

    // At the full width the two are the same read, so they have to agree.
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Little) == read_as<uint32_t>(bytes, Endian::Little));
    CHECK(read_partial_as<uint32_t>(bytes, 4, Endian::Big) == read_as<uint32_t>(bytes, Endian::Big));

    // Reading nothing yields nothing rather than touching the pointer.
    CHECK(read_partial_as<uint32_t>(bytes, 0, Endian::Little) == 0u);

    // Assembling into a wider destination zero-extends rather than sign-extends.
    const unsigned char high[] = {0xff, 0xff};
    CHECK(read_partial_as<uint64_t>(high, 2, Endian::Little) == 0xffffu);
}

TEST_CASE("read_partial_as agrees with a direct assembly at every width and byte order")
{
    // The point of assembling arithmetically is that the result does not depend on the host's own
    // endianness, so the reference here is spelled out per byte rather than by reading through the
    // same helper.
    const unsigned char bytes[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    for (size_t width = 0; width <= sizeof(bytes); ++width)
    {
        CAPTURE(width);

        uint64_t little = 0, big = 0;
        for (size_t i = 0; i < width; ++i)
        {
            little |= (uint64_t)bytes[i] << (8 * i);
            big = (big << 8) | bytes[i];
        }

        CHECK(read_partial_as<uint64_t>(bytes, width, Endian::Little) == little);
        CHECK(read_partial_as<uint64_t>(bytes, width, Endian::Big) == big);

        // Reading from every offset the array allows, so no width is only ever tried at offset zero.
        for (size_t offset = 0; offset + width <= sizeof(bytes); ++offset)
        {
            CAPTURE(offset);
            uint64_t l = 0, b = 0;
            for (size_t i = 0; i < width; ++i)
            {
                l |= (uint64_t)bytes[offset + i] << (8 * i);
                b = (b << 8) | bytes[offset + i];
            }
            CHECK(read_partial_as<uint64_t>(bytes + offset, width, Endian::Little) == l);
            CHECK(read_partial_as<uint64_t>(bytes + offset, width, Endian::Big) == b);
        }
    }

    // A single byte reads the same either way, whatever its value -- the one width with no byte order.
    for (int v = 0; v <= 0xff; ++v)
    {
        const unsigned char one = (unsigned char)v;
        CHECK(read_partial_as<uint32_t>(&one, 1, Endian::Little) == (uint32_t)v);
        CHECK(read_partial_as<uint32_t>(&one, 1, Endian::Big) == (uint32_t)v);
    }
}

TEST_CASE("read_partial_as at full width matches read_as for every type it supports")
{
    // At sizeof(T) the two are the same read, so they have to agree -- for both byte orders and for
    // each width read_as handles.
    const unsigned char bytes[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    for (auto e : {Endian::Little, Endian::Big})
    {
        CHECK(read_partial_as<uint8_t>(bytes, 1, e) == read_as<uint8_t>(bytes, e));
        CHECK(read_partial_as<uint16_t>(bytes, 2, e) == read_as<uint16_t>(bytes, e));
        CHECK(read_partial_as<uint32_t>(bytes, 4, e) == read_as<uint32_t>(bytes, e));
        CHECK(read_partial_as<uint64_t>(bytes, 8, e) == read_as<uint64_t>(bytes, e));
    }
}

TEST_CASE("swap_bytes is its own inverse for every supported width")
{
    for (uint16_t v : {(uint16_t)0, (uint16_t)1, (uint16_t)0x1234, (uint16_t)0xffff})
        CHECK(swap_bytes(swap_bytes(v)) == v);
    for (uint32_t v : {0u, 1u, 0x12345678u, 0xffffffffu}) CHECK(swap_bytes(swap_bytes(v)) == v);
    for (uint64_t v : {0ull, 1ull, 0x0123456789abcdefull, ~0ull}) CHECK(swap_bytes(swap_bytes(v)) == v);

    // A one-byte value has nothing to swap.
    for (int v = 0; v <= 0xff; ++v) CHECK(swap_bytes((uint8_t)v) == (uint8_t)v);
}
