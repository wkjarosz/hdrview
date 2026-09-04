//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// exif.cpp reads a tag's decoded value back out of JSON to build the human-readable string beside it
// (value["value"].get<int>() and friends, at forty-odd sites), assuming the tag holds the scalar its number
// implies. A file is free to disagree.

#include <doctest/doctest.h>

#include "imageio/exif.h"

#include "test_support.h"

using namespace hdrview_test;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{

constexpr uint16_t k_tag_make = 0x010f, k_tag_compression = 0x0103, k_tag_orientation = 0x0112, k_tag_exif_ifd = 0x8769,
                   k_tag_maker_note = 0x927c;
constexpr uint16_t k_fmt_ascii = 2, k_fmt_short = 3, k_fmt_long = 4, k_fmt_rational = 5, k_fmt_undefined = 7;

/// Builds a little-endian TIFF header with one IFD0, which is what an EXIF block is.
struct TiffBuilder
{
    struct Entry
    {
        uint16_t             tag, format;
        uint32_t             components;
        std::vector<uint8_t> inline_or_external; ///< the 4 inline bytes, or the payload to place after the IFD
        bool                 external = false;
    };

    std::vector<Entry> entries;      ///< IFD0
    std::vector<Entry> exif_entries; ///< the EXIF sub-IFD, which IFD0 points to when non-empty

    void add_inline(uint16_t tag, uint16_t format, uint32_t components, std::vector<uint8_t> four_bytes)
    {
        four_bytes.resize(4, 0);
        entries.push_back({tag, format, components, std::move(four_bytes), false});
    }
    void add_external(uint16_t tag, uint16_t format, uint32_t components, std::vector<uint8_t> payload)
    {
        entries.push_back({tag, format, components, std::move(payload), true});
    }
    /// A maker note lives in the EXIF IFD, the only IFD libexif accepts the tag in.
    void add_maker_note(std::vector<uint8_t> note)
    {
        exif_entries.push_back({k_tag_maker_note, k_fmt_undefined, (uint32_t)note.size(), std::move(note), true});
    }

    std::vector<uint8_t> build() const
    {
        std::vector<uint8_t> out;
        auto                 u16 = [&](uint16_t v) { put(out, v); };
        auto                 u32 = [&](uint32_t v) { put(out, v); };

        out.insert(out.end(), {'I', 'I'});
        u16(42);
        u32(8); // IFD0 starts right after the header

        // each IFD is a count + 12 bytes per entry + the next-IFD offset; external payloads follow both
        const uint32_t ifd0_count = (uint32_t)entries.size() + (exif_entries.empty() ? 0u : 1u);
        const uint32_t sub_at     = 8 + 2 + 12 * ifd0_count + 4;
        const uint32_t sub_end    = exif_entries.empty() ? sub_at : sub_at + 2 + 12 * (uint32_t)exif_entries.size() + 4;

        uint32_t payload_at = sub_end;
        auto     emit       = [&](const Entry &e)
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
        };

        u16((uint16_t)ifd0_count);
        for (const auto &e : entries) emit(e);
        if (!exif_entries.empty())
        {
            u16(k_tag_exif_ifd);
            u16(k_fmt_long);
            u32(1);
            u32(sub_at);
        }
        u32(0); // no IFD1

        if (!exif_entries.empty())
        {
            u16((uint16_t)exif_entries.size());
            for (const auto &e : exif_entries) emit(e);
            u32(0);
        }

        for (const auto *list : {&entries, &exif_entries})
            for (const auto &e : *list)
                if (e.external)
                    out.insert(out.end(), e.inline_or_external.begin(), e.inline_or_external.end());

        return out;
    }
};

json parse(const std::vector<uint8_t> &blob) { return exif_to_json(blob.data(), blob.size()); }

/// Whether any IFD in the result holds a tag with this title.
bool has_tag_named(const json &j, const std::string &title)
{
    for (auto &[ifd, tags] : j.items())
        if (tags.is_object() && tags.contains(title))
            return true;
    return false;
}

/// One entry of an Apple maker note's IFD.
struct MakerNoteEntry
{
    uint16_t tag, format;
    uint32_t components;
    uint32_t inline_or_offset; ///< the value itself where it fits in four bytes, else an offset into the note
};

/// Where apple_maker_note() places \p data in a note holding \p count entries.
constexpr uint32_t maker_note_data_at(size_t count) { return 16 + 12 * (uint32_t)count + 4; }

/// Builds an Apple maker note: a 12-byte signature, a little-endian IFD, then \p data.
std::vector<uint8_t> apple_maker_note(const std::vector<MakerNoteEntry> &entries, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> n;
    auto                 u16 = [&](uint16_t v) { put(n, v); };
    auto                 u32 = [&](uint32_t v) { put(n, v); };

    const char *signature = "Apple iOS";
    n.insert(n.end(), signature, signature + 10);
    n.insert(n.end(), {0, 1});
    n.insert(n.end(), {'I', 'I'});
    u16((uint16_t)entries.size());
    for (const auto &e : entries)
    {
        u16(e.tag);
        u16(e.format);
        u32(e.components);
        u32(e.inline_or_offset);
    }
    u32(0); // no next IFD
    n.insert(n.end(), data.begin(), data.end());
    return n;
}

} // namespace

TEST_CASE("An EXIF block with an ordinary tag decodes it")
{
    // baseline, so a change that stops EXIF parsing entirely cannot pass as one that hardens it
    TiffBuilder t;
    t.add_external(k_tag_make, k_fmt_ascii, 8, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0});
    const json j = parse(t.build());

    REQUIRE(j.is_object());
    REQUIRE(has_tag_named(j, "Manufacturer"));
}

TEST_CASE("One tag the file describes oddly does not discard the rest of the EXIF block")
{
    // Compression is read back as a single int to name it ("Uncompressed", "JPEG", ...). Two components make
    // the decoded value an array, and .get<int>() on an array throws out through to_json(), losing every tag.
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

TEST_CASE("A maker-note entry is followed only where its data lies inside the note")
{
    // the note holds one rational, 84/2, in the data area right after its two entries
    constexpr uint32_t k_data_at  = maker_note_data_at(2);
    constexpr uint16_t k_gain_tag = 0x30, k_headroom_tag = 0x21;

    // offsets high enough that offset + size wraps back into range, which a check written as an addition
    // reads as "fits" wherever size_t is 32 bits wide, as in the wasm build; then one far past the note and
    // one that overruns it by a single byte
    for (uint32_t bad_offset : {0xFFFFFFF8u, 0xFFFFFFFFu, 0x80000000u, 1000u, k_data_at + 1u})
    {
        CAPTURE(bad_offset);
        TiffBuilder t;
        t.add_external(k_tag_make, k_fmt_ascii, 8, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0});
        t.add_maker_note(apple_maker_note(
            {{k_gain_tag, k_fmt_rational, 1, bad_offset}, {k_headroom_tag, k_fmt_rational, 1, k_data_at}},
            {84, 0, 0, 0, 2, 0, 0, 0}));

        const auto blob = t.build();
        Exif       exif{blob.data(), blob.size()};
        REQUIRE(exif.valid());

        CHECK_FALSE(exif.apple_makernote_value(k_gain_tag).has_value());

        // the entry the note does describe well is still read, so the walk neither stops at the bad one nor
        // rejects every offset
        auto headroom = exif.apple_makernote_value(k_headroom_tag);
        REQUIRE(headroom.has_value());
        CHECK(*headroom == doctest::Approx(42.0));
    }
}

#ifdef HDRVIEW_TEST_LIBEXIF_DIR

namespace
{

/// The TIFF block inside a JPEG's APP1 "Exif\0\0" segment.
std::vector<uint8_t> exif_segment(const std::string &jpeg)
{
    for (size_t at = 2; at + 4 <= jpeg.size() && (uint8_t)jpeg[at] == 0xFF;)
    {
        const uint8_t  marker = (uint8_t)jpeg[at + 1];
        const uint16_t length = uint16_t(((uint8_t)jpeg[at + 2] << 8) | (uint8_t)jpeg[at + 3]);
        if (marker == 0xDA || length < 2)
            break;
        if (marker == 0xE1 && jpeg.compare(at + 4, 6, "Exif\0\0", 6) == 0)
            return {jpeg.begin() + at + 10, jpeg.begin() + at + 2 + length};
        at += 2 + length;
    }
    return {};
}

/// The ASCII entries libexif reports for a file, keyed by its own name for each tag.
/**
    `parsed` is the dump libexif produces from the same bytes, in lines of "Entry 3: Model (ASCII)" followed
    by "Value: Canon PowerShot S70".
*/
std::map<std::string, std::string> ascii_entries(const std::string &parsed)
{
    std::map<std::string, std::string> entries;
    std::istringstream                 in(parsed);
    for (std::string line, tag; std::getline(in, line);)
    {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;

        const auto text = line.substr(start);
        if (text.rfind("Entry ", 0) == 0)
        {
            const auto colon = text.find(": ");
            tag = colon != std::string::npos && text.size() > 8 && text.substr(text.size() - 8) == " (ASCII)"
                      ? text.substr(colon + 2, text.size() - colon - 10)
                      : "";
        }
        else if (!tag.empty() && text.rfind("Value: ", 0) == 0)
        {
            if (const auto v = text.substr(7); v.size() > 3)
                entries.emplace(tag, v);
            tag.clear();
        }
    }
    return entries;
}

} // namespace

// libexif's own regression files, one per maker-note layout its parsers know. Every vendor lays a note out
// differently and several misdescribe their offsets, which is what no blob written here can stand in for.
TEST_CASE("A real camera file's tags and maker note survive into the metadata")
{
    namespace fs = std::filesystem;

    int files = 0;
    for (const auto &entry : fs::directory_iterator(HDRVIEW_TEST_LIBEXIF_DIR))
    {
        const auto path = entry.path();
        if (path.extension() != ".jpg")
            continue;

        CAPTURE(path.filename().string());
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string jpeg{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

        const auto block = exif_segment(jpeg);
        REQUIRE_FALSE(block.empty());

        Exif exif{block.data(), block.size()};
        REQUIRE(exif.valid());

        const json j = exif.to_json();
        REQUIRE(j.contains("TIFF IFD0"));
        // each file is named for the maker-note variant it carries, so every one has to be found
        CHECK(j.contains("Maker notes"));

        std::ifstream     pin(path.string() + ".parsed", std::ios::binary);
        const std::string parsed{std::istreambuf_iterator<char>(pin), std::istreambuf_iterator<char>()};
        const auto        entries = ascii_entries(parsed);
        REQUIRE_FALSE(entries.empty());

        // the strings libexif reads out of the same bytes have to be the strings that arrive here
        const auto dump = j.dump();
        for (const auto &[tag, value] : entries)
        {
            CAPTURE(value);
            CHECK(dump.find(value) != std::string::npos);
        }

        // and against the tag each belongs to, under the title HDRView keys by rather than libexif's name.
        // Presence alone would not notice a build that lost every title and filed them under tag numbers.
        for (const auto &[name, title] :
             {std::pair{"Make", "Manufacturer"}, std::pair{"Model", "Model"}, std::pair{"DateTime", "Date and Time"}})
        {
            const auto reported = entries.find(name);
            if (reported == entries.end())
                continue;

            CAPTURE(title);
            REQUIRE(j["TIFF IFD0"].contains(title));
            CHECK(j["TIFF IFD0"][title].value("string", std::string{}) == reported->second);
        }
        ++files;
    }
    CHECK(files > 0);
}

#endif // HDRVIEW_TEST_LIBEXIF_DIR
