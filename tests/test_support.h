/** \file test_support.h
    \author Wojciech Jarosz

    Fixtures shared across the test suites: byte writers, image builders, and container formats.
*/

#pragma once

#include "endian-utils.h"
#include "image.h"
#include "imageio/image_loader.h"
#include "imageio/streams.h"
#include "test_asan.h"

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
    SpanIStream in{bytes};
    auto        images = load_image(in, name, opts);
    return images.size() == 1 ? images.front() : nullptr;
}

/// Decodes `bytes` through one format's loader; null unless the load yielded exactly one image.
template <typename Loader>
ImagePtr load_bytes(Loader &&loader, const std::string &bytes, const char *name, const ImageLoadOptions &opts = {})
{
    SpanIStream in{bytes};
    auto        images = loader(in, name, opts);
    return images.size() == 1 ? images.front() : nullptr;
}

/// One IFD entry: a tag, its TIFF type, a count, and either an inline value or the bytes of its values.
struct TiffEntry
{
    uint16_t             tag;
    uint16_t             type;      ///< 2 = ASCII, 3 = SHORT, 4 = LONG, 5 = RATIONAL, 7 = UNDEFINED
    uint32_t             count = 1; ///< number of values, not bytes
    uint32_t             value = 0; ///< the value itself when `blob` is empty
    std::vector<uint8_t> blob;      ///< the values' bytes, inline when they fit in four and out of line otherwise
};

/// A single-strip TIFF: header, IFD0, an EXIF sub-IFD, out-of-line values, then `strip`.
/**
    `entries` and `exif` are sorted by tag here, and must not include StripOffsets (273), StripByteCounts (279)
    or the EXIF IFD pointer (34665), which come from where the strip and the sub-IFD land. An empty `strip` adds
    no strip tags and an empty `exif` no sub-IFD, so with neither this is the bare directory an EXIF block is.
*/
inline std::string tiff_bytes(Endian endian, std::vector<TiffEntry> entries, const std::vector<uint8_t> &strip = {},
                              std::vector<TiffEntry> exif = {})
{
    const auto by_tag = [](const TiffEntry &a, const TiffEntry &b) { return a.tag < b.tag; };
    // an entry count, twelve bytes each, and the next-IFD pointer
    const auto ifd_size = [](size_t n) { return uint32_t(2 + 12 * n + 4); };

    if (!strip.empty())
    {
        entries.push_back({273, 4, 1, 0, {}});
        entries.push_back({279, 4, 1, uint32_t(strip.size()), {}});
    }
    const uint32_t ifd0_size = ifd_size(entries.size() + (exif.empty() ? 0 : 1));
    if (!exif.empty())
        entries.push_back({34665, 4, 1, 8 + ifd0_size, {}});
    std::sort(entries.begin(), entries.end(), by_tag);
    std::sort(exif.begin(), exif.end(), by_tag);

    // the header (8) and the directories, then the out-of-line values, then the strip
    uint32_t offset = 8 + ifd0_size + (exif.empty() ? 0 : ifd_size(exif.size()));
    for (auto *ifd : {&entries, &exif})
        for (auto &e : *ifd)
            if (e.blob.size() > 4)
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

    for (const auto *ifd : {&entries, &exif})
    {
        if (ifd->empty())
            continue;
        put<uint16_t>(out, uint16_t(ifd->size()), endian);
        for (const auto &e : *ifd)
        {
            put(out, e.tag, endian);
            put(out, e.type, endian);
            put(out, e.count, endian);
            if (!e.blob.empty() && e.blob.size() <= 4)
            {
                out.append(reinterpret_cast<const char *>(e.blob.data()), e.blob.size());
                out.append(4 - e.blob.size(), '\0');
            }
            // a SHORT that fits in the value field sits in its first half, in either byte order
            else if (e.type == 3 && e.count == 1)
            {
                put<uint16_t>(out, uint16_t(e.value), endian);
                put<uint16_t>(out, 0, endian);
            }
            else
                put(out, e.value, endian);
        }
        put<uint32_t>(out, 0, endian); // no next IFD
    }

    for (const auto *ifd : {&entries, &exif})
        for (const auto &e : *ifd)
            if (e.blob.size() > 4)
                out.append(reinterpret_cast<const char *>(e.blob.data()), e.blob.size());
    out.append(reinterpret_cast<const char *>(strip.data()), strip.size());
    return out;
}

/// A PNG chunk: its length, type and data, then the CRC over the type and data.
/**
    The CRC is computed here rather than by zlib's crc32(), since a translation unit including test_zip.h sees
    miniz's declarations of zlib's names and cannot also include zlib.h.
*/
inline std::string png_chunk(const char *type, const std::string &data)
{
    const std::string body = std::string(type, 4) + data;

    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : body)
    {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }

    std::string out;
    put<uint32_t>(out, uint32_t(data.size()), Endian::Big);
    out += body;
    put<uint32_t>(out, ~crc, Endian::Big);
    return out;
}

} // namespace hdrview_test
