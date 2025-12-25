//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "raw.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "exif.h"
#include "image.h"
#include "timer.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <libexif/exif-tag.h>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>

#include <smallthreadpool.h>

using namespace std;

#ifndef HDRVIEW_ENABLE_LIBRAW

bool is_raw_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_raw_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("RAW support not enabled in this build.");
}

#else

#include <libraw/libraw.h>
#include <libraw/libraw_version.h>

#include <libexif/exif-data.h>

#if LIBRAW_VERSION < LIBRAW_MAKE_VERSION(0, 20, 0)
#error "HDRView requires LibRaw 0.20.0 or later"
#endif

namespace
{

// Custom LibRaw datastream that wraps std::istream
class LibRawIStream : public LibRaw_abstract_datastream
{
public:
    LibRawIStream(istream &stream) : m_stream(stream) {}

    int valid() override { return m_stream.good() ? 1 : 0; }

    int read(void *ptr, size_t size, size_t nmemb) override
    {
        m_stream.read(static_cast<char *>(ptr), size * nmemb);
        return static_cast<int>(m_stream.gcount() / size);
    }

    int seek(INT64 offset, int whence) override
    {
        ios_base::seekdir dir;
        switch (whence)
        {
        case SEEK_SET: dir = ios_base::beg; break;
        case SEEK_CUR: dir = ios_base::cur; break;
        case SEEK_END: dir = ios_base::end; break;
        default: return -1;
        }
        m_stream.clear(); // Clear any eof flags
        m_stream.seekg(offset, dir);
        return m_stream.good() ? 0 : -1;
    }

    INT64 tell() override { return m_stream.tellg(); }

    INT64 size() override
    {
        auto current_pos = m_stream.tellg();
        m_stream.seekg(0, ios_base::end);
        auto stream_size = m_stream.tellg();
        m_stream.seekg(current_pos, ios_base::beg);
        return stream_size;
    }

    int get_char() override { return m_stream.get(); }

    char *gets(char *str, int sz) override
    {
        m_stream.getline(str, sz);
        return m_stream.good() ? str : nullptr;
    }

    int scanf_one(const char * /*fmt*/, void * /*val*/) override { return -1; }

    int eof() override { return m_stream.eof() ? 1 : 0; }

    int jpeg_src(void * /*jpegdata*/) override { return -1; }

    const char *fname() override { return nullptr; }

private:
    istream &m_stream;
};

// Context for LibRaw EXIF callback
struct ExifContext
{
    ExifContext()
    {
        content         = exif_content_new();
        data            = exif_data_new();
        content->parent = data;
    }

    ~ExifContext()
    {
        if (content)
            exif_content_free(content);
        if (data)
            exif_data_unref(data);
    }

    json         metadata;
    ExifContent *content = nullptr;
    ExifData    *data    = nullptr;
};

// LibRaw EXIF callback handler
void exif_handler(void *context, int tag, int type, int len, unsigned int ord, void *ifp, INT64 base)
{
    ExifContext &exif = *(ExifContext *)context;

    // LibRaw encodes IFD information in bits 16-23 of the tag parameter:
    // 0x00 (0) = EXIF sub-IFD tags (from parse_exif)
    // 0x20 (2) = Kodak maker notes (from parse_kodak_ifd)
    // 0x40 (4) = Interoperability IFD (from parse_exif_interop)
    // 0x50 (5) = GPS IFD (from parse_gps_libraw)
    int libraw_ifd_idx = (tag >> 16) & 0xFF;

    // Get actual tag value (lower 16 bits)
    int actual_tag = tag & 0xFFFF;

    // Map LibRaw IFD indices to libexif ExifIfd enum and IFD name
    const char *ifd_name;
    ExifIfd     ifd_enum;

    switch (libraw_ifd_idx)
    {
    default: // Unknown, treat as EXIF_IFD_0
        ifd_name = "TIFF";
        ifd_enum = EXIF_IFD_0;
        break;
    case 0x00: // EXIF sub-IFD
        ifd_name = "EXIF";
        ifd_enum = EXIF_IFD_EXIF;
        break;
    case 0x02: // Kodak maker notes - treat as EXIF_IFD_0
        ifd_name = "TIFF";
        ifd_enum = EXIF_IFD_0;
        break;
    case 0x04: // Interoperability IFD
        ifd_name = "Interoperability";
        ifd_enum = EXIF_IFD_INTEROPERABILITY;
        break;
    case 0x05: // GPS IFD
        ifd_name = "GPS";
        ifd_enum = EXIF_IFD_GPS;
        break;
    }

    ExifEntry *entry = exif_entry_new();
    if (!entry)
        return;

    const auto guard = ScopeGuard([&]() { exif_entry_unref(entry); });

    exif_data_set_byte_order(exif.data, ord == 0x4949 ? EXIF_BYTE_ORDER_INTEL : EXIF_BYTE_ORDER_MOTOROLA);
    entry->parent     = exif.content;
    entry->tag        = static_cast<ExifTag>(actual_tag);
    entry->format     = static_cast<ExifFormat>(type);
    entry->components = len;

    const int sizePerComponent = exif_format_get_size(entry->format);
    if (sizePerComponent == 0)
        return;

    entry->size = len * sizePerComponent;
    entry->data = (unsigned char *)malloc(entry->size); // Will get freed by exif_entry_unref

    // LibRaw has already positioned the stream at the correct location
    // The base parameter is for TIFF offset calculations, not for seeking
    auto *stream = (LibRaw_abstract_datastream *)ifp;
    stream->read(entry->data, sizePerComponent, len);

    try
    {
        exif.metadata[ifd_name].update(entry_to_json(entry, ord == 0x4949 ? 1 : 0, ifd_enum));
    }
    catch (const std::exception &e)
    {
        // ignore errors
        spdlog::warn("Error processing EXIF tag {}: {}", tag, e.what());
    }
}

} // namespace

bool is_raw_image(istream &is) noexcept
{
    try
    {
        unique_ptr<LibRaw> raw(new LibRaw());
        LibRawIStream      ds(is);

        auto ret = raw->open_datastream(&ds) == LIBRAW_SUCCESS;

        is.clear();
        is.seekg(0);
        return ret;
    }
    catch (...)
    {
        is.clear();
        is.seekg(0);
        return false;
    }
}

vector<ImagePtr> load_raw_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "RAW"};
    Timer     timer;

    // Create and configure LibRaw processor (use heap allocation for thread-safe version)
    unique_ptr<LibRaw> processor(new LibRaw());

    // Set up EXIF callback handler to extract metadata
    ExifContext exif_ctx; // Constructor creates and initializes libexif structures
    processor->set_exifparser_handler(exif_handler, &exif_ctx);

    // Set processing parameters
    processor->imgdata.params.use_camera_matrix = 1; // Use camera color matrix
    processor->imgdata.params.use_camera_wb     = 1; // Use camera white balance
    processor->imgdata.params.use_auto_wb       = 0;
    processor->imgdata.params.no_auto_bright    = 1;   // Prevent exposure scaling
    processor->imgdata.params.gamm[0]           = 1.0; // Keep linear output
    processor->imgdata.params.gamm[1]           = 1.0;
    processor->imgdata.params.highlight         = 0;  // Disable highlight recovery / compression
    processor->imgdata.params.output_bps        = 16; // Full precision
    processor->imgdata.params.user_qual         = 3;  // demosaic algorithm/quality:
                                                      // 0 - linear
                                                      // 1 - VNG
                                                      // 2 - PPG
                                                      // 3 - AHD
                                                      // 4 - DCB
                                                      // 11 - DHT
                                                      // 12 - AAHD
    processor->imgdata.params.output_color = 1;       // Output color space (camera → XYZ → output)
                                                      // 0 Raw color (unique to each camera)
                                                      // 1 sRGB D65 (default)
                                                      // 2 Adobe RGB (1998) D65
                                                      // 3 Wide Gamut RGB D65
                                                      // 4 Kodak ProPhoto RGB D65
                                                      // 5 XYZ
                                                      // 6 ACES
                                                      // 7 DCI-P3
                                                      // 8 Rec2020

    // Create custom datastream from istream
    LibRawIStream libraw_stream(is);

    // Open the RAW file using datastream (avoids loading entire file into memory)
    int ret = processor->open_datastream(&libraw_stream);
    if (ret != LIBRAW_SUCCESS)
        throw runtime_error(fmt::format("Failed to open RAW file: {}", libraw_strerror(ret)));

    // Unpack the RAW data
    ret = processor->unpack();
    if (ret != LIBRAW_SUCCESS)
        throw runtime_error(fmt::format("Failed to unpack RAW data: {}", libraw_strerror(ret)));

    // Process the image (demosaic, white balance, etc.)
    ret = processor->dcraw_process();
    if (ret != LIBRAW_SUCCESS)
        throw runtime_error(fmt::format("Failed to process RAW image: {}", libraw_strerror(ret)));

    // Access image data directly from imgdata
    auto &idata = processor->imgdata;
    auto &sizes = idata.sizes;

    // Use iwidth/iheight for the processed image dimensions
    int2 size{sizes.iwidth, sizes.iheight};
    int  flip         = sizes.flip;
    int  num_channels = 3; // Force RGB

    // Calculate oriented size based on flip
    int2 oriented_size = (flip & 4) ? int2{size.y, size.x} : size;

    // Verify we have image data
    if (!idata.image)
        throw runtime_error("No image data available after processing");

    // Create HDRView image with oriented dimensions
    auto image                = make_shared<Image>(oriented_size, num_channels);
    image->filename           = filename;
    image->metadata["loader"] = "LibRaw";

    // Add collected EXIF metadata if any
    if (!exif_ctx.metadata.empty())
    {
        image->metadata["exif"] = exif_ctx.metadata;
        // spdlog::info("Extracted {} EXIF IFD sections from RAW file", exif_ctx.metadata.size());
        // spdlog::info("Exif: {}", exif_ctx.metadata.dump(2));
    }
    else
    {
        spdlog::warn("No EXIF metadata extracted from RAW file");
    }

    // Helper function to handle flip transformations
    auto flip_index = [&](int2 idx) -> int2
    {
        if (flip & 4)
            swap(idx.x, idx.y);

        if (flip & 1)
            idx.y = oriented_size.y - 1 - idx.y;

        if (flip & 2)
            idx.x = oriented_size.x - 1 - idx.x;

        return idx;
    };

    // Access image data as array of ushort[4]
    const auto *img_data = idata.image;

    // Allocate float buffer for all pixels
    vector<float> float_pixels((size_t)oriented_size.x * oriented_size.y * num_channels);

    // Convert from 16-bit to float [0,1] with flip handling
    constexpr float scale = 1.0f / 65535.0f;

    stp::parallel_for(stp::blocked_range<int>(0, size.y, 32),
                      [&](int begin, int end, int unit_index, int thread_index)
                      {
                          for (int y = begin; y < end; ++y)
                          {
                              for (int x = 0; x < size.x; ++x)
                              {
                                  size_t src_i   = (size_t)y * size.x + x;
                                  int2   flipped = flip_index({x, y});
                                  size_t dst_i   = (size_t)flipped.y * oriented_size.x + flipped.x;

                                  for (int c = 0; c < num_channels; ++c)
                                      float_pixels[dst_i * num_channels + c] = img_data[src_i][c] * scale;
                              }
                          }
                      });

    // Copy data to image channels
    for (int c = 0; c < num_channels; ++c)
    {
        image->channels[c].copy_from_interleaved(float_pixels.data(), oriented_size.x, oriented_size.y, num_channels, c,
                                                 [](float v) { return v; });
    }

    // Set chromaticities - RAW files are typically in a camera-specific color space
    // LibRaw outputs to sRGB by default, but with linear gamma as we requested
    image->chromaticities            = Chromaticities(); // Default is sRGB/Rec709
    image->metadata["color profile"] = "Linear sRGB";

    spdlog::info("Loaded RAW image ({}x{}, {} channels) in {:.2f}ms", oriented_size.x, oriented_size.y, num_channels,
                 timer.elapsed());

    return {image};
}

#endif
