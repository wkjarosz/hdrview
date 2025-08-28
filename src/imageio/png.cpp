//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "png.h"
#include "colorspace.h"
#include "exif.h"
#include "icc.h"
#include "image.h"
#include "imgui.h"
#include "texture.h"
#include "timer.h"
#include <optional>

#ifndef HDRVIEW_ENABLE_LIBPNG

bool is_png_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_png_image(istream &is, string_view filename)
{
    throw runtime_error("PNG support not enabled in this build.");
}

#else

#include <cmath>
#include <cstring>
#include <fmt/core.h>
#include <png.h>
#include <stdexcept>
#include <vector>

#include <ImfHeader.h>
#include <ImfRgbaYca.h>
#include <ImfStandardAttributes.h>

using namespace std;

namespace
{

// Helper to check PNG signature
bool check_png_signature(istream &is)
{
    unsigned char sig[8];
    is.read(reinterpret_cast<char *>(sig), 8);
    bool is_png = !png_sig_cmp(sig, 0, 8);
    is.clear();
    is.seekg(0);
    return is_png;
}

template <bool Read = true>
struct PngInfoPtrDeleter
{
    mutable png_structp png_ptr;
    void                operator()(png_infop info_ptr) const
    {
        if constexpr (Read)
            png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        else
            png_destroy_write_struct(&png_ptr, &info_ptr);
    }
};

} // end anonymous namespace

bool is_png_image(istream &is) noexcept
{
    auto start = is.tellg();
    bool ret   = false;
    try
    {
        ret = check_png_signature(is);
    }
    catch (...)
    {
    }
    is.clear();
    is.seekg(start);
    return ret;
}

vector<ImagePtr> load_png_image(istream &is, string_view filename, string_view channel_selector)
{
    if (!check_png_signature(is))
        throw runtime_error("Not a PNG file");

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr)
        throw runtime_error("Failed to create PNG read struct");

    png_set_error_fn(
        png_ptr, nullptr, [](png_structp png_ptr, png_const_charp error_msg)
        { throw invalid_argument{fmt::format("PNG error: {}", error_msg)}; },
        [](png_structp png_ptr, png_const_charp warning_msg) { spdlog::warn("PNG warning: {}", warning_msg); });

    std::unique_ptr<png_info, PngInfoPtrDeleter<true>> info_ptr{png_create_info_struct(png_ptr),
                                                                PngInfoPtrDeleter<true>{png_ptr}};

    if (!info_ptr)
        throw runtime_error("Failed to create PNG info struct");

    png_set_read_fn(png_ptr, &is,
                    [](png_structp png_ptr, png_bytep data, png_size_t length)
                    {
                        auto *stream = static_cast<istream *>(png_get_io_ptr(png_ptr));
                        stream->read(reinterpret_cast<char *>(data), length);
                        if (stream->gcount() != (streamsize)length)
                            png_error(png_ptr, "Read error in PNG stream");
                    });

    png_read_info(png_ptr, info_ptr.get());

    png_uint_32 width, height;
    int         file_bit_depth, color_type, interlace;
    png_get_IHDR(png_ptr, info_ptr.get(), &width, &height, &file_bit_depth, &color_type, &interlace, nullptr, nullptr);

    // Convert palette to RGB, expand bit depths to 16-bit, add alpha if needed
    png_set_palette_to_rgb(png_ptr);
    png_set_expand_gray_1_2_4_to_8(png_ptr);
    png_set_tRNS_to_alpha(png_ptr);

    if (interlace != PNG_INTERLACE_NONE)
    {
        spdlog::debug("Image is interlaced. Converting to non-interlaced.");
        png_set_interlace_handling(png_ptr);
    }

    png_read_update_info(png_ptr, info_ptr.get());

    if (file_bit_depth > 8 && is_little_endian())
        png_set_swap(png_ptr);

    int channels  = png_get_channels(png_ptr, info_ptr.get());
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr.get());

    if (bit_depth != 8 && bit_depth != 16)
        throw invalid_argument{
            fmt::format("PNG: requested bit depth to be either 8 or 16 now, but received {}", bit_depth)};

    //
    // Read color chunks in reverse priority order
    //

    std::vector<uint8_t> icc_profile;
    bool                 has_icc_profile  = false;
    png_charp            icc_name         = nullptr;
    int                  compression_type = 0;
    png_bytep            icc_ptr          = nullptr;
    png_uint_32          icc_len          = 0;

    if (png_get_iCCP(png_ptr, info_ptr.get(), &icc_name, &compression_type, &icc_ptr, &icc_len))
    {
        icc_profile.assign(icc_ptr, icc_ptr + icc_len);
        has_icc_profile = true;
        spdlog::info("PNG: Found ICC profile: {} ({} bytes)", icc_name, icc_len);
    }

    double           gamma       = 2.2; // default gamma
    int              srgb_intent = 0;
    TransferFunction tf          = TransferFunction_Unknown; // default

    if (png_get_gAMA(png_ptr, info_ptr.get(), &gamma))
    {
        spdlog::info("PNG: Found gamma chunk: {:.4f}", gamma);
        tf = TransferFunction_Gamma;
    }

    std::optional<Chromaticities> chr;

    // Read chromaticities if present
    double wx, wy, rx, ry, gx, gy, bx, by;
    if (png_get_cHRM(png_ptr, info_ptr.get(), &wx, &wy, &rx, &ry, &gx, &gy, &bx, &by))
    {
        spdlog::info(
            "PNG: Found chromaticities chunk: R({:.4f},{:.4f}) G({:.4f},{:.4f}) B({:.4f},{:.4f}) W({:.4f},{:.4f})", rx,
            ry, gx, gy, bx, by, wx, wy);
        *chr = Chromaticities{float2(rx, ry), float2(gx, gy), float2(bx, by), float2(wx, wy)};
    }

    if (png_get_sRGB(png_ptr, info_ptr.get(), &srgb_intent))
    {
        spdlog::info("PNG: Found sRGB chunk. sRGB intent: {}", srgb_intent);
        tf = TransferFunction_sRGB;
    }

    bool     has_cICP              = false;
    png_byte video_full_range_flag = 1;
#ifdef PNG_cICP_SUPPORTED
    png_byte color_primaries;
    png_byte transfer_function;
    png_byte matrix_coefficients;

    if (png_get_cICP(png_ptr, info_ptr.get(), &color_primaries, &transfer_function, &matrix_coefficients,
                     &video_full_range_flag))
    {
        has_cICP = true;
        spdlog::info("PNG: Found cICP chunk:\n\tcolor Primaries: {}\n\tTransfer Function: {}\n\t"
                     "Matrix Coefficients: {}\n\tVideo Full Range: {}",
                     color_primaries, transfer_function, matrix_coefficients, video_full_range_flag);

        if (matrix_coefficients != 0)
            spdlog::warn(
                "Unsupported matrix coefficients in cICP chunk: {}. PNG images only support RGB (=0). Ignoring.",
                matrix_coefficients);

        try
        {
            chr = chromaticities_from_cicp(color_primaries);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("PNG: Unknown cICP color primaries: {}", int(color_primaries));
        }

        float gamma_f = gamma;
        tf            = transfer_function_from_cicp(transfer_function, &gamma_f);
        gamma         = gamma_f;
        if (tf == TransferFunction_Unknown)
            spdlog::warn("PNG: cICP transfer function ({}) is not recognized, assuming sRGB", transfer_function);
    }
#endif

    string tf_desc = transfer_function_name(tf, gamma);

    // Done reading color chunks
    //

    json metadata = json::object();

#if defined(PNG_TEXT_SUPPORTED)
    /* png_get_text returns the number of text chunks and writes a pointer to
       an array of png_text into the provided address. Pass the address of a
       png_textp variable. */
    png_textp text_ptr = nullptr;
    int       num_text = png_get_text(png_ptr, info_ptr.get(), &text_ptr, nullptr);
    if (num_text > 0 && text_ptr)
    {
        for (int t = 0; t < num_text; ++t)
        {
            spdlog::info("text {} : {}", text_ptr[t].key, text_ptr[t].text);
            metadata["header"][text_ptr[t].key] = {
                {"value", text_ptr[t].text}, {"string", text_ptr[t].text}, {"type", "string"}};
        }
    }
#endif

#if defined(PNG_EASY_ACCESS_SUPPORTED)
    {
        auto color_type_name = [](png_byte t)
        {
            switch (t)
            {
            case PNG_COLOR_TYPE_GRAY: return "Gray";
            case PNG_COLOR_TYPE_PALETTE: return "Palette";
            case PNG_COLOR_TYPE_RGB: return "RGB";
            case PNG_COLOR_TYPE_RGB_ALPHA: return "RGB+Alpha";
            case PNG_COLOR_TYPE_GRAY_ALPHA: return "Gray+Alpha";
            default: return "Unknown";
            }
        };
        auto b                           = png_get_color_type(png_ptr, info_ptr.get());
        metadata["header"]["color type"] = {
            {"value", b}, {"string", fmt::format("{} ({})", color_type_name(b), b)}, {"type", "int"}};

        b                                 = png_get_filter_type(png_ptr, info_ptr.get());
        metadata["header"]["filter type"] = {
            {"value", b},
            {"string", fmt::format("{} ({})", b == 0 ? "Default" : "Intrapixel Differencing", b)},
            {"type", "int"}};

        // b                                      = png_get_compression_type(png_ptr, info_ptr.get());
        // metadata["header"]["compression type"] = {
        //     {"value", b},
        //     {"string", fmt::format("{} ({})", b == 0 ? "Default" : "Intrapixel Differencing", b)},
        //     {"type", "int"}};

        b                                    = png_get_interlace_type(png_ptr, info_ptr.get());
        metadata["header"]["interlace type"] = {
            {"value", b},
            {"string", fmt::format("{} ({})", b == PNG_INTERLACE_NONE ? "None" : "Adam7", b)},
            {"type", "int"}};

        auto u32                                 = png_get_x_pixels_per_meter(png_ptr, info_ptr.get());
        metadata["header"]["x pixels per meter"] = {{"value", u32}, {"string", to_string(u32)}, {"type", "int"}};
        u32                                      = png_get_y_pixels_per_meter(png_ptr, info_ptr.get());
        metadata["header"]["y pixels per meter"] = {{"value", u32}, {"string", to_string(u32)}, {"type", "int"}};

        u32                                   = png_get_x_offset_pixels(png_ptr, info_ptr.get());
        metadata["header"]["x offset pixels"] = {{"value", u32}, {"string", to_string(u32)}, {"type", "int"}};
        u32                                   = png_get_y_offset_pixels(png_ptr, info_ptr.get());
        metadata["header"]["y offset pixels"] = {{"value", u32}, {"string", to_string(u32)}, {"type", "int"}};

        u32                                    = png_get_x_offset_microns(png_ptr, info_ptr.get());
        metadata["header"]["x offset microns"] = {{"value", u32}, {"string", to_string(u32)}, {"type", "int"}};
        u32                                    = png_get_y_offset_microns(png_ptr, info_ptr.get());
        metadata["header"]["y offset microns"] = {{"value", u32}, {"string", to_string(u32)}, {"type", "int"}};

        auto f                                   = png_get_pixel_aspect_ratio(png_ptr, info_ptr.get());
        metadata["header"]["pixel aspect ratio"] = {{"value", f}, {"string", to_string(f)}, {"type", "float"}};
    }
#endif

#if defined(PNG_eXIf_SUPPORTED)
    png_bytep   exif_ptr = nullptr;
    png_uint_32 exif_len = 0;
    if (png_get_eXIf_1(png_ptr, info_ptr.get(), &exif_len, &exif_ptr) && exif_ptr && exif_len > 0)
    {
        spdlog::info("PNG: Found EXIF chunk ({} bytes)", exif_len);
        try
        {
            metadata["exif"] = exif_to_json(exif_ptr, exif_len);
            spdlog::debug("PNG: EXIF metadata successfully parsed: {}", metadata["exif"].dump(2));
        }
        catch (const std::exception &e)
        {
            spdlog::warn("PNG: Exception while parsing EXIF chunk: {}", e.what());
        }
    }
#endif

    metadata["loader"] = "libpng";
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        metadata["pixel format"] = fmt::format("{}-bit indexed color", file_bit_depth);
    else
        metadata["pixel format"] = fmt::format("{}-bit ({} bpc)", channels * file_bit_depth, file_bit_depth);

    png_uint_32 num_frames = 0, num_plays = 0;
    bool        animation = false;
#ifdef PNG_APNG_SUPPORTED
    animation = png_get_acTL(png_ptr, info_ptr.get(), &num_frames, &num_plays) && num_frames > 1;
#endif

    if (animation)
        spdlog::info("PNG: Detected APNG with {} frames, {} plays", num_frames, num_plays);
    else
        num_frames = 1, num_plays = 0;

    ImGuiTextFilter filter{string(channel_selector).c_str()};
    filter.Build();

    std::vector<ImagePtr> images;
    for (png_uint_32 frame_idx = 0; frame_idx < num_frames; ++frame_idx)
    {
        if (frame_idx > 0)
            // Advance to next frame
            png_read_frame_head(png_ptr, info_ptr.get());

        png_uint_32 frame_width = width, frame_height = height;
        png_uint_32 frame_x_off = 0, frame_y_off = 0;
        if (animation)
        {
            png_uint_16 delay_num = 0, delay_den = 0;
            png_byte    dispose_op = 0, blend_op = 0;
            // Get frame control info
            png_get_next_frame_fcTL(png_ptr, info_ptr.get(), &frame_width, &frame_height, &frame_x_off, &frame_y_off,
                                    &delay_num, &delay_den, &dispose_op, &blend_op);
        }

        int3 size{int(frame_width), int(frame_height), channels};
        auto image                     = make_shared<Image>(size.xy(), size.z);
        image->filename                = filename;
        image->file_has_straight_alpha = size.z == 4 || size.z == 2;
        image->chromaticities          = chr;
        image->metadata                = metadata;

        if (animation)
        {
            image->partname       = fmt::format("frame {:04}", frame_idx);
            image->data_window    = Box2i{int2(frame_x_off, frame_y_off), int2(frame_x_off, frame_y_off) + size.xy()};
            image->display_window = Box2i{int2{0}, int2(width, height)};

            if (!filter.PassFilter(&image->partname[0], &image->partname[0] + image->partname.size()))
            {
                spdlog::debug("PNG: Skipping frame {} (filtered out by channel selector)", frame_idx);
                continue;
            }
        }

        const auto             num_pixels        = size_t(size.x * size.y);
        const auto             bytes_per_channel = size_t(bit_depth / 8);
        const auto             bytes_per_pixel   = bytes_per_channel * image->channels.size();
        std::vector<png_byte>  imagedata(num_pixels * bytes_per_pixel);
        std::vector<png_bytep> row_pointers(size.y);
        for (int y = 0; y < size.y; ++y) row_pointers[y] = &imagedata[y * size.x * bytes_per_pixel];
        png_read_image(png_ptr, row_pointers.data());

        // process and copy over the pixel data
        std::vector<float> float_pixels(size.x * size.y * size.z);
        if (video_full_range_flag)
            for (size_t i = 0; i < float_pixels.size(); ++i)
                float_pixels[i] = bit_depth == 16
                                      ? dequantize_full(reinterpret_cast<const uint16_t *>(imagedata.data())[i])
                                      : dequantize_full(reinterpret_cast<const uint8_t *>(imagedata.data())[i]);
        else
            for (size_t i = 0; i < float_pixels.size(); ++i)
                float_pixels[i] = bit_depth == 16
                                      ? dequantize_narrow(reinterpret_cast<const uint16_t *>(imagedata.data())[i])
                                      : dequantize_narrow(reinterpret_cast<const uint8_t *>(imagedata.data())[i]);
        // ICC profile linearization
        if (has_icc_profile && !has_cICP)
        {
            Chromaticities chr;
            if (icc::linearize_colors(float_pixels.data(), size, icc_profile, &tf_desc, &chr))
            {
                spdlog::info("PNG: Linearizing colors using ICC profile.");
                image->chromaticities = chr;
            }
        }
        else if (tf != TransferFunction_Linear)
        {
            to_linear(float_pixels.data(), size, tf, gamma);
        }
        image->metadata["transfer function"] = tf_desc;
        for (int c = 0; c < size.z; ++c)
            image->channels[c].copy_from_interleaved(float_pixels.data(), size.x, size.y, size.z, c,
                                                     [](float v) { return v; });
        images.push_back(image);
    }
    return images;
}

void save_png_image(const Image &img, ostream &os, string_view filename, float gain, bool sRGB, bool dither)
{
    Timer timer;
    // get interleaved LDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved_bytes(&w, &h, &n, gain, sRGB, dither);

    if (n != 1 && n != 2 && n != 3 && n != 4)
        throw runtime_error("Unsupported channel count for PNG");

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr)
        throw runtime_error("Failed to create PNG write struct");

    png_set_error_fn(
        png_ptr, nullptr, [](png_structp png_ptr, png_const_charp error_msg)
        { throw invalid_argument{fmt::format("PNG error: {}", error_msg)}; },
        [](png_structp png_ptr, png_const_charp warning_msg) { spdlog::warn("PNG warning: {}", warning_msg); });

    std::unique_ptr<png_info, PngInfoPtrDeleter<false>> info_ptr{png_create_info_struct(png_ptr),
                                                                 PngInfoPtrDeleter<false>{png_ptr}};

    if (!info_ptr)
        throw runtime_error("Failed to create PNG info struct");

    png_set_write_fn(
        png_ptr, &os,
        [](png_structp png_ptr, png_bytep data, png_size_t length)
        {
            auto *stream = static_cast<ostream *>(png_get_io_ptr(png_ptr));
            stream->write(reinterpret_cast<char *>(data), length);
            if (!stream->good())
                png_error(png_ptr, "Write error in PNG stream");
        },
        [](png_structp png_ptr)
        {
            auto *stream = static_cast<ostream *>(png_get_io_ptr(png_ptr));
            stream->flush();
        });

    int color_type = (n == 1)   ? PNG_COLOR_TYPE_GRAY
                     : (n == 2) ? PNG_COLOR_TYPE_GRAY_ALPHA
                     : (n == 3) ? PNG_COLOR_TYPE_RGB
                                : PNG_COLOR_TYPE_RGB_ALPHA;

    png_set_IHDR(png_ptr, info_ptr.get(), w, h, 8, color_type, PNG_INTERLACE_ADAM7, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr.get());

    // Write image data row by row
    size_t row_bytes = w * n;
    for (int y = 0; y < h; ++y) png_write_row(png_ptr, pixels.get() + y * row_bytes);

    png_write_end(png_ptr, info_ptr.get());
}

#endif