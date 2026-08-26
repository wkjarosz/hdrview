//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// Loaders size their buffers from the header, so a file declaring billions of pixels costs the memory or the
// decode time whether or not the pixels are there. The headers below are the ones a fuzzer found; each is a
// few dozen bytes and declares a canvas nothing could display.

#include <doctest/doctest.h>

#include "imageio/dds.h"
#include "imageio/image_loader.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::string as_stream_contents(const std::vector<uint8_t> &bytes)
{
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void append_be32(std::vector<uint8_t> &v, uint32_t x)
{
    v.insert(v.end(), {uint8_t(x >> 24), uint8_t(x >> 16), uint8_t(x >> 8), uint8_t(x)});
}

void append_le32(std::vector<uint8_t> &v, uint32_t x)
{
    v.insert(v.end(), {uint8_t(x), uint8_t(x >> 8), uint8_t(x >> 16), uint8_t(x >> 24)});
}

//! A QOI header declaring \p w by \p h. Big-endian dimensions at bytes 4..12, then channels and colorspace.
std::string qoi_header(uint32_t w, uint32_t h)
{
    std::vector<uint8_t> v{'q', 'o', 'i', 'f'};
    append_be32(v, w);
    append_be32(v, h);
    v.push_back(4); // channels
    v.push_back(0); // colorspace
    v.resize(v.size() + 32, 0);
    return as_stream_contents(v);
}

//! A DDS header declaring \p w by \p h, complete enough that the parse reaches the dimensions.
/*!
    Offsets follow the DDS_HEADER layout: size, flags, height, width at 4..20, and the 32-byte
    DDS_PIXELFORMAT at 76. An empty pixel format makes the parse stop before it ever reads the size.
*/
std::string dds_header(uint32_t w, uint32_t h)
{
    std::vector<uint8_t> v(128, 0);
    auto                 put_le32 = [&v](size_t off, uint32_t x)
    {
        v[off]     = uint8_t(x);
        v[off + 1] = uint8_t(x >> 8);
        v[off + 2] = uint8_t(x >> 16);
        v[off + 3] = uint8_t(x >> 24);
    };

    v[0] = 'D';
    v[1] = 'D';
    v[2] = 'S';
    v[3] = ' ';
    put_le32(4, 124);      // dwSize
    put_le32(8, 0x1007);   // caps | height | width | pixelformat
    put_le32(12, h);       // dwHeight
    put_le32(16, w);       // dwWidth
    put_le32(76, 32);      // ddspf.dwSize
    put_le32(80, 0x41);    // ddspf.dwFlags: RGB | alpha
    put_le32(88, 32);      // ddspf.dwRGBBitCount
    put_le32(92, 0x00ff0000);
    put_le32(96, 0x0000ff00);
    put_le32(100, 0x000000ff);
    put_le32(104, 0xff000000);
    put_le32(108, 0x1000); // dwCaps: texture

    v.resize(256, 0); //< a little pixel data, so nothing stops short of the header
    return as_stream_contents(v);
}

//! A GIF87a header declaring \p w by \p h. Little-endian 16-bit dimensions at bytes 6..10.
std::string gif_header(uint16_t w, uint16_t h)
{
    std::vector<uint8_t> v{'G', 'I', 'F', '8', '7', 'a'};
    v.insert(v.end(), {uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8)});
    v.insert(v.end(), {0xF7, 0x00, 0x00});
    v.resize(v.size() + 768, 0); // global color table
    return as_stream_contents(v);
}

//! Why \p loader refused \p bytes, or an empty string if it didn't refuse.
/*!
    The message matters, not just the throw: these headers are truncated, so a loader that skipped the
    dimension check would still fail -- just later, slower, and for a different reason.
*/
template <typename Loader>
std::string rejection_reason(Loader &&loader, const std::string &bytes, const char *filename)
{
    std::istringstream is(bytes, std::ios::binary);
    try
    {
        loader(is, filename, ImageLoadOptions{});
        return {};
    }
    catch (const std::exception &e)
    {
        return e.what();
    }
}

//! Whether \p reason is the dimension check's complaint rather than some later decode failure.
bool blames_dimensions(const std::string &reason)
{
    return reason.find("implausibly large") != std::string::npos;
}

} // namespace

TEST_CASE("check_image_dimensions accepts real sizes and rejects degenerate ones")
{
    CHECK_NOTHROW(check_image_dimensions(1, 1, "test"));
    CHECK_NOTHROW(check_image_dimensions(1920, 1080, "test"));
    CHECK_NOTHROW(check_image_dimensions(16384, 16384, "test")); //< a large but ordinary EXR

    CHECK_THROWS_AS(check_image_dimensions(0, 16, "test"), std::invalid_argument);
    CHECK_THROWS_AS(check_image_dimensions(16, 0, "test"), std::invalid_argument);
    CHECK_THROWS_AS(check_image_dimensions(-1, 16, "test"), std::invalid_argument);

    // One axis alone is enough, even when the pixel count would fit.
    CHECK_THROWS_AS(check_image_dimensions(4, 16776963, "test"), std::invalid_argument);
    // As is the total, even when each axis would fit on its own.
    CHECK_THROWS_AS(check_image_dimensions(19789, 19789, "test"), std::invalid_argument);
    CHECK_THROWS_AS(check_image_dimensions(4294967295ll, 4294967295ll, "test"), std::invalid_argument);
}

TEST_CASE("Loaders reject implausible dimensions before decoding")
{
    // The four headers a fuzzer found. Before the check these took 32-38 seconds or exhausted memory; the
    // point of asserting the throw rather than timing the call is that it stays deterministic on any runner.
    SUBCASE("QOI declaring 4 x 16776963")
    {
        auto reason = rejection_reason(load_qoi_image, qoi_header(4, 16776963), "bomb.qoi");
        CAPTURE(reason);
        CHECK(blames_dimensions(reason));
    }
    SUBCASE("DDS declaring 4294967295 square")
    {
        auto reason = rejection_reason(load_dds_image, dds_header(4294967295u, 4294967295u), "bomb.dds");
        CAPTURE(reason);
        CHECK(blames_dimensions(reason));
    }
    SUBCASE("GIF declaring 19789 x 19789")
    {
        auto reason = rejection_reason(load_stb_image, gif_header(19789, 19789), "bomb.gif");
        CAPTURE(reason);
        CHECK(blames_dimensions(reason));
    }
    SUBCASE("GIF declaring 7850 x 57457")
    {
        auto reason = rejection_reason(load_stb_image, gif_header(7850, 57457), "bomb.gif");
        CAPTURE(reason);
        CHECK(blames_dimensions(reason));
    }
}

TEST_CASE("A header declaring an ordinary size is not rejected for its dimensions")
{
    // Guards against the limits being tightened into the range of real images: these headers are truncated,
    // so they still fail, but they must not fail with a dimension complaint.
    std::istringstream is(qoi_header(64, 64), std::ios::binary);
    try
    {
        load_qoi_image(is, "small.qoi", ImageLoadOptions{});
    }
    catch (const std::exception &e)
    {
        CHECK(std::string(e.what()).find("implausibly large") == std::string::npos);
    }
}
