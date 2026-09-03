/** \file test_support.h
    \author Wojciech Jarosz

    Fixtures shared across the test suites: byte writers, image builders, and container formats.
*/

#pragma once

#include "endian-utils.h"
#include "image.h"
#include "imageio/image_loader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace hdrview_test
{

/// Appends `value` in `endian` byte order to a container of `char` or `uint8_t`.
template <typename T, typename Bytes>
void put(Bytes &out, T value, Endian endian = Endian::Little)
{
    unsigned char buf[sizeof(T)];
    write_as(buf, value, endian);
    for (unsigned char b : buf) out.push_back(typename Bytes::value_type(b));
}

/// Overwrites the `sizeof(T)` bytes at `offset` with `value` in `endian` byte order.
template <typename T, typename Bytes>
void patch(Bytes &out, size_t offset, T value, Endian endian = Endian::Little)
{
    unsigned char buf[sizeof(T)];
    write_as(buf, value, endian);
    for (size_t i = 0; i < sizeof(T); ++i) out[offset + i] = typename Bytes::value_type(buf[i]);
}

/// An image of `size` with `num_channels` channels, every sample set to `fill(channel, x, y)`.
template <typename F>
ImagePtr test_image(int2 size, int num_channels, F &&fill)
{
    auto img = std::make_shared<Image>(size, num_channels);
    for (int c = 0; c < num_channels; ++c)
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) img->channels[size_t(c)](x, y) = fill(c, x, y);
    img->finalize();
    return img;
}

/// Every sample of every channel, as one comparable value.
inline std::vector<float> samples(const ImagePtr &img)
{
    std::vector<float> out;
    for (const auto &ch : img->channels)
        for (int i = 0; i < ch.num_elements(); ++i) out.push_back(ch(i));
    return out;
}

/// Decodes `bytes` as if read from a file named `name`; null unless the load yielded exactly one image.
inline ImagePtr load_bytes(const std::string &bytes, const char *name, const ImageLoadOptions &opts = {})
{
    std::istringstream in(bytes, std::ios::binary);
    auto               images = load_image(in, name, opts);
    return images.size() == 1 ? images.front() : nullptr;
}

/// Decodes `bytes` through one format's loader; null unless the load yielded exactly one image.
template <typename Loader>
ImagePtr load_bytes(Loader &&loader, const std::string &bytes, const char *name, const ImageLoadOptions &opts = {})
{
    std::istringstream in(bytes, std::ios::binary);
    auto               images = loader(in, name, opts);
    return images.size() == 1 ? images.front() : nullptr;
}

/// One IFD entry: a tag, its TIFF type, a count, and either an inline value or bytes stored out of line.
struct TiffEntry
{
    uint16_t             tag;
    uint16_t             type;      ///< 3 = SHORT, 4 = LONG, 7 = UNDEFINED
    uint32_t             count = 1; ///< number of values, not bytes
    uint32_t             value = 0; ///< the value itself when `blob` is empty, otherwise its file offset
    std::vector<uint8_t> blob;      ///< values too large for the four-byte field
};

/// A single-directory, single-strip TIFF: header, IFD, out-of-line values, then `strip`.
/**
    `entries` are sorted by tag here, and must not include StripOffsets (273) or StripByteCounts (279),
    which come from where the strip lands.
*/
inline std::string tiff_bytes(Endian endian, std::vector<TiffEntry> entries, const std::vector<uint8_t> &strip)
{
    entries.push_back({273, 4, 1, 0, {}});
    entries.push_back({279, 4, 1, uint32_t(strip.size()), {}});
    std::sort(entries.begin(), entries.end(), [](const TiffEntry &a, const TiffEntry &b) { return a.tag < b.tag; });

    // header (8) + entry count (2) + twelve bytes each + next-IFD pointer (4), then the blobs, then the strip
    uint32_t offset = 8 + 2 + uint32_t(entries.size()) * 12 + 4;
    for (auto &e : entries)
        if (!e.blob.empty())
        {
            e.value = offset;
            offset += uint32_t(e.blob.size());
        }
    for (auto &e : entries)
        if (e.tag == 273)
            e.value = offset;

    std::string out;
    out += endian == Endian::Little ? "II" : "MM";
    put<uint16_t>(out, 42, endian);
    put<uint32_t>(out, 8, endian);

    put<uint16_t>(out, uint16_t(entries.size()), endian);
    for (const auto &e : entries)
    {
        put(out, e.tag, endian);
        put(out, e.type, endian);
        put(out, e.count, endian);
        // a SHORT that fits in the value field sits in its first half, in either byte order
        if (e.type == 3 && e.count == 1)
        {
            put<uint16_t>(out, uint16_t(e.value), endian);
            put<uint16_t>(out, 0, endian);
        }
        else
            put(out, e.value, endian);
    }
    put<uint32_t>(out, 0, endian); // no next IFD

    for (const auto &e : entries) out.append(reinterpret_cast<const char *>(e.blob.data()), e.blob.size());
    out.append(reinterpret_cast<const char *>(strip.data()), strip.size());
    return out;
}

} // namespace hdrview_test
