//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "jpg.h"
#include "colorspace.h"
#include "exif.h"
#include "icc.h"
#include "image.h"
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

#ifndef HDRVIEW_ENABLE_LIBJPEG

#include "stb.h"

bool is_jpg_image(istream &is) noexcept { return false; }

std::vector<ImagePtr> load_jpg_image(std::istream &is, std::string_view filename, std::string_view channel_selector)
{
    throw runtime_error("Turbo JPEG support not enabled in this build.");
}

void save_jpg_image(const Image &img, std::ostream &os, std::string_view filename, float gain, bool sRGB, bool dither,
                    int quality, bool progressive)
{
    return save_stb_jpg(img, os, filename, gain, sRGB, dither, quality);
}

#else

#include <jerror.h>
#include <jpeglib.h>

bool is_jpg_image(std::istream &is) noexcept
{
    try
    {
        unsigned char magic[2];
        is.read(reinterpret_cast<char *>(magic), 2);
        is.clear();
        is.seekg(0);
        return magic[0] == 0xFF && magic[1] == 0xD8;
    }
    catch (...)
    {
        is.clear();
        is.seekg(0);
        return false;
    }
}

std::vector<ImagePtr> load_jpg_image(std::istream &is, std::string_view filename, std::string_view channel_selector)
{
    ScopedMDC mdc{"IO", "JPG"};

    struct jpeg_stream_source_mgr : public jpeg_source_mgr
    {
        std::istream       *is;
        std::vector<JOCTET> buffer;
        jpeg_stream_source_mgr(std::istream &input, size_t bufsize = 4096) : is(&input), buffer(bufsize)
        {
            init_source       = [](j_decompress_ptr) {};
            fill_input_buffer = [](j_decompress_ptr cinfo) -> boolean
            {
                auto *src = static_cast<jpeg_stream_source_mgr *>(cinfo->src);
                src->is->read(reinterpret_cast<char *>(src->buffer.data()), src->buffer.size());
                size_t n = src->is->gcount();
                if (n == 0)
                {
                    src->buffer[0]       = (JOCTET)0xFF;
                    src->buffer[1]       = (JOCTET)JPEG_EOI;
                    src->next_input_byte = src->buffer.data();
                    src->bytes_in_buffer = 2;
                    return TRUE;
                }
                src->next_input_byte = src->buffer.data();
                src->bytes_in_buffer = n;
                return TRUE;
            };
            skip_input_data = [](j_decompress_ptr cinfo, long num_bytes)
            {
                auto *src = static_cast<jpeg_stream_source_mgr *>(cinfo->src);
                if (num_bytes > 0)
                {
                    while (num_bytes > (long)src->bytes_in_buffer)
                    {
                        num_bytes -= (long)src->bytes_in_buffer;
                        src->fill_input_buffer(cinfo);
                    }
                    src->next_input_byte += num_bytes;
                    src->bytes_in_buffer -= num_bytes;
                }
            };
            resync_to_restart = jpeg_resync_to_restart;
            term_source       = [](j_decompress_ptr) {};
            next_input_byte   = buffer.data();
            bytes_in_buffer   = 0;
        }
    };

    jpeg_stream_source_mgr src_mgr(is);
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr         jerr;

    cinfo.err       = jpeg_std_error(&jerr);
    jerr.error_exit = [](j_common_ptr cinfo)
    {
        char buffer[JMSG_LENGTH_MAX];
        (*cinfo->err->format_message)(cinfo, buffer);
        throw std::invalid_argument{buffer};
    };

    jpeg_create_decompress(&cinfo);
    auto cinfo_deleter = [](jpeg_decompress_struct *cinfo) { jpeg_destroy_decompress(cinfo); };
    std::unique_ptr<jpeg_decompress_struct, decltype(cinfo_deleter)> cinfo_guard(&cinfo, cinfo_deleter);

    cinfo.src = reinterpret_cast<jpeg_source_mgr *>(&src_mgr);

    try
    {
        jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF); // EXIF, XMP
        jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF); // ICC, ISO

        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
            throw std::invalid_argument{"Failed to read JPEG header."};

        // ICC profile extraction
        std::vector<uint8_t> icc_profile;
        {
            unsigned char *icc_data = nullptr;
            unsigned int   icc_len  = 0;
            if (jpeg_read_icc_profile(&cinfo, &icc_data, &icc_len))
            {
                spdlog::warn("Read in ICC profile from JPEG.");
                icc_profile.assign(icc_data, icc_data + icc_len);
                free(icc_data);
            }
        }

        // EXIF extraction (APP1 marker)
        std::vector<uint8_t> exif_data;
        for (jpeg_saved_marker_ptr marker = cinfo.marker_list; marker; marker = marker->next)
        {
            if (marker->marker == JPEG_APP0 + 1 && marker->data_length > 6 &&
                std::memcmp(marker->data, "Exif\0\0", 6) == 0)
            {
                exif_data.assign(marker->data + 6, marker->data + marker->data_length);
                break;
            }
        }

        jpeg_start_decompress(&cinfo);
        int3 size{(int)cinfo.output_width, (int)cinfo.output_height, (int)cinfo.output_components};
        auto image                     = make_shared<Image>(size.xy(), size.z);
        image->filename                = filename;
        image->file_has_straight_alpha = false;
        image->metadata["loader"]      = "libjpeg-turbo";
        auto color_space_name          = [](J_COLOR_SPACE cp)
        {
            switch (cp)
            {
            case JCS_UNKNOWN: return "Unknown";
            case JCS_GRAYSCALE: return "Grayscale";
            case JCS_RGB: return "RGB";
            case JCS_YCbCr: return "YCbCr";
            case JCS_CMYK: return "CMYK";
            case JCS_YCCK: return "YCCK";
            case JCS_EXT_RGB: return "Extended RGB";
            case JCS_EXT_RGBX: return "Extended RGBX";
            case JCS_EXT_BGR: return "Extended BGR";
            case JCS_EXT_BGRX: return "Extended BGRX";
            case JCS_EXT_XBGR: return "Extended XBGR";
            case JCS_EXT_XRGB: return "Extended XRGB";
            case JCS_EXT_RGBA: return "Extended RGBA";
            case JCS_EXT_BGRA: return "Extended BGRA";
            case JCS_EXT_ABGR: return "Extended ABGR";
            case JCS_EXT_ARGB: return "Extended ARGB";
            case JCS_RGB565: return "RGB565";
            }
        };
        image->metadata["pixel format"] =
            fmt::format("{} ({} channel{}, {} bpc)", color_space_name(cinfo.jpeg_color_space), cinfo.num_components,
                        cinfo.num_components > 1 ? "s" : "", cinfo.data_precision);
#if JPEG_LIB_VERSION >= 80
        image->metadata["header"]["baseline"] = {
            {"value", cinfo.is_baseline}, {"string", cinfo.is_baseline ? "true" : "false"}, {"type", "bool"}};
#endif
        image->metadata["header"]["progressive"] = {
            {"value", cinfo.progressive_mode}, {"string", cinfo.progressive_mode ? "true" : "false"}, {"type", "bool"}};

        image->metadata["header"]["coding"] = {
            {"value", cinfo.arith_code}, {"string", cinfo.arith_code ? "Arithmetic" : "Huffman"}, {"type", "bool"}};

        image->metadata["header"]["JFIF version"] = {
            {"value", 100 * cinfo.JFIF_major_version + cinfo.JFIF_minor_version},
            {"type", "float"},
            {"string", fmt::format("{}.{}", cinfo.JFIF_major_version, cinfo.JFIF_minor_version)}};
        image->metadata["header"]["density unit"] = {{"value", cinfo.density_unit},
                                                     {"string", cinfo.density_unit == 1   ? "dots/inch"
                                                                : cinfo.density_unit == 2 ? "dots/cm"
                                                                                          : "unknown"},
                                                     {"type", "uint8"}};
        image->metadata["header"]["X density"]    = {
            {"value", cinfo.X_density}, {"string", to_string(cinfo.X_density)}, {"type", "uint16"}};
        image->metadata["header"]["Y density"] = {
            {"value", cinfo.Y_density}, {"string", to_string(cinfo.Y_density)}, {"type", "uint16"}};
        image->metadata["header"]["has Adobe marker"] = {{"value", cinfo.saw_Adobe_marker != 0},
                                                         {"string", cinfo.saw_Adobe_marker ? "true" : "false"},
                                                         {"type", "bool"}};
        if (cinfo.saw_Adobe_marker)
            image->metadata["header"]["Adobe transform"] = {{"value", cinfo.Adobe_transform},
                                                            {"string", cinfo.Adobe_transform == 1 ? "YCbCr"
                                                                       : cinfo.Adobe_transform == 2
                                                                           ? "YCCK"
                                                                           : "Unknown (RGB or CMYK)"},
                                                            {"type", "uint8"}};

        if (!exif_data.empty())
        {
            try
            {
                image->metadata["exif"] = exif_to_json(exif_data.data(), exif_data.size());
                spdlog::debug("EXIF metadata successfully parsed: {}", image->metadata["exif"].dump(2));
            }
            catch (const std::exception &e)
            {
                spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
            }
        }

        std::vector<uint8_t> row_buffer(size.x * size.z);
        std::vector<float>   float_pixels(size.x * size.y * size.z);
        for (int y = 0; y < size.y; ++y)
        {
            JSAMPROW row_pointer = row_buffer.data();
            jpeg_read_scanlines(&cinfo, &row_pointer, 1);

            for (int x = 0; x < size.x; ++x)
                for (int c = 0; c < size.z; ++c)
                    float_pixels[(y * size.x + x) * size.z + c] = dequantize_full(row_buffer[x * size.z + c]);
        }
        jpeg_finish_decompress(&cinfo);

        TransferFunction tf = TransferFunction_Unknown; // default
        std::string      tf_desc;

        // ICC profile linearization
        if (!icc_profile.empty())
        {
            Chromaticities chr;
            if (icc::linearize_colors(float_pixels.data(), size, icc_profile, &tf_desc, &chr))
            {
                spdlog::info("Linearizing colors using ICC profile.");
                image->chromaticities = chr;
            }
        }
        else if (tf != TransferFunction_Linear)
        {
            tf_desc = transfer_function_name(TransferFunction_Unknown);
            // If no ICC profile, assume sRGB transfer function
            to_linear(float_pixels.data(), size, tf, 2.2f);
        }
        image->metadata["transfer function"] = tf_desc;
        for (int c = 0; c < size.z; ++c)
            image->channels[c].copy_from_interleaved(float_pixels.data(), size.x, size.y, size.z, c,
                                                     [](float v) { return v; });

        return {image};
    }
    catch (const std::exception &e)
    {
        // jpeg_destroy_decompress will be called by unique_ptr deleter
        throw std::runtime_error(fmt::format("Error during decompression: {}", e.what()));
    }
}

void save_jpg_image(const Image &img, std::ostream &os, std::string_view filename, float gain, bool sRGB, bool dither,
                    int quality, bool progressive)
{
    // get interleaved LDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved<uint8_t>(&w, &h, &n, gain, sRGB ? TransferFunction_sRGB : TransferFunction_Linear,
                                              2.2f, dither);
    // Validation: ensure we actually have pixel data / valid dimensions
    if (!pixels || w <= 0 || h <= 0)
        throw runtime_error("JPEG: empty image or invalid image dimensions");

    if (n > 3)
    {
        // Remove alpha channel: convert RGBA to RGB in-place
        std::unique_ptr<uint8_t[]> rgb_pixels(new uint8_t[w * h * 3]);
        for (int i = 0, j = 0; i < w * h; ++i, j += n)
        {
            rgb_pixels[i * 3 + 0] = pixels[j + 0];
            rgb_pixels[i * 3 + 1] = pixels[j + 1];
            rgb_pixels[i * 3 + 2] = pixels[j + 2];
        }
        pixels.swap(rgb_pixels);
        n = 3;
    }

    jpeg_compress_struct cinfo;
    jpeg_error_mgr       jerr;
    cinfo.err = jpeg_std_error(&jerr);

    auto cinfo_deleter = [](jpeg_compress_struct *cinfo) { jpeg_destroy_compress(cinfo); };
    std::unique_ptr<jpeg_compress_struct, decltype(cinfo_deleter)> cinfo_guard(&cinfo, cinfo_deleter);

    jpeg_create_compress(&cinfo);

    struct ostream_dest_mgr : public jpeg_destination_mgr
    {
        std::ostream       *os;
        std::vector<JOCTET> buffer;
        ostream_dest_mgr(std::ostream &out, size_t bufsize = 4096) : os(&out), buffer(bufsize)
        {
            init_destination = [](j_compress_ptr cinfo)
            {
                auto *dest             = static_cast<ostream_dest_mgr *>(cinfo->dest);
                dest->next_output_byte = dest->buffer.data();
                dest->free_in_buffer   = dest->buffer.size();
            };
            empty_output_buffer = [](j_compress_ptr cinfo) -> boolean
            {
                auto *dest = static_cast<ostream_dest_mgr *>(cinfo->dest);
                dest->os->write(reinterpret_cast<char *>(dest->buffer.data()), dest->buffer.size());
                dest->next_output_byte = dest->buffer.data();
                dest->free_in_buffer   = dest->buffer.size();
                return TRUE;
            };
            term_destination = [](j_compress_ptr cinfo)
            {
                auto  *dest      = static_cast<ostream_dest_mgr *>(cinfo->dest);
                size_t datacount = dest->buffer.size() - dest->free_in_buffer;
                if (datacount > 0)
                    dest->os->write(reinterpret_cast<char *>(dest->buffer.data()), datacount);
            };
        }
    } dest_mgr(os);
    cinfo.dest = reinterpret_cast<jpeg_destination_mgr *>(&dest_mgr);

    cinfo.image_width      = w;
    cinfo.image_height     = h;
    cinfo.input_components = n;
    cinfo.in_color_space   = (img.channels.size() == 1) ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    if (progressive)
        jpeg_simple_progression(&cinfo);

    jpeg_start_compress(&cinfo, TRUE);
    // write scanlines one row at a time with a JSAMPROW pointer to each row
    const size_t row_stride = size_t(w) * size_t(n); // bytes per row
    for (int y = 0; y < h; ++y)
    {
        JSAMPROW row_pointer = pixels.get() + size_t(y) * row_stride;
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
}

#endif
