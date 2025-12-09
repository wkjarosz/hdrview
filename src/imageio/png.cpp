//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "png.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "exif.h"
#include "icc.h"
#include "image.h"
#include "timer.h"
#include <optional>

#include "fonts.h"
#include "imgui_ext.h"

struct PNGSaveOptions
{
    float            gain            = 1.f;
    bool             dither          = true;
    TransferFunction tf              = TransferFunction::sRGB;
    int              data_type_index = 0;
    bool             interlaced      = false;
};

static PNGSaveOptions s_opts;

#ifndef HDRVIEW_ENABLE_LIBPNG

bool is_png_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_png_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("PNG support not enabled in this build.");
}

void save_png_image(const Image &img, std::ostream &os, std::string_view filename, float gain, bool sRGB, bool dither)
{
    throw runtime_error("PNG support not enabled in this build.");
}

PNGSaveOptions *png_parameters_gui() { return &s_opts; }

void save_png_image(const Image &img, std::ostream &os, std::string_view filename, const PNGSaveOptions *params)
{
    throw runtime_error("PNG support not enabled in this build.");
}

#else

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

// Return the offset to start of the sequence "Exif\0\0" in `data` of length `len`, or -1 if not found
int find_exif_signature_offset(const char *data, int len)
{
    if (!data || len <= 6)
        return -1;
    static const char sig[6] = {'E', 'x', 'i', 'f', '\0', '\0'};
    for (int i = 0; i + 6 <= len; ++i)
    {
        if (std::memcmp(data + i, sig, 6) == 0)
            return i;
    }
    return -1;
}

// returns new size of decoded buffer
int decode_ascii_hex_to_binary(uint8_t u8[], int length)
{
    int     r     = 0; // resulting length
    int     t     = 0; // temporary
    bool    first = true;
    bool    valid[256];
    uint8_t value[256];
    for (int i = 0; i < 256; i++) valid[i] = false;
    for (int i = '0'; i <= '9'; i++)
    {
        valid[i] = true;
        value[i] = i - '0';
    }
    for (int i = 'a'; i <= 'f'; i++)
    {
        valid[i] = true;
        value[i] = 10 + i - 'a';
    }
    for (int i = 'A'; i <= 'F'; i++)
    {
        valid[i] = true;
        value[i] = 10 + i - 'A';
    }

    for (int i = 0; i < length; i++)
    {
        unsigned char x = u8[i];
        if (valid[x])
        {
            if (first)
            {
                t     = value[x] << 4;
                first = false;
            }
            else
            {
                first   = true;
                u8[r++] = t + value[x];
            }
        }
    }
    return r;
}

json decode_exif_text(char *text, size_t len)
{
    spdlog::info("Found Raw EXIF data in text chunk");
    try
    {
        uint8_t *u8         = reinterpret_cast<uint8_t *>(text);
        auto     binary_len = decode_ascii_hex_to_binary(u8, len);
        int      hex_offset = find_exif_signature_offset(text, binary_len);
        if (hex_offset < 0)
        {
            spdlog::warn("EXIF signature not found");
            return {};
        }

        int remaining = binary_len - hex_offset;
        return exif_to_json(u8 + hex_offset, remaining);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
        return {};
    }
}

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

vector<ImagePtr> load_png_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "PNG"};
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
        throw invalid_argument{fmt::format("Requested bit depth to be either 8 or 16 now, but received {}", bit_depth)};

    json metadata = json::object();

    //
    // Read color chunks in reverse priority order
    //

    std::vector<uint8_t> icc_profile;
    png_charp            icc_name         = nullptr;
    int                  compression_type = 0;
    png_bytep            icc_ptr          = nullptr;
    png_uint_32          icc_len          = 0;

    if (png_get_iCCP(png_ptr, info_ptr.get(), &icc_name, &compression_type, &icc_ptr, &icc_len))
    {
        icc_profile.assign(icc_ptr, icc_ptr + icc_len);
        spdlog::info("Found ICC profile: {} ({} bytes)", icc_name, icc_len);
    }

    double           gamma       = 2.2; // default gamma
    int              srgb_intent = 0;
    TransferFunction tf          = TransferFunction::Unspecified; // default

    if (png_get_gAMA(png_ptr, info_ptr.get(), &gamma))
    {
        tf.type  = TransferFunction::Gamma;
        tf.gamma = float(1.0 / gamma);
        spdlog::info("Found gamma chunk: {:.4f}", 1.0 / gamma);
    }

    std::optional<Chromaticities> chr;

    // Read chromaticities if present
    double wx, wy, rx, ry, gx, gy, bx, by;
    if (png_get_cHRM(png_ptr, info_ptr.get(), &wx, &wy, &rx, &ry, &gx, &gy, &bx, &by))
    {
        spdlog::info("Found chromaticities chunk: R({:.4f},{:.4f}) G({:.4f},{:.4f}) B({:.4f},{:.4f}) W({:.4f},{:.4f})",
                     rx, ry, gx, gy, bx, by, wx, wy);
        *chr = Chromaticities{float2(rx, ry), float2(gx, gy), float2(bx, by), float2(wx, wy)};
    }

    if (png_get_sRGB(png_ptr, info_ptr.get(), &srgb_intent))
    {
        spdlog::info("Found sRGB chunk. sRGB intent: {}", srgb_intent);
        tf = TransferFunction::sRGB;
    }

    bool     has_cICP              = false;
    png_byte video_full_range_flag = 1;
    png_byte color_primaries;
    png_byte transfer_function;
    png_byte matrix_coefficients;
#ifdef PNG_cICP_SUPPORTED

    if (png_get_cICP(png_ptr, info_ptr.get(), &color_primaries, &transfer_function, &matrix_coefficients,
                     &video_full_range_flag))
    {
        has_cICP = true;
        spdlog::info("Found cICP chunk:\n\tcolor Primaries: {}\n\tTransfer Function: {}\n\t"
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
            spdlog::warn("Unknown cICP color primaries: {}", int(color_primaries));
        }

        tf = transfer_function_from_cicp(transfer_function);
        if (tf.type == TransferFunction::Unspecified)
            spdlog::warn("cICP transfer function ({}) is not recognized, assuming sRGB", transfer_function);

        metadata["header"]["CICP video full range"] = {{"value", video_full_range_flag != 0},
                                                       {"string", video_full_range_flag ? "true" : "false"},
                                                       {"type", "bool"}};
        metadata["header"]["CICP triple"]           = {
            {"value", {color_primaries, transfer_function, matrix_coefficients}},
            {"string", fmt::format("CP={}, TF={}, MC={}", color_primaries, transfer_function, matrix_coefficients)},
            {"type", "array"},
            {"description",
                       "Coding-independent code points (CICP) is a way to signal the color properties of the image via three "
                                 "numbers: color primaries (CP), transfer function (TF), and matrix coefficients (MC)."}};
    }
#endif

    string tf_desc = transfer_function_name(tf);

    // Done reading color chunks
    //

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
            if (string(text_ptr[t].key) == string("Raw profile type exif"))
            {
                if (auto j = decode_exif_text(text_ptr[t].text, text_ptr[t].text_length); !j.empty())
                    metadata["exif"] = j;
            }
            else if (string(text_ptr[t].key) == string("XML:com.adobe.xmp"))
            {
                spdlog::info("Found XMP chunk in text data: {}", text_ptr[t].text);
                metadata["header"]["XMP"] = {{"value", text_ptr[t].text},
                                             {"string", text_ptr[t].text},
                                             {"type", "string"},
                                             {"description", "XMP metadata"}};
            }
            else
            {
                spdlog::debug("text {} : {}", text_ptr[t].key, text_ptr[t].text);
                metadata["header"][text_ptr[t].key] = {
                    {"value", text_ptr[t].text}, {"string", text_ptr[t].text}, {"type", "string"}};
            }
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
        spdlog::info("Found EXIF chunk ({} bytes)", exif_len);
        try
        {
            metadata["exif"] = exif_to_json(exif_ptr, exif_len);
            spdlog::debug("EXIF metadata successfully parsed: {}", metadata["exif"].dump(2));
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
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
        spdlog::info("Detected APNG with {} frames, {} plays", num_frames, num_plays);
    else
        num_frames = 1, num_plays = 0;

    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();

    std::vector<ImagePtr> images;
    for (png_uint_32 frame_idx = 0; frame_idx < num_frames; ++frame_idx)
    {
#ifdef PNG_APNG_SUPPORTED
        if (frame_idx > 0)
            // Advance to next frame
            png_read_frame_head(png_ptr, info_ptr.get());
#endif

        png_uint_32 frame_width = width, frame_height = height;
        png_uint_32 frame_x_off = 0, frame_y_off = 0;
#ifdef PNG_APNG_SUPPORTED
        if (animation)
        {
            png_uint_16 delay_num = 0, delay_den = 0;
            png_byte    dispose_op = 0, blend_op = 0;
            // Get frame control info
            png_get_next_frame_fcTL(png_ptr, info_ptr.get(), &frame_width, &frame_height, &frame_x_off, &frame_y_off,
                                    &delay_num, &delay_den, &dispose_op, &blend_op);
        }
#endif

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
                spdlog::debug("Skipping frame {} (filtered out by channel selector)", frame_idx);
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
        if (!icc_profile.empty())
            image->icc_data = icc_profile;
        if (opts.tf_override.type == TransferFunction::Unspecified)
        {
            if (!icc_profile.empty() && !has_cICP)
            {
                Chromaticities chr;
                if (icc::linearize_colors(float_pixels.data(), size, icc_profile, &tf_desc, &chr))
                {
                    spdlog::info("Linearizing colors using ICC profile.");
                    image->chromaticities = chr;
                }
            }
            else if (tf.type != TransferFunction::Linear)
                to_linear(float_pixels.data(), size, tf);

            image->metadata["transfer function"] = tf_desc;
        }
        else
        {
            spdlog::info("Ignoring embedded color profile and linearizing using requested transfer function: {}",
                         transfer_function_name(opts.tf_override));
            try
            {
                // some cICP transfer functions always correspond to certain primaries, try to deduce that
                image->chromaticities = chromaticities_from_cicp(transfer_function_to_cicp(opts.tf_override.type));
            }
            catch (...)
            {
                spdlog::warn("Failed to infer chromaticities from transfer function cICP value: {}",
                             int(opts.tf_override.type));
            }
            to_linear(float_pixels.data(), size, opts.tf_override);
            image->metadata["transfer function"] = transfer_function_name(opts.tf_override);
        }

        for (int c = 0; c < size.z; ++c)
            image->channels[c].copy_from_interleaved(float_pixels.data(), size.x, size.y, size.z, c,
                                                     [](float v) { return v; });
        images.push_back(image);
    }
    return images;
}

void save_png_image(const Image &img, ostream &os, string_view filename, float gain, bool dither, bool interlaced,
                    bool sixteen_bit, TransferFunction tf)
{
    Timer                       timer;
    int                         w = 0, h = 0, n = 0;
    std::unique_ptr<uint8_t[]>  pixels8;
    std::unique_ptr<uint16_t[]> pixels16;
    void                       *pixels = nullptr;

    int cicp_primaries = 2;
    if (img.chromaticities)
        cicp_primaries = chromaticities_to_cicp(*img.chromaticities);
    // if cicp_primaries are unrecognized (< 0), we will convert to sRGB/Rec709

    if (sixteen_bit)
    {
        pixels16 = img.as_interleaved<uint16_t>(&w, &h, &n, gain, tf, dither, true, cicp_primaries < 0);
        if (is_little_endian())
        {
            // Swap bytes for each 16-bit pixel (big-endian required by PNG)
            for (int i = 0; i < w * h * n; ++i)
            {
                uint16_t v  = pixels16[i];
                pixels16[i] = (v >> 8) | (v << 8);
            }
        }
        pixels = pixels16.get();
    }
    else
    {
        pixels8 = img.as_interleaved<uint8_t>(&w, &h, &n, gain, tf, dither, true, cicp_primaries < 0);
        pixels  = pixels8.get();
    }

    if (!pixels || w <= 0 || h <= 0)
        throw runtime_error("PNG: empty image or invalid image dimensions");

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

    int bit_depth = sixteen_bit ? 16 : 8;

    png_set_IHDR(png_ptr, info_ptr.get(), w, h, bit_depth, color_type,
                 interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    if (img.chromaticities)
    {
        // to make sure
        auto chr = (cicp_primaries < 0) ? Chromaticities{} : *img.chromaticities;
        png_set_cHRM(png_ptr, info_ptr.get(), chr.white.x, chr.white.y, chr.red.x, chr.red.y, chr.green.x, chr.green.y,
                     chr.blue.x, chr.blue.y);
    }

#ifdef PNG_cICP_SUPPORTED
    // if cicp_primaries are unrecognized (< 0), we already converted values to sRGB/BT.709
    png_byte color_primaries     = cicp_primaries < 0 ? 1 : cicp_primaries; // e.g. 1 for BT.709, 9 for BT.2020
    png_byte transfer_function   = transfer_function_to_cicp(tf); // e.g. 13 for sRGB/BT.709, 8 for linear, 16 for PQ
    png_byte matrix_coefficients = 0;                             // e.g. 0 for RGB
    png_byte video_full_range    = 1;                             // 1 for full range, 0 for limited

    png_set_cICP(png_ptr, info_ptr.get(), color_primaries, transfer_function, matrix_coefficients, video_full_range);
#endif

    png_write_info(png_ptr, info_ptr.get());

    // Ask libpng for the correct rowbytes (safer than w * n)
    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr.get());
    if (row_bytes != size_t(w * n * bit_depth / 8))
        throw runtime_error("PNG: mismatched rowbytes");

    // build row pointers and let libpng handle Adam7 passes
    std::vector<png_bytep> row_pointers(h);
    for (int y = 0; y < h; ++y)
        row_pointers[y] =
            sixteen_bit ? reinterpret_cast<png_bytep>(pixels16.get()) + y * row_bytes : pixels8.get() + y * row_bytes;

    png_write_image(png_ptr, row_pointers.data());

    png_write_end(png_ptr, info_ptr.get());
}

PNGSaveOptions *png_parameters_gui()
{
    if (ImGui::PE::Begin("libPNG Save Options", ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

        ImGui::PE::Entry(
            "Gain",
            [&]
            {
                ImGui::BeginGroup();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::IconButtonSize().x -
                                        ImGui::GetStyle().ItemInnerSpacing.x);
                auto changed = ImGui::SliderFloat("##Gain", &s_opts.gain, 0.1f, 10.0f);
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::IconButton(ICON_MY_EXPOSURE))
                    s_opts.gain = exp2f(hdrview()->exposure());
                ImGui::Tooltip("Set gain from the current viewport exposure value.");
                ImGui::EndGroup();
                return changed;
            },
            "Multiply the pixels by this value before saving.");

        ImGui::PE::Entry(
            "Transfer function",
            [&]
            {
                if (ImGui::BeginCombo("##Transfer function", transfer_function_name(s_opts.tf).c_str()))
                {
                    for (int i = TransferFunction::Linear; i <= TransferFunction::DCI_P3; ++i)
                    {
                        bool is_selected = (s_opts.tf.type == (TransferFunction::Type_)i);
                        if (ImGui::Selectable(
                                transfer_function_name({(TransferFunction::Type_)i, s_opts.tf.gamma}).c_str(),
                                is_selected))
                            s_opts.tf.type = (TransferFunction::Type_)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.");

        if (s_opts.tf.type == TransferFunction::Gamma)
            ImGui::PE::SliderFloat("Gamma", &s_opts.tf.gamma, 0.1f, 5.f, "%.3f", 0,
                                   "When using a gamma transfer function, this is the gamma value to use.");

        ImGui::PE::Checkbox("Dither", &s_opts.dither);
        ImGui::PE::Checkbox("Interlaced", &s_opts.interlaced);
        ImGui::PE::Combo("Pixel format", &s_opts.data_type_index, "UInt8\0UInt16\0");

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = PNGSaveOptions{};

    return &s_opts;
}

// throws on error
void save_png_image(const Image &img, std::ostream &os, std::string_view filename, const PNGSaveOptions *opts)
{
    if (opts == nullptr)
        throw std::invalid_argument("PNGSaveOptions pointer is null");

    save_png_image(img, os, filename, opts->gain, opts->dither, opts->interlaced, opts->data_type_index, opts->tf);
}

#endif