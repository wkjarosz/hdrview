//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// exif.cpp reads a tag's decoded value back out of JSON to build the human-readable string beside it --
// value["value"].get<int>() and friends, at forty-odd sites. Each assumes the tag holds the scalar its
// number implies. A file is free to disagree.

#include <doctest/doctest.h>

#include "imageio/exif.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

//! Builds a little-endian TIFF header with one IFD0, which is what an EXIF block is.
struct TiffBuilder
{
    struct Entry
    {
        uint16_t             tag, format;
        uint32_t             components;
        std::vector<uint8_t> inline_or_external; //!< the 4 inline bytes, or the payload to place after the IFD
        bool                 external = false;
    };

    std::vector<Entry> entries;

    void add_inline(uint16_t tag, uint16_t format, uint32_t components, std::vector<uint8_t> four_bytes)
    {
        four_bytes.resize(4, 0);
        entries.push_back({tag, format, components, std::move(four_bytes), false});
    }
    void add_external(uint16_t tag, uint16_t format, uint32_t components, std::vector<uint8_t> payload)
    {
        entries.push_back({tag, format, components, std::move(payload), true});
    }

    std::vector<uint8_t> build() const
    {
        std::vector<uint8_t> out;
        auto                 u16 = [&](uint16_t v) { out.insert(out.end(), {uint8_t(v & 0xff), uint8_t(v >> 8)}); };
        auto                 u32 = [&](uint32_t v)
        {
            for (int i = 0; i < 4; ++i) out.push_back(uint8_t((v >> (8 * i)) & 0xff));
        };

        out.insert(out.end(), {'I', 'I'});
        u16(42);
        u32(8); // IFD0 starts right after the header

        // count + 12 bytes per entry + the next-IFD offset; external payloads follow
        const uint32_t ifd_end    = 8 + 2 + 12 * (uint32_t)entries.size() + 4;
        uint32_t       payload_at = ifd_end;

        u16((uint16_t)entries.size());
        for (const auto &e : entries)
        {
            u16(e.tag);
            u16(e.format);
            u32(e.components);
            if (e.external)
            {
                u32(payload_at);
                payload_at += (uint32_t)e.inline_or_external.size();
            }
            else
                out.insert(out.end(), e.inline_or_external.begin(), e.inline_or_external.end());
        }
        u32(0); // no IFD1

        for (const auto &e : entries)
            if (e.external)
                out.insert(out.end(), e.inline_or_external.begin(), e.inline_or_external.end());

        return out;
    }
};

constexpr uint16_t k_tag_make = 0x010f, k_tag_compression = 0x0103, k_tag_orientation = 0x0112;
constexpr uint16_t k_fmt_ascii = 2, k_fmt_short = 3;

json parse(const std::vector<uint8_t> &blob) { return exif_to_json(blob.data(), blob.size()); }

//! Whether any IFD in the result holds a tag with this title.
bool has_tag_named(const json &j, const std::string &title)
{
    for (auto &[ifd, tags] : j.items())
        if (tags.is_object() && tags.contains(title))
            return true;
    return false;
}

} // namespace

TEST_CASE("An EXIF block with an ordinary tag decodes it")
{
    // Baseline, so a change that stops EXIF parsing entirely cannot pass as one that hardens it.
    TiffBuilder t;
    t.add_external(k_tag_make, k_fmt_ascii, 8, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0});
    const json j = parse(t.build());

    REQUIRE(j.is_object());
    REQUIRE(has_tag_named(j, "Manufacturer"));
}

TEST_CASE("One tag the file describes oddly does not discard the rest of the EXIF block")
{
    // Compression is read back as a single int to name it ("Uncompressed", "JPEG", ...). Declaring two
    // components makes the decoded value an array, and .get<int>() on an array throws -- out of
    // entry_to_json(), out of the IFD loop, out of to_json(). Every loader wraps that call, so the throw
    // costs not just this tag but every tag in the file.
    SUBCASE("a scalar tag carrying an array")
    {
        TiffBuilder t;
        t.add_external(k_tag_make, k_fmt_ascii, 8, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0});
        t.add_inline(k_tag_compression, k_fmt_short, 2, {1, 0, 6, 0});

        json j;
        CHECK_NOTHROW(j = parse(t.build()));
        CHECK(has_tag_named(j, "Manufacturer"));
    }

    SUBCASE("a scalar tag carrying text")
    {
        TiffBuilder t;
        t.add_external(k_tag_make, k_fmt_ascii, 8, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0});
        t.add_external(k_tag_compression, k_fmt_ascii, 4, {'n', 'o', 'n', 'e'});

        json j;
        CHECK_NOTHROW(j = parse(t.build()));
        CHECK(has_tag_named(j, "Manufacturer"));
    }

    SUBCASE("a tag with no components at all")
    {
        TiffBuilder t;
        t.add_external(k_tag_make, k_fmt_ascii, 8, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0});
        t.add_inline(k_tag_orientation, k_fmt_short, 0, {0, 0, 0, 0});

        json j;
        CHECK_NOTHROW(j = parse(t.build()));
        CHECK(has_tag_named(j, "Manufacturer"));
    }

    SUBCASE("a tag whose format is not one EXIF defines")
    {
        TiffBuilder t;
        t.add_external(k_tag_make, k_fmt_ascii, 8, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0});
        t.add_inline(k_tag_compression, 999, 1, {1, 0, 0, 0});

        json j;
        CHECK_NOTHROW(j = parse(t.build()));
        CHECK(has_tag_named(j, "Manufacturer"));
    }
}

TEST_CASE("A maker-note tag's range is bounded without the arithmetic wrapping")
{
    // Ranges that genuinely fit, including the empty and exactly-full ones.
    CHECK(maker_note_range_within(0, 0, 0));
    CHECK(maker_note_range_within(0, 100, 100));
    CHECK(maker_note_range_within(90, 10, 100));
    CHECK(maker_note_range_within(100, 0, 100));

    // Ranges that do not.
    CHECK_FALSE(maker_note_range_within(91, 10, 100));
    CHECK_FALSE(maker_note_range_within(101, 0, 100));
    CHECK_FALSE(maker_note_range_within(0, 101, 100));

    // And the ones the file gets to choose: an offset high enough that adding the size wraps back into
    // range. Checked by adding, these read as "fits" -- which is what happens where size_t is 32 bits
    // wide, as it is in the wasm build.
    CHECK_FALSE(maker_note_range_within(0xFFFFFFF8u, 32u, 100u));
    CHECK_FALSE(maker_note_range_within(0xFFFFFFFFu, 1u, 100u));
    CHECK_FALSE(maker_note_range_within(0x80000000u, 0x80000000u, 100u));
}
