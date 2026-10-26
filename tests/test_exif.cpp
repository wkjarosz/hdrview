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

constexpr uint16_t k_tag_make = 0x010f, k_tag_maker_note = 0x927c;
constexpr uint16_t k_fmt_ascii = 2, k_fmt_short = 3, k_fmt_rational = 5, k_fmt_undefined = 7;

/// An IFD0 Make entry, an ordinary tag to sit beside the one a case is about.
const TiffEntry k_make{k_tag_make, k_fmt_ascii, 8, 0, {'H', 'D', 'R', 'V', 'i', 'e', 'w', 0}};

json parse(const std::string &blob)
{ return exif_to_json(reinterpret_cast<const uint8_t *>(blob.data()), blob.size()); }

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

TEST_CASE("One tag the file describes oddly does not discard the rest of the EXIF block")
{
    // Each of these tags is read back as a single int to name its value ("Uncompressed", "RGB", ...). A file
    // describing one as anything else makes .get<int>() throw out through to_json(), losing every tag, unless that
    // one tag alone is skipped. Make is the control, whose title has to survive each time. An entry with no
    // components or a format EXIF does not define, and DNG's own tags, never get that far, so none is a case here.
    struct Shape
    {
        const char          *label;
        uint16_t             format;
        uint32_t             components;
        std::vector<uint8_t> bytes;
    };
    // the array fits in the entry and the text lies outside it, so both ways a value is stored are read
    const Shape shapes[] = {
        {"an array", k_fmt_short, 2, {1, 0, 6, 0}},
        {"text", k_fmt_ascii, 5, {'n', 'o', 'n', 'e', 0}},
    };
    // Compression, PhotometricInterpretation and PlanarConfiguration
    for (uint16_t tag : {uint16_t(259), uint16_t(262), uint16_t(284)})
        for (const auto &shape : shapes)
        {
            CAPTURE(tag);
            CAPTURE(std::string{shape.label});
            json j;
            CHECK_NOTHROW(
                j = parse(tiff_bytes(Endian::Little, {k_make, {tag, shape.format, shape.components, 0, shape.bytes}})));
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
        const auto note = apple_maker_note(
            {{k_gain_tag, k_fmt_rational, 1, bad_offset}, {k_headroom_tag, k_fmt_rational, 1, k_data_at}},
            {84, 0, 0, 0, 2, 0, 0, 0});

        // a maker note lives in the EXIF IFD, the only IFD libexif accepts the tag in
        const auto blob = tiff_bytes(Endian::Little, {k_make}, {},
                                     {{k_tag_maker_note, k_fmt_undefined, uint32_t(note.size()), 0, note}});
        Exif       exif{reinterpret_cast<const uint8_t *>(blob.data()), blob.size()};
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
        // only the \r goes: git checks these dumps out with CRLF endings on Windows, and the suffix this
        // looks for sits at the end of the line, but the trailing spaces padding a fixed-width ASCII value
        // are part of that value
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

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
        for (const auto &entry : entries)
        {
            const std::string &value = entry.second; //< named rather than bound: CAPTURE() captures in a lambda, which
                                                     //a structured binding cannot be before C++20
            CAPTURE(value);
            CHECK(dump.find(value) != std::string::npos);
        }

        // and against the tag each belongs to, under the title HDRView keys by rather than libexif's name.
        // Presence alone would not notice a build that lost every title and filed them under tag numbers.
        for (const auto &names :
             {std::pair{"Make", "Manufacturer"}, std::pair{"Model", "Model"}, std::pair{"DateTime", "Date and Time"}})
        {
            const char *name = names.first, *title = names.second;
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
