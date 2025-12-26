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
#include "icc.h"
#include "image.h"
#include "jpg.h"
#include "timer.h"
#include <sstream>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <libexif/exif-tag.h>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>

#include <smallthreadpool.h>

#ifndef HDRVIEW_ENABLE_LIBRAW

bool is_raw_image(std::std::istream &is) noexcept { return false; }

vector<ImagePtr> load_raw_image(std::std::istream &is, string_view filename, const ImageLoadOptions &opts)
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

// Custom LibRaw datastream that wraps std::std::istream
class LibRawIStream : public LibRaw_abstract_datastream
{
public:
    LibRawIStream(std::istream &stream) : m_stream(stream) {}

    int valid() override { return m_stream.good() ? 1 : 0; }

    int read(void *ptr, size_t size, size_t nmemb) override
    {
        m_stream.read(static_cast<char *>(ptr), size * nmemb);
        return static_cast<int>(m_stream.gcount() / size);
    }

    int seek(INT64 offset, int whence) override
    {
        std::ios_base::seekdir dir;
        switch (whence)
        {
        case SEEK_SET: dir = std::ios_base::beg; break;
        case SEEK_CUR: dir = std::ios_base::cur; break;
        case SEEK_END: dir = std::ios_base::end; break;
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
        m_stream.seekg(0, std::ios_base::end);
        auto stream_size = m_stream.tellg();
        m_stream.seekg(current_pos, std::ios_base::beg);
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
    std::istream &m_stream;
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

bool is_raw_image(std::istream &is) noexcept
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

vector<ImagePtr> load_raw_image(std::istream &is, string_view filename, const ImageLoadOptions &opts)
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

    // Create custom datastream from std::istream
    LibRawIStream libraw_stream(is);

    // Open the RAW file using datastream (avoids loading entire file into memory)
    if (auto ret = processor->open_datastream(&libraw_stream); ret != LIBRAW_SUCCESS)
        throw std::runtime_error(fmt::format("Failed to open RAW file: {}", libraw_strerror(ret)));

    // Access image data directly from imgdata
    auto &idata = processor->imgdata;

    // Create HDRView image with oriented dimensions
    std::vector<ImagePtr> images;

    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();

    if (filter.PassFilter("main"))
    {
        try
        {
            // Unpack the RAW data
            if (auto ret = processor->unpack(); ret != LIBRAW_SUCCESS)
                throw std::runtime_error(fmt::format("Failed to unpack RAW data: {}", libraw_strerror(ret)));

            // Now process the full image (demosaic, white balance, etc.)
            if (auto ret = processor->dcraw_process(); ret != LIBRAW_SUCCESS)
                throw std::runtime_error(fmt::format("Failed to process RAW image: {}", libraw_strerror(ret)));

            auto &sizes = idata.sizes;

            // Use iwidth/iheight for the processed image dimensions
            int2 size{sizes.iwidth, sizes.iheight};
            int  flip         = sizes.flip;
            int  num_channels = 3; // Force RGB

            // Calculate oriented size based on flip
            int2 oriented_size = (flip & 4) ? int2{size.y, size.x} : size;

            // Verify we have image data
            if (!idata.image)
                throw std::runtime_error("No image data available after processing");

            auto image                = std::make_shared<Image>(oriented_size, num_channels);
            image->filename           = filename;
            image->partname           = "main";
            image->metadata["loader"] = "LibRaw";

            // Helper function to handle flip transformations
            auto flip_index = [&](int2 idx) -> int2
            {
                if (flip & 4)
                    std::swap(idx.x, idx.y);

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
            // We include an ad-hoc scale factor here to make the exposure match the DNG preview better
            constexpr float scale = 2.2f / 65535.0f;

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

            string profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
            if (opts.override_profile)
            {
                spdlog::info("Ignoring embedded color profile with user-specified profile: {} {}",
                             color_gamut_name(opts.gamut_override), transfer_function_name(opts.tf_override));

                Chromaticities chr;
                if (linearize_pixels(float_pixels.data(), int3{size, num_channels},
                                     gamut_chromaticities(opts.gamut_override), opts.tf_override, opts.keep_primaries,
                                     &profile_desc, &chr))
                {
                    image->chromaticities = chr;
                    profile_desc += " (override)";
                }
            }
            else
            {
                Chromaticities chr;
                // We configured LibRaw to output to linear sRGB above
                if (linearize_pixels(float_pixels.data(), int3{size, num_channels},
                                     gamut_chromaticities(ColorGamut_sRGB_BT709), TransferFunction::Linear,
                                     opts.keep_primaries, &profile_desc, &chr))
                    image->chromaticities = chr;
            }
            image->metadata["color profile"] = profile_desc;

            // Copy data to image channels
            for (int c = 0; c < num_channels; ++c)
                image->channels[c].copy_from_interleaved(float_pixels.data(), oriented_size.x, oriented_size.y,
                                                         num_channels, c, [](float v) { return v; });

            images.push_back(image);
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(fmt::format("Error processing RAW image: {}", e.what()));
        }
    }
    else
        spdlog::debug("Skipping main RAW image (filtered out by channel selector '{}')", opts.channel_selector);

    for (int ti = 0; ti < idata.thumbs_list.thumbcount; ti++)
    {
        string name = fmt::format("thumbnail:{}", ti);

        if (!filter.PassFilter(&name[0], &name[0] + name.size()))
        {
            spdlog::debug("Skipping thumbnail image {}: '{}' (filtered out by channel selector '{}')", ti, name,
                          opts.channel_selector);
            continue;
        }

        int tret = processor->unpack_thumb_ex(ti);
        if (tret != LIBRAW_SUCCESS)
            break; // no more thumbnails or error

        int                       err   = 0;
        libraw_processed_image_t *thumb = processor->dcraw_make_mem_thumb(&err);
        if (!thumb)
            continue;

        const auto guard = ScopeGuard([&]() { LibRaw::dcraw_clear_mem(thumb); });

        try
        {
            if (thumb->type == LIBRAW_IMAGE_JPEG)
            {
                std::string        s(reinterpret_cast<char *>(thumb->data), thumb->data_size);
                std::istringstream iss(s);
                auto               thumbs = load_jpg_image(iss, fmt::format("{}:thumb{}", filename, ti), opts);
                for (auto &ti_img : thumbs)
                {
                    ti_img->metadata["loader"]                 = "LibRaw";
                    ti_img->metadata["header"]["Is thumbnail"] = {
                        {"value", true},
                        {"string", "Yes"},
                        {"type", "bool"},
                        {"description", "Indicates this image is a thumbnail"}};
                    ti_img->partname = fmt::format("thumbnail:{}", ti);
                    images.push_back(ti_img);
                }
            }
            else if (thumb->type == LIBRAW_IMAGE_BITMAP)
            {
                int  w         = thumb->width;
                int  h         = thumb->height;
                int  n         = thumb->colors;
                auto timg      = std::make_shared<Image>(int2{w, h}, n);
                timg->filename = filename;
                timg->partname = fmt::format("thumbnail:{}", ti);
                timg->metadata["pixel format"] =
                    fmt::format("{}-bit ({} bpc)", thumb->colors * thumb->bits, thumb->bits);
                timg->metadata["loader"]                 = "LibRaw";
                timg->metadata["header"]["Is thumbnail"] = {{"value", true},
                                                            {"string", "Yes"},
                                                            {"type", "bool"},
                                                            {"description", "Indicates this image is a thumbnail"}};
                timg->partname                           = fmt::format("thumbnail:{}", ti);

                // Load interleaved bytes/shorts into a float buffer, then linearize
                std::vector<float> float_pixels((size_t)w * h * n);
                if (thumb->bits == 8)
                {
                    auto data8 = reinterpret_cast<uint8_t *>(thumb->data);
                    for (size_t i = 0; i < (size_t)w * h * n; ++i) float_pixels[i] = data8[i] / 255.0f;
                }
                else if (thumb->bits == 16)
                {
                    auto data16 = reinterpret_cast<uint16_t *>(thumb->data);
                    for (size_t i = 0; i < (size_t)w * h * n; ++i) float_pixels[i] = data16[i] / 65535.0f;
                }

                // Apply sRGB->linear correction to bitmap thumbnails (fix washed-out appearance)
                string profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::sRGB);
                if (opts.override_profile)
                {
                    spdlog::info("Ignoring embedded color profile with user-specified profile: {} {}",
                                 color_gamut_name(opts.gamut_override), transfer_function_name(opts.tf_override));

                    Chromaticities chr;
                    if (linearize_pixels(float_pixels.data(), int3{w, h, n}, gamut_chromaticities(opts.gamut_override),
                                         opts.tf_override, opts.keep_primaries, &profile_desc, &chr))
                    {
                        timg->chromaticities = chr;
                        profile_desc += " (override)";
                    }
                }
                else
                {
                    Chromaticities chr;
                    // LibRaw bitmap thumbnails are in sRGB color space
                    if (linearize_pixels(float_pixels.data(), int3{w, h, n},
                                         gamut_chromaticities(ColorGamut_sRGB_BT709), TransferFunction::sRGB,
                                         opts.keep_primaries, &profile_desc, &chr))
                        timg->chromaticities = chr;
                }
                timg->metadata["color profile"] = profile_desc;

                for (int c = 0; c < n; ++c)
                    timg->channels[c].copy_from_interleaved<float>(float_pixels.data(), w, h, n, c,
                                                                   [](float v) { return v; });

                images.push_back(timg);
            }
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Error loading thumbnail {}: {}", ti, e.what());
        }
    }

    spdlog::info("Loaded RAW image in {:.2f}ms", timer.elapsed());

    // Attach EXIF/XMP metadata (if present) to all images (thumbnails + main)
    if (!exif_ctx.metadata.empty())
    {
        for (auto &img_ptr : images) { img_ptr->metadata["exif"] = exif_ctx.metadata; }
    }
    else
    {
        spdlog::warn("No EXIF metadata extracted from RAW file");
    }

    return images;
}

#endif
