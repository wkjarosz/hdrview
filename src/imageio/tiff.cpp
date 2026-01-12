//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "tiff.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "exif.h"
#include "icc.h"
#include "image.h"
#include "timer.h"

#include "fonts.h"
#include "imgui_ext.h"

using namespace std;

struct TIFFSaveOptions
{
    float            gain            = 1.f;
    TransferFunction tf              = TransferFunction::Linear;
    int              compression     = 1; // 0=None, 1=LZW, 2=ZIP, 3=PackBits
    int              data_type_index = 0; // 0=8bit, 1=16bit, 2=float
};

static TIFFSaveOptions s_opts;

#if !HDRVIEW_ENABLE_LIBTIFF

json get_tiff_info() { return {{"name", "libtiff"}}; }

bool is_tiff_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_tiff_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("TIFF support not enabled in this build.");
}

void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename, float gain, TransferFunction tf,
                     int compression, int data_type)
{
    throw runtime_error("TIFF support not enabled in this build.");
}

TIFFSaveOptions *tiff_parameters_gui() { return &s_opts; }

void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename, const TIFFSaveOptions *params)
{
    throw runtime_error("TIFF support not enabled in this build.");
}

#else

#include <cstring>
#include <half.h>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>
#include <tiffio.h>

json get_tiff_info()
{
    json j;
    j["enabled"] = true;
    j["name"]    = "libtiff";
#ifdef TIFFLIB_VERSION_STR_MAJ_MIN_MIC
    j["version"] = TIFFLIB_VERSION_STR_MAJ_MIN_MIC;
#else
    j["version"] = "";
#endif
    j["features"] = json::object();
    return j;
}

namespace
{

// Custom TIFF error and warning handlers
void error_handler(const char *module, const char *fmt, va_list args)
{
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    spdlog::error("TIFF error ({}): {}", module ? module : "unknown", buffer);
}

void warning_handler(const char *module, const char *fmt, va_list args)
{
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    spdlog::warn("TIFF warning ({}): {}", module ? module : "unknown", buffer);
}

// Custom TIFF I/O structure for reading from memory
struct TiffInput
{
    const uint8_t *data;
    toff_t         offset;
    tsize_t        size;

    TiffInput(const uint8_t *data, size_t size) : data(data), offset(0), size(size) {}

    static tsize_t read(thandle_t handle, tdata_t data, tsize_t size)
    {
        auto tiff = reinterpret_cast<TiffInput *>(handle);
        size      = std::min(size, tiff->size - (tsize_t)tiff->offset);
        memcpy(data, tiff->data + tiff->offset, size);
        tiff->offset += size;
        return size;
    }

    static tsize_t write(thandle_t, tdata_t, tsize_t) { return 0; }

    static toff_t seek(thandle_t handle, toff_t offset, int whence)
    {
        auto tiff = reinterpret_cast<TiffInput *>(handle);
        switch (whence)
        {
        case SEEK_SET: tiff->offset = offset; break;
        case SEEK_CUR: tiff->offset += offset; break;
        case SEEK_END: tiff->offset = tiff->size - offset; break;
        }
        return tiff->offset;
    }

    static int close(thandle_t) { return 0; }

    static toff_t get_size(thandle_t handle)
    {
        auto tiff = reinterpret_cast<TiffInput *>(handle);
        return tiff->size;
    }

    static int map(thandle_t handle, tdata_t *pdata, toff_t *psize)
    {
        auto tiff = reinterpret_cast<TiffInput *>(handle);
        *pdata    = (tdata_t)tiff->data;
        *psize    = tiff->size;
        return 1;
    }

    static void unmap(thandle_t, tdata_t, toff_t) {}
};

// Custom TIFF I/O for writing to ostream
struct TiffOutput
{
    ostream     *os;
    vector<char> buffer;

    explicit TiffOutput(ostream *os) : os(os) {}

    static tsize_t read(thandle_t handle, tdata_t data, tsize_t size) { return 0; }

    static tsize_t write(thandle_t handle, tdata_t data, tsize_t size)
    {
        auto tiff = reinterpret_cast<TiffOutput *>(handle);
        tiff->os->write(reinterpret_cast<const char *>(data), size);
        return tiff->os->good() ? size : 0;
    }

    static toff_t seek(thandle_t handle, toff_t offset, int whence)
    {
        auto tiff = reinterpret_cast<TiffOutput *>(handle);
        tiff->os->seekp(offset, whence == SEEK_SET ? ios::beg : (whence == SEEK_CUR ? ios::cur : ios::end));
        return tiff->os->tellp();
    }

    static int close(thandle_t) { return 0; }

    static toff_t get_size(thandle_t handle)
    {
        auto tiff = reinterpret_cast<TiffOutput *>(handle);
        auto pos  = tiff->os->tellp();
        tiff->os->seekp(0, ios::end);
        auto size = tiff->os->tellp();
        tiff->os->seekp(pos);
        return size;
    }
};

// Helper to check TIFF signature
// Returns 0: not a tiff image; 1: little endian tiff file; 2: big endian tiff file
int check_tiff_signature(istream &is)
{
    char sig[4];
    is.read(sig, 4);
    if (is.gcount() != 4)
        return 0;

    // Check for TIFF magic numbers (little-endian: II, big-endian: MM)
    bool is_le = (sig[0] == 'I' && sig[1] == 'I' && sig[2] == 42 && sig[3] == 0); // Little-endian
    bool is_be = (sig[0] == 'M' && sig[1] == 'M' && sig[2] == 0 && sig[3] == 42); // Big-endian

    is.seekg(0);

    if (is_le)
        return 1;
    else if (is_be)
        return 2;

    return 0;
}

inline void throw_if_error(int status, const string_view msg)
{
    if (status == 0)
        throw invalid_argument(fmt::format("Failed to read {}'. LibTiff error code {}'", msg, status));
}

vector<ImagePtr> load_image(TIFF *tif, bool reverse_endian, tdir_t dir, int sub_id, int sub_chain_id,
                            const ImageLoadOptions &opts)
{
    Timer timer;

    string partname =
        sub_id != -1 ? fmt::format("main.{}.sub.{}.{}", dir, sub_id, sub_chain_id) : fmt::format("main.{}", dir);

    vector<ImagePtr> images;
    try
    {
        uint32_t width, height;

        throw_if_error(TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width), "image width");
        throw_if_error(TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height), "image height");

        if (width == 0 || height == 0)
            throw runtime_error(fmt::format("Invalid TIFF dimensions: {}x{}", width, height));

        uint16_t samples_per_pixel, bits_per_sample, sample_format, photometric, planar_config, compression_type;
        throw_if_error(TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel), "samples per pixel");
        throw_if_error(TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample), "bits per sample");
        throw_if_error(TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sample_format), "sample format");
        throw_if_error(TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &photometric), "photometric tag");
        throw_if_error(TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar_config), "planar configuration");
        throw_if_error(TIFFGetFieldDefaulted(tif, TIFFTAG_COMPRESSION, &compression_type), "compression type");

        // Interpret untyped data as unsigned integer
        if (sample_format == SAMPLEFORMAT_VOID)
            sample_format = SAMPLEFORMAT_UINT;

        if (sample_format > SAMPLEFORMAT_IEEEFP)
            throw invalid_argument{fmt::format("Unsupported sample format: {}", sample_format)};

        // Handle LogLUV/LogL formats - configure for float output
        if (photometric == PHOTOMETRIC_LOGLUV || photometric == PHOTOMETRIC_LOGL)
        {
            spdlog::debug("Converting LogLUV/LogL to float.");
            if (compression_type == COMPRESSION_SGILOG || compression_type == COMPRESSION_SGILOG24)
            {
                TIFFSetField(tif, TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_FLOAT);
                bits_per_sample = 32;
                sample_format   = SAMPLEFORMAT_IEEEFP;
            }
        }

        // Handle PIXARLOG format
        if (compression_type == COMPRESSION_PIXARLOG)
        {
            spdlog::debug("Converting PIXAR log data to float.");
            TIFFSetField(tif, TIFFTAG_PIXARLOGDATAFMT, PIXARLOGDATAFMT_FLOAT);
            bits_per_sample = 32;
            sample_format   = SAMPLEFORMAT_IEEEFP;
        }

        const uint16_t file_bits_per_sample = bits_per_sample;
        if (compression_type == COMPRESSION_JPEG)
        {
            if (bits_per_sample <= 8)
                bits_per_sample = 8;
            else if (bits_per_sample <= 12)
                bits_per_sample = 12;
            else if (bits_per_sample <= 16)
                bits_per_sample = 16;

            TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bits_per_sample);

            if (photometric == PHOTOMETRIC_YCBCR)
            {
                spdlog::debug("Converting JPEG YCbCr to RGB.");
                photometric = PHOTOMETRIC_RGB;
            }

            if (!TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB))
                throw runtime_error{"Failed to set JPEG color mode."};
        }

        // Check if we need to use libtiff's RGBA interface for complex color spaces
        // This handles YCbCr, CMYK, and Lab conversions automatically
        bool use_rgba_interface = false;
        bool is_cmyk            = (photometric == PHOTOMETRIC_SEPARATED && samples_per_pixel == 4);
        bool is_lab             = (photometric == PHOTOMETRIC_CIELAB || photometric == PHOTOMETRIC_ICCLAB ||
                       photometric == PHOTOMETRIC_ITULAB);

        int num_channels = samples_per_pixel;

        if ((photometric == PHOTOMETRIC_YCBCR && compression_type != COMPRESSION_JPEG) || is_cmyk || is_lab)
        {
            const char *color_space = is_cmyk ? "CMYK" : (is_lab ? "Lab" : "YCbCr");
            spdlog::debug("Using RGBA interface for {} image", color_space);
            use_rgba_interface = true;
            // The RGBA interface will give us 8-bit RGBA data
            num_channels = 3; // Will be adjusted if alpha is detected
        }

        // Handle palette/indexed color
        // uint16_t *palette_r = nullptr, *palette_g = nullptr, *palette_b = nullptr;
        const uint16_t *palette[3] = {};
        bool            is_palette = (photometric == PHOTOMETRIC_PALETTE);
        if (is_palette)
        {
            if (num_channels != 1)
                throw runtime_error{"Palette images must have 1 color channel per pixel."};

            if (sample_format != SAMPLEFORMAT_UINT)
                throw runtime_error{"Palette images must have unsigned integer sample format."};

            if (!TIFFGetField(tif, TIFFTAG_COLORMAP, &palette[0], &palette[1], &palette[2]))
                throw runtime_error("PHOTOMETRIC_PALETTE specified but no color palette found");

            spdlog::debug("Found palette with {} entries", 1u << file_bits_per_sample);
            // For palette images, we'll convert to RGB (3 channels)
            num_channels = 3;
        }

        // Check for alpha channel information
        bool      has_alpha           = false;
        bool      is_premultiplied    = false;
        uint16_t  num_extra_samples   = 0;
        uint16_t *extra_samples_types = nullptr;

        if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &num_extra_samples, &extra_samples_types))
        {
            // Look for alpha channel in extra samples
            for (uint16_t i = 0; i < num_extra_samples; ++i)
            {
                if (extra_samples_types[i] == EXTRASAMPLE_ASSOCALPHA)
                {
                    has_alpha        = true;
                    is_premultiplied = true;
                    spdlog::debug("Found associated (premultiplied) alpha channel");
                    break;
                }
                else if (extra_samples_types[i] == EXTRASAMPLE_UNASSALPHA)
                {
                    has_alpha        = true;
                    is_premultiplied = false;
                    spdlog::debug("Found unassociated (straight) alpha channel");
                    break;
                }
            }
        }

        // If no EXTRASAMPLES tag, infer alpha presence from channel count
        if (!has_alpha && num_channels == 4)
        {
            has_alpha = true;
            // Default to straight alpha if not specified
            is_premultiplied = false;
            spdlog::debug("Inferred alpha channel from channel count (assuming straight alpha)");
        }

        auto image = make_shared<Image>(int2{(int)width, (int)height}, num_channels);
        // Track what type of alpha the file contained (not what we convert it to internally)
        image->alpha_type =
            has_alpha ? (is_premultiplied ? AlphaType_PremultipliedLinear : AlphaType_Straight) : AlphaType_None;
        image->metadata["loader"] = "libtiff";
        image->partname           = partname;

        // Format description
        string format_str;
        if (sample_format == SAMPLEFORMAT_IEEEFP)
            format_str = fmt::format("{}-bit float ({} bpc)", bits_per_sample * samples_per_pixel, bits_per_sample);
        else if (sample_format == SAMPLEFORMAT_INT)
            format_str =
                fmt::format("{}-bit signed int ({} bpc)", bits_per_sample * samples_per_pixel, bits_per_sample);
        else
            format_str =
                fmt::format("{}-bit unsigned int ({} bpc)", bits_per_sample * samples_per_pixel, bits_per_sample);

        image->metadata["pixel format"] = format_str;

        if (use_rgba_interface)
        {
            image->metadata["header"]["Converted via RGBA interface"] = {
                {"value", true},
                {"string", "Yes"},
                {"type", "bool"},
                {"description", "Image was converted to RGB using libtiff RGBA interface"}};
        }

        // Store palette info
        if (is_palette)
        {
            image->metadata["header"]["Color palette"] = {
                {"value", true},
                {"string", fmt::format("{} entries", 1u << file_bits_per_sample)},
                {"type", "bool"},
                {"description", "Image uses indexed color palette"}};
        }

        // Store alpha channel info
        if (has_alpha)
        {
            image->metadata["header"]["Alpha channel"] = {{"value", true},
                                                          {"string", is_premultiplied ? "Premultiplied" : "Straight"},
                                                          {"type", "bool"},
                                                          {"description", "Alpha channel type in file"}};
        }

        // Handle ICC profile
        uint32_t icc_profile_size = 0;
        void    *icc_profile_data = nullptr;
        if (TIFFGetField(tif, TIFFTAG_ICCPROFILE, &icc_profile_size, &icc_profile_data) && icc_profile_size > 0)
        {
            image->icc_data.resize(icc_profile_size);
            memcpy(image->icc_data.data(), icc_profile_data, icc_profile_size);
            image->metadata["header"]["ICC profile"] = {{"value", true},
                                                        {"string", fmt::format("{} bytes", icc_profile_size)},
                                                        {"type", "bool"},
                                                        {"description", "Embedded ICC color profile"}};
        }

        // Check for transfer function tag
        uint16_t *tf_r = nullptr, *tf_g = nullptr, *tf_b = nullptr;
        if (TIFFGetField(tif, TIFFTAG_TRANSFERFUNCTION, &tf_r, &tf_g, &tf_b) && tf_r)
        {
            image->metadata["header"]["Transfer function"] = {{"value", true},
                                                              {"string", "Present"},
                                                              {"type", "bool"},
                                                              {"description", "TIFF transfer function lookup table"}};
        }

        // Check for primaries
        float *primaries = nullptr;
        if (TIFFGetField(tif, TIFFTAG_PRIMARYCHROMATICITIES, &primaries) && primaries)
        {
            image->metadata["header"]["Primary chromaticities"] = {
                {"value", true},
                {"string", fmt::format("R:({:.4f},{:.4f}) G:({:.4f},{:.4f}) B:({:.4f},{:.4f})", primaries[0],
                                       primaries[1], primaries[2], primaries[3], primaries[4], primaries[5])},
                {"type", "bool"},
                {"description", "Custom RGB primary chromaticities"}};
        }

        // Check for white point
        float *whitePoint = nullptr;
        if (TIFFGetField(tif, TIFFTAG_WHITEPOINT, &whitePoint) && whitePoint)
        {
            image->metadata["header"]["White point"] = {
                {"value", true},
                {"string", fmt::format("({:.4f},{:.4f})", whitePoint[0], whitePoint[1])},
                {"type", "bool"},
                {"description", "Custom white point chromaticity"}};
        }

        // Read raw data for HDR support
        int3          size{(int)width, (int)height, num_channels};
        vector<float> float_pixels(product(size));

        // Handle YCbCr and other special formats using libtiff's RGBA interface
        if (use_rgba_interface)
        {
            spdlog::debug("Reading image using RGBA interface");

            // Allocate buffer for RGBA data (always ABGR format from libtiff)
            vector<uint32_t> rgba_buffer(width * height);

            // Read the entire image as RGBA (libtiff handles YCbCr->RGB conversion)
            if (!TIFFReadRGBAImageOriented(tif, width, height, rgba_buffer.data(), ORIENTATION_TOPLEFT, 0))
            {
                throw runtime_error("Failed to read TIFF image using RGBA interface");
            }

            // Convert from ABGR uint32 to float RGB(A)
            // TIFFReadRGBAImageOriented returns ABGR in native byte order
            int block_size = std::max(1, 1024 * 1024);
            parallel_for(blocked_range<int>(0, (int)(width * height), block_size),
                         [&rgba_buffer, &float_pixels, num_channels](int begin, int end, int, int)
                         {
                             for (int i = begin; i < end; ++i)
                             {
                                 uint32_t abgr = rgba_buffer[i];

                                 // Extract RGBA components (stored as ABGR in memory)
                                 uint8_t r = TIFFGetR(abgr);
                                 uint8_t g = TIFFGetG(abgr);
                                 uint8_t b = TIFFGetB(abgr);
                                 uint8_t a = TIFFGetA(abgr);

                                 // Convert to float 0-1 range
                                 size_t dst_idx            = i * num_channels;
                                 float_pixels[dst_idx + 0] = r / 255.0f;
                                 float_pixels[dst_idx + 1] = g / 255.0f;
                                 float_pixels[dst_idx + 2] = b / 255.0f;
                                 if (num_channels == 4)
                                     float_pixels[dst_idx + 3] = a / 255.0f;
                             }
                         });
        }
        else
        {
            // Pre-compute bias and inverse divisor for integer formats based on file bit depth
            const float int_inv_divisor = 1.0f / (float)((1ull << file_bits_per_sample) - 1);
            const float int_bias =
                sample_format == SAMPLEFORMAT_INT ? (float)(1ull << (file_bits_per_sample - 1)) : 0.0f;

            // Helper function to unpack bits (handles both byte-aligned and bit-packed data)
            auto unpack_bits =
                [&](const uint8_t *input, size_t input_size, int bitwidth, vector<uint32_t> &output, bool handle_sign)
            {
                // If the bitwidth is byte aligned (multiple of 8), data is already in machine endianness
                if (bitwidth % 8 == 0)
                {
                    const uint32_t bytes_per_sample = bitwidth / 8;
                    for (size_t i = 0; i < output.size(); ++i)
                    {
                        output[i] = 0;
                        for (uint32_t j = 0; j < bytes_per_sample; ++j)
                        {
                            if (is_little_endian())
                                output[i] |= (uint32_t)input[i * bytes_per_sample + j] << (8 * j);
                            else
                                output[i] |= (uint32_t)input[i * bytes_per_sample + j]
                                             << ((sizeof(uint32_t) * 8 - 8) - 8 * j);
                        }

                        // If signbit is set, set all bits to the left to 1
                        if (handle_sign && (output[i] & (1u << (bitwidth - 1))))
                            output[i] |= ~((1u << bitwidth) - 1);
                    }
                    return;
                }

                // Otherwise, data is packed bitwise, MSB first / big endian
                uint64_t current_bits   = 0;
                int      bits_available = 0;
                size_t   i              = 0;

                for (size_t j = 0; j < input_size; ++j)
                {
                    current_bits = (current_bits << 8) | input[j];
                    bits_available += 8;

                    while (bits_available >= bitwidth && i < output.size())
                    {
                        bits_available -= bitwidth;
                        output[i] = (current_bits >> bits_available) & ((1u << bitwidth) - 1);

                        // If signbit is set, set all bits to the left to 1
                        if (handle_sign && (output[i] & (1u << (bitwidth - 1))))
                            output[i] |= ~((1u << bitwidth) - 1);

                        ++i;
                    }
                }
            };

            // Helper function to convert unpacked integer or raw float data to float
            auto convert_to_float = [&](const uint8_t *buffer, const uint32_t *unpacked, size_t buffer_idx) -> float
            {
                if (unpacked)
                {
                    // Handle integer data (already unpacked - both byte-aligned and bit-packed)
                    // Works for both UINT (bias=0) and INT (bias=2^(n-1))
                    return ((float)unpacked[buffer_idx] + int_bias) * int_inv_divisor;
                }
                else // SAMPLEFORMAT_IEEEFP
                {
                    // Handle floating-point data directly from buffer
                    if (bits_per_sample == 32)
                        return reinterpret_cast<const float *>(buffer)[buffer_idx];
                    else if (bits_per_sample == 16)
                        return (float)reinterpret_cast<const half *>(buffer)[buffer_idx];
                    else if (bits_per_sample == 64)
                        return (float)reinterpret_cast<const double *>(buffer)[buffer_idx];
                }

                return 0.f;
            };

            // Treat scanlines as tiles with width = image width. This unifies the code path.
            // Use TIFFReadEncodedTile for tiled images and TIFFReadEncodedStrip for scanline-based images.
            const bool      is_tiled = TIFFIsTiled(tif);
            uint32_t        tile_width, tile_height, num_tiles_x, num_tiles_y;
            uint64_t        tile_size, tile_row_size, strip_height, num_strips;
            vector<uint8_t> tile_buffer;

            // Function pointer to read tile/strip data - both have the same signature
            auto read_data = is_tiled ? TIFFReadEncodedTile : TIFFReadEncodedStrip;

            if (is_tiled)
            {
                if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width) ||
                    !TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height))
                    throw runtime_error("Failed to read tile dimensions");

                tile_size     = TIFFTileSize64(tif);
                tile_row_size = TIFFTileRowSize64(tif);
                num_tiles_x   = (width + tile_width - 1) / tile_width;
                num_tiles_y   = (height + tile_height - 1) / tile_height;
            }
            else
            {
                // Strips are just tiles with the same width as the image
                tile_width    = width;
                tile_size     = TIFFStripSize64(tif);
                tile_row_size = TIFFScanlineSize64(tif);
                num_tiles_x   = 1;

                // Get rows per strip to compute tile_height
                uint32_t rows_per_strip = 0;
                TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);
                // Protect against invalid values (0 or excessively large)
                if (rows_per_strip == 0 || rows_per_strip > height)
                    rows_per_strip = height;
                tile_height  = rows_per_strip;
                num_tiles_y  = (height + tile_height - 1) / tile_height;
                num_strips   = num_tiles_y;
                strip_height = tile_height;
            }

            // Ensure we allocate enough for potentially oversized tiles/strips (handles edge cases)
            tile_size = std::max(tile_size, (uint64_t)tile_width * tile_height * file_bits_per_sample *
                                                (is_palette ? 1 : (is_cmyk ? 4 : (is_lab ? 3 : num_channels))) / 8);
            tile_buffer.resize(tile_size);

            // Always unpack integer formats (unpack_bits handles both byte-aligned and bit-packed efficiently)
            const bool       needs_unpacking = sample_format != SAMPLEFORMAT_IEEEFP;
            vector<uint32_t> unpacked_buffer;

            // Store tile/strip information in metadata
            image->metadata["header"]["Pixel organization"] = {
                {"value",
                 {is_tiled, num_tiles_x, is_tiled ? num_tiles_y : num_strips, tile_width,
                  is_tiled ? tile_height : strip_height}},
                {"string", is_tiled ? fmt::format("{}{}{} grid of ({}{}{}) tiles", num_tiles_x, ICON_MY_TIMES,
                                                  num_tiles_y, tile_width, ICON_MY_TIMES, tile_height)
                                    : fmt::format("{} strips of height {}", num_strips, strip_height)},
                {"type", "array"},
                {"description", "TIFF pixel organization: tiled or strip-based"}};

            if (planar_config == PLANARCONFIG_CONTIG)
            {
                // Interleaved/contiguous data
                for (uint32_t tile_y = 0; tile_y < num_tiles_y; ++tile_y)
                {
                    for (uint32_t tile_x = 0; tile_x < num_tiles_x; ++tile_x)
                    {
                        // Calculate tile/strip index for reading
                        uint32_t tile_index = is_tiled ? (tile_y * num_tiles_x + tile_x) : tile_y;

                        // Read tile or strip using the unified function pointer
                        if (read_data(tif, tile_index, tile_buffer.data(), tile_size) < 0)
                            throw runtime_error(fmt::format(
                                "Failed to read {} {} (index {})", is_tiled ? "tile" : "strip",
                                is_tiled ? fmt::format("({}, {})", tile_x, tile_y) : fmt::format("{}", tile_y),
                                tile_index));

                        // Process pixels in this tile/strip
                        // For sub-byte bit depths, we must unpack row-by-row because bit packing is done per scanline
                        const size_t samples_per_row =
                            tile_width * (is_palette ? 1 : (is_cmyk ? 4 : (is_lab ? 3 : num_channels)));
                        if (needs_unpacking)
                            unpacked_buffer.resize(samples_per_row);

                        for (uint32_t ty = 0; ty < tile_height; ++ty)
                        {
                            uint32_t y = tile_y * tile_height + ty;
                            if (y >= height)
                                break;

                            // Unpack this row if necessary
                            const uint8_t *source_buffer = tile_buffer.data() + ty * tile_row_size;
                            if (needs_unpacking)
                            {
                                unpack_bits(source_buffer, tile_row_size, file_bits_per_sample, unpacked_buffer,
                                            sample_format == SAMPLEFORMAT_INT);
                            }

                            for (uint32_t tx = 0; tx < tile_width; ++tx)
                            {
                                uint32_t x = tile_x * tile_width + tx;
                                if (x >= width)
                                    break;

                                if (is_palette)
                                {
                                    // Palette/indexed color: read index and look up RGB values
                                    // ColorMap values are display-referred and will be linearized later
                                    size_t   buffer_idx = tx; // Row-relative index
                                    uint32_t index      = needs_unpacking ? unpacked_buffer[buffer_idx]
                                                                          : (uint32_t)convert_to_float(source_buffer,
                                                                                                       nullptr, buffer_idx) *
                                                                           ((1u << file_bits_per_sample) - 1);

                                    // Store normalized palette values (0-1 range)
                                    size_t pixel_idx            = (y * width + x) * num_channels;
                                    float_pixels[pixel_idx + 0] = palette[0][index] / 65535.0f;
                                    float_pixels[pixel_idx + 1] = palette[1][index] / 65535.0f;
                                    float_pixels[pixel_idx + 2] = palette[2][index] / 65535.0f;
                                }
                                else
                                {
                                    // Normal color data
                                    for (int c = 0; c < num_channels; ++c)
                                    {
                                        size_t buffer_idx = tx * num_channels + c; // Row-relative index
                                        size_t pixel_idx  = (y * width + x) * num_channels + c;
                                        float_pixels[pixel_idx] =
                                            convert_to_float(source_buffer, unpacked_buffer.data(), buffer_idx);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                // Planar/separate data - read each channel separately
                // Note: Palette images are not typically stored in planar format
                // (CMYK and Lab use RGBA interface, so won't reach here)
                if (is_palette)
                    throw runtime_error("Planar configuration not supported for palette images");

                for (int c = 0; c < num_channels; ++c)
                {
                    for (uint32_t tile_y = 0; tile_y < num_tiles_y; ++tile_y)
                    {
                        for (uint32_t tile_x = 0; tile_x < num_tiles_x; ++tile_x)
                        {
                            // Calculate tile/strip index for reading (including plane offset for separate config)
                            uint32_t tile_index;
                            if (is_tiled)
                            {
                                // For tiled: index = plane_offset + tile_y * num_tiles_x + tile_x
                                uint32_t tiles_per_plane = num_tiles_x * num_tiles_y;
                                tile_index               = c * tiles_per_plane + tile_y * num_tiles_x + tile_x;
                            }
                            else
                            {
                                // For strips: index = plane_offset + strip_number
                                tile_index = c * num_tiles_y + tile_y;
                            }

                            // Read tile or strip using the unified function pointer
                            if (read_data(tif, tile_index, tile_buffer.data(), tile_size) < 0)
                                throw runtime_error(fmt::format(
                                    "Failed to read {} {} for channel {} (index {})", is_tiled ? "tile" : "strip",
                                    is_tiled ? fmt::format("({}, {})", tile_x, tile_y) : fmt::format("{}", tile_y), c,
                                    tile_index));

                            // Process pixels in this tile/strip
                            // For sub-byte bit depths, we must unpack row-by-row
                            if (needs_unpacking)
                                unpacked_buffer.resize(tile_width);

                            for (uint32_t ty = 0; ty < tile_height; ++ty)
                            {
                                uint32_t y = tile_y * tile_height + ty;
                                if (y >= height)
                                    break;

                                // Unpack this row if necessary
                                const uint8_t *source_buffer = tile_buffer.data() + ty * tile_row_size;
                                if (needs_unpacking)
                                {
                                    unpack_bits(source_buffer, tile_row_size, file_bits_per_sample, unpacked_buffer,
                                                sample_format == SAMPLEFORMAT_INT);
                                }

                                for (uint32_t tx = 0; tx < tile_width; ++tx)
                                {
                                    uint32_t x = tile_x * tile_width + tx;
                                    if (x >= width)
                                        break;

                                    size_t buffer_idx = tx; // Row-relative index
                                    size_t pixel_idx  = (y * width + x) * num_channels + c;
                                    float_pixels[pixel_idx] =
                                        convert_to_float(source_buffer, unpacked_buffer.data(), buffer_idx);
                                }
                            }
                        }
                    }
                }
            }
        } // end else (normal tile/strip reading)

        // Handle PHOTOMETRIC_MINISWHITE: invert grayscale values (0=white, max=black)
        if (photometric == PHOTOMETRIC_MINISWHITE)
        {
            spdlog::debug("Inverting pixel values for PHOTOMETRIC_MINISWHITE");
            for (size_t i = 0; i < float_pixels.size(); ++i) float_pixels[i] = 1.0f - float_pixels[i];
        }

        // Apply color space conversions with proper priority:
        // 1. override_profile (user's explicit override)
        // 2. ICC profile
        // 3. TRANSFERFUNCTION + PRIMARYCHROMATICITIES + WHITEPOINT tags
        // 4. Defaults
        string         profile_desc;
        Chromaticities c;

        if (opts.override_profile)
        {
            // Priority 1: User override (highest priority) - use gamut_override and tf_override
            spdlog::debug("Using user-specified color space override");

            // Use the user-specified gamut, not file metadata
            Chromaticities file_chr = gamut_chromaticities(opts.gamut_override);

            if (linearize_pixels(float_pixels.data(), size, file_chr, opts.tf_override, opts.keep_primaries,
                                 &profile_desc, &c))
                image->chromaticities = c;
        }
        else if (!image->icc_data.empty())
        {
            // Priority 2: ICC profile
            if (ICCProfile(image->icc_data)
                    .linearize_pixels(float_pixels.data(), size, opts.keep_primaries, &profile_desc, &c))
            {
                spdlog::info("Linearizing colors using ICC profile.");
                image->chromaticities = c;
            }
        }
        else
        {
            // Priority 3: TRANSFERFUNCTION tag + chromaticities
            uint16_t *tf_r = nullptr, *tf_g = nullptr, *tf_b = nullptr;
            bool      has_transfer_function = TIFFGetField(tif, TIFFTAG_TRANSFERFUNCTION, &tf_r, &tf_g, &tf_b);

            if (has_transfer_function && tf_r)
            {
                spdlog::debug("Applying TRANSFERFUNCTION tag for linearization");
                // Apply the transfer function LUT to linearize the pixels
                int block_size = std::max(1, 1024 * 1024 / num_channels);
                parallel_for(blocked_range<int>(0, (int)(width * height), block_size),
                             [&float_pixels, num_channels, tf_r, tf_g, tf_b](int begin, int end, int, int)
                             {
                                 for (int i = begin * num_channels; i < end * num_channels; ++i)
                                 {
                                     // Map float value (0-1) to 16-bit index, look up in LUT, convert back to float
                                     uint16_t index =
                                         (uint16_t)(std::min(std::max(float_pixels[i], 0.0f), 1.0f) * 65535.0f);
                                     // Determine which transfer function to use based on channel
                                     int       c  = i % num_channels;
                                     uint16_t *tf = (c == 0) ? tf_r : ((c == 1 && tf_g) ? tf_g : (tf_b ? tf_b : tf_r));
                                     float_pixels[i] = tf[index] / 65535.0f;
                                 }
                             });
                profile_desc = "TIFF TransferFunction";

                // Still apply chromaticities if present
                Chromaticities file_chr;
                if (primaries)
                {
                    file_chr.red   = {primaries[0], primaries[1]};
                    file_chr.green = {primaries[2], primaries[3]};
                    file_chr.blue  = {primaries[4], primaries[5]};
                }

                if (whitePoint)
                    file_chr.white = {whitePoint[0], whitePoint[1]};

                // Apply color space conversion if chromaticities were found
                if (!opts.keep_primaries && (file_chr.red.x != 0.f || file_chr.white.x != 0.f))
                {
                    // Pixels are already linear from TRANSFERFUNCTION, just convert color space
                    if (linearize_pixels(float_pixels.data(), size, file_chr, TransferFunction::Linear,
                                         opts.keep_primaries, &profile_desc, &c))
                        image->chromaticities = c;
                }
            }
            else
            {
                // Priority 4: Defaults based on photometric interpretation and sample format
                TransferFunction tf{TransferFunction::Unspecified};
                if (sample_format == SAMPLEFORMAT_IEEEFP)
                    tf = TransferFunction::Linear;
                // else if (photometric == PHOTOMETRIC_MINISBLACK || photometric == PHOTOMETRIC_MINISWHITE)
                //     tf = TransferFunction::Linear; // TIFF spec: grayscale is typically linear
                else
                    tf = TransferFunction::Gamma; // RGB and palette default to gamma 2.2

                Chromaticities file_chr;
                if (photometric == PHOTOMETRIC_LOGLUV || photometric == PHOTOMETRIC_LOGL)
                    file_chr = {{1.f, 0.f}, {0.f, 1.f}, {0.f, 0.f}, {1.f / 3.f, 1.f / 3.f}};

                if (primaries)
                {
                    spdlog::debug("Found custom primaries; applying...");
                    file_chr.red   = {primaries[0], primaries[1]};
                    file_chr.green = {primaries[2], primaries[3]};
                    file_chr.blue  = {primaries[4], primaries[5]};
                }

                if (whitePoint)
                    file_chr.white = {whitePoint[0], whitePoint[1]};

                if (linearize_pixels(float_pixels.data(), size, file_chr, tf, opts.keep_primaries, &profile_desc, &c))
                    image->chromaticities = c;
            }
        }

        image->metadata["color profile"] = profile_desc;

        // Convert straight alpha to premultiplied if needed (HDRView uses premultiplied alpha pipeline)
        // Note: image->alpha_type tracks what the file contained, not our internal representation
        if (has_alpha && !is_premultiplied && num_channels == 4)
        {
            spdlog::debug("Converting straight alpha to premultiplied");
            int block_size = std::max(1, 1024 * 1024);
            parallel_for(blocked_range<int>(0, (int)(width * height), block_size),
                         [&float_pixels](int begin, int end, int, int)
                         {
                             for (int i = begin; i < end; ++i)
                             {
                                 size_t pixel_idx = i * 4;
                                 float  alpha     = float_pixels[pixel_idx + 3];

                                 // Premultiply RGB channels by alpha
                                 float_pixels[pixel_idx + 0] *= alpha;
                                 float_pixels[pixel_idx + 1] *= alpha;
                                 float_pixels[pixel_idx + 2] *= alpha;
                             }
                         });
        }

        // Copy processed pixels to image channels
        for (int c = 0; c < num_channels; ++c)
            image->channels[c].copy_from_interleaved(float_pixels.data(), size.x, size.y, size.z, c,
                                                     [](float v) { return v; });

        spdlog::debug("Loaded TIFF sub-image ({}x{}, {} channels) in {:.2f}ms", width, height, num_channels,
                      timer.elapsed());

        // add the image
        images.push_back(image);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("Failed to load {}: {}; skipping...", partname, e.what());
    }

    return images;
}

/// Extract EXIF data from TIFF file as a blob suitable for libexif parsing.
/// Returns a vector with "Exif\0\0" header followed by minimal TIFF structure.
/// Returns empty vector if extraction fails or is not possible.
vector<uint8_t> extract_tiff_exif_blob(const vector<uint8_t> &data, bool reverse_endian)
{
    if (data.size() < 8)
        return {};

    try
    {
        // Determine TIFF endianness
        Endian   tiff_endian      = reverse_endian ? (is_little_endian() ? Endian::Big : Endian::Little)
                                                   : (is_little_endian() ? Endian::Little : Endian::Big);
        uint32_t first_ifd_offset = read_as<uint32_t>(&data[4], tiff_endian);

        spdlog::debug("First IFD offset: {} (0x{:08X})", first_ifd_offset, first_ifd_offset);

        if (first_ifd_offset <= 8 || first_ifd_offset >= data.size())
            return {};

        // Construct a minimal TIFF blob: header + IFD + trailing data
        // We include extra data after the IFD to capture offset-referenced values
        const size_t ifd_buffer_size = 256 * 1024; // 256KB should cover most metadata
        size_t       blob_data_size  = std::min(ifd_buffer_size, data.size() - first_ifd_offset);
        size_t       blob_size       = 8 + blob_data_size; // header + IFD data

        vector<uint8_t> tiff_blob(blob_size);

        // Copy TIFF header (8 bytes)
        memcpy(tiff_blob.data(), data.data(), 8);

        // Modify the IFD offset to point right after the header (offset 8)
        write_as<uint32_t>(tiff_blob.data() + 4, 8, tiff_endian);

        // Copy IFD and trailing data
        memcpy(tiff_blob.data() + 8, data.data() + first_ifd_offset, blob_data_size);

        // Now we need to adjust any offsets in the IFD entries
        // IFD structure: 2-byte count, then 12-byte entries
        uint16_t num_entries = read_as<uint16_t>(tiff_blob.data() + 8, tiff_endian);
        spdlog::debug("IFD has {} entries", num_entries);

        // Adjust offsets in IFD entries (subtract the old IFD offset, add 8)
        int32_t offset_delta = 8 - (int32_t)first_ifd_offset;

        for (uint16_t i = 0; i < num_entries && i < 1000; ++i) // sanity limit
        {
            size_t   entry_offset = 8 + 2 + (i * 12); // header + count + entry
            uint16_t tag          = read_as<uint16_t>(tiff_blob.data() + entry_offset + 0, tiff_endian);
            uint16_t type         = read_as<uint16_t>(tiff_blob.data() + entry_offset + 2, tiff_endian);
            uint32_t count        = read_as<uint32_t>(tiff_blob.data() + entry_offset + 4, tiff_endian);
            uint32_t value_offset = read_as<uint32_t>(tiff_blob.data() + entry_offset + 8, tiff_endian);

            // Determine if this is an offset or inline value
            // Values > 4 bytes are stored as offsets
            size_t type_size = 0;
            switch (type)
            {
            case 1: type_size = 1; break;  // BYTE
            case 2: type_size = 1; break;  // ASCII
            case 3: type_size = 2; break;  // SHORT
            case 4: type_size = 4; break;  // LONG
            case 5: type_size = 8; break;  // RATIONAL
            case 6: type_size = 1; break;  // SBYTE
            case 7: type_size = 1; break;  // UNDEFINED
            case 8: type_size = 2; break;  // SSHORT
            case 9: type_size = 4; break;  // SLONG
            case 10: type_size = 8; break; // SRATIONAL
            case 11: type_size = 4; break; // FLOAT
            case 12: type_size = 8; break; // DOUBLE
            default: type_size = 1; break;
            }

            size_t value_size = type_size * count;

            // If value doesn't fit inline (> 4 bytes), it's an offset that needs adjusting
            if (value_size > 4 && value_offset >= first_ifd_offset && value_offset < first_ifd_offset + blob_data_size)
            {
                uint32_t new_offset = value_offset + offset_delta;
                write_as<uint32_t>(tiff_blob.data() + entry_offset + 8, new_offset, tiff_endian);
                spdlog::debug("Adjusted tag 0x{:04X} offset from {} to {}", tag, value_offset, new_offset);
            }
        }

        // Adjust the "next IFD" offset at the end of the IFD
        size_t next_ifd_offset_pos = 8 + 2 + (num_entries * 12);
        if (next_ifd_offset_pos + 4 <= tiff_blob.size())
        {
            uint32_t next_ifd = read_as<uint32_t>(tiff_blob.data() + next_ifd_offset_pos, tiff_endian);
            if (next_ifd > 0 && next_ifd >= first_ifd_offset && next_ifd < first_ifd_offset + blob_data_size)
            {
                write_as<uint32_t>(tiff_blob.data() + next_ifd_offset_pos, next_ifd + offset_delta, tiff_endian);
                spdlog::debug("Adjusted next IFD offset from {} to {}", next_ifd, next_ifd + offset_delta);
            }
            else if (next_ifd > 0)
            {
                // Next IFD is outside our buffer, set to 0 (no next IFD)
                write_as<uint32_t>(tiff_blob.data() + next_ifd_offset_pos, 0, tiff_endian);
                spdlog::debug("Set next IFD offset to 0 (was {})", next_ifd);
            }
        }

        // Prepend "Exif\0\0" header if not already present
        if (tiff_blob.size() < 6 || memcmp(tiff_blob.data(), "Exif\0\0", 6) != 0)
        {
            vector<uint8_t> exif_blob(6 + tiff_blob.size());
            exif_blob[0] = 'E';
            exif_blob[1] = 'x';
            exif_blob[2] = 'i';
            exif_blob[3] = 'f';
            exif_blob[4] = 0;
            exif_blob[5] = 0;
            memcpy(exif_blob.data() + 6, tiff_blob.data(), tiff_blob.size());
            return exif_blob;
        }

        return tiff_blob;
    }
    catch (const std::exception &e)
    {
        spdlog::debug("Failed to extract EXIF blob: {}", e.what());
        return {};
    }
}

vector<ImagePtr> load_sub_images(TIFF *tif, bool reverse_endian, tdir_t dir, const ImageLoadOptions &opts)
{
    vector<ImagePtr> images;

    // Check if there are SubIFD subfiles
    toff_t *offsets;
    int     num_sub_IFDs = 0;
    if (TIFFGetField(tif, TIFFTAG_SUBIFD, &num_sub_IFDs, &offsets))
    {
        // Make a copy of the offsets, as they are only valid until the next TIFFReadDirectory() call
        vector<toff_t> sub_IFD_offsets(offsets, offsets + num_sub_IFDs);
        for (int i = 0; i < num_sub_IFDs; i++)
        {
            // Read first SubIFD directory
            if (!TIFFSetSubDirectory(tif, sub_IFD_offsets[i]))
                throw invalid_argument{"Failed to read sub IFD."};

            int j = 0;
            do {
                auto sub_images = load_image(tif, reverse_endian, dir, i, j, opts);
                for (auto sub_image : sub_images) images.push_back(sub_image);
                ++j;
            } while (TIFFReadDirectory(tif));
        }

        // Go back to main-IFD chain and re-read that main-IFD directory
        if (!TIFFSetDirectory(tif, dir))
            spdlog::warn("Failed to re-read the main IFD directory.");
    }

    return images;
}

} // namespace

bool is_tiff_image(istream &is) noexcept
{
    bool ret = false;
    try
    {
        auto start = is.tellg();
        ret        = check_tiff_signature(is) != 0;
        is.clear();
        is.seekg(start);
    }
    catch (...)
    {
    }
    return ret;
}

vector<ImagePtr> load_tiff_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "TIFF"};

    bool reverse_endian = false;
    if (auto e = check_tiff_signature(is); e != 0)
    {
        bool le        = is_little_endian();
        reverse_endian = (e == 2 && le) || (e == 1 && !le);
    }
    else
        throw runtime_error("Not a valid TIFF file.");

    // Set custom error/warning handlers
    TIFFSetErrorHandler(error_handler);
    TIFFSetWarningHandler(warning_handler);

    // Read entire file into memory
    is.clear();
    is.seekg(0, ios::end);
    size_t file_size = static_cast<size_t>(is.tellg());
    is.seekg(0, ios::beg);

    if (file_size == 0)
        throw runtime_error("Empty TIFF file.");

    vector<uint8_t> data(file_size);
    is.read(reinterpret_cast<char *>(data.data()), file_size);
    if (static_cast<size_t>(is.gcount()) != file_size)
        throw runtime_error("Failed to read TIFF file completely.");

    TiffInput tiff_data(data.data(), file_size);
    TIFF     *tif = TIFFClientOpen(string(filename).c_str(), "rMc", reinterpret_cast<thandle_t>(&tiff_data),
                                   TiffInput::read, TiffInput::write, TiffInput::seek, TiffInput::close,
                                   TiffInput::get_size, TiffInput::map, TiffInput::unmap);

    if (!tif)
        throw runtime_error("Failed to open TIFF file.");

    auto tif_guard = ScopeGuard{[tif] { TIFFClose(tif); }};

    // Extract EXIF/TIFF metadata using libexif
    Exif            exif;
    json            exif_json;
    vector<uint8_t> exif_blob = extract_tiff_exif_blob(data, reverse_endian);
    if (!exif_blob.empty())
    {
        spdlog::debug("Found EXIF data of size {} bytes", exif_blob.size());

        try
        {
            exif      = Exif{exif_blob};
            exif_json = exif.to_json();
            if (!exif_json.empty())
                spdlog::debug("TIFF/EXIF metadata successfully parsed");
            else
                spdlog::debug("EXIF blob extracted but parsing returned empty result");
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Exception while parsing EXIF data: {}", e.what());
            exif.reset();
        }
    }

    // Extract XMP metadata (usually in first IFD)
    vector<uint8_t> xmp_blob;
    uint32_t        xmp_size = 0;
    void           *xmp_data = nullptr;
    if (TIFFGetField(tif, TIFFTAG_XMLPACKET, &xmp_size, &xmp_data) && xmp_size > 0)
    {
        xmp_blob.resize(xmp_size);
        memcpy(xmp_blob.data(), xmp_data, xmp_size);
        spdlog::debug("Found XMP metadata of size {} bytes", xmp_size);
    }

    vector<ImagePtr> images;

    // TIFF files can contain multiple directories (sub-images)
    do {

        tdir_t dir = TIFFCurrentDirectory(tif);

        auto added_images = load_image(tif, reverse_endian, dir, -1, -1, opts);
        for (auto image : added_images)
        {
            image->filename = filename;
            image->xmp_data = xmp_blob;

            // Use pre-parsed EXIF data
            if (exif.valid())
            {
                image->exif             = exif;
                image->metadata["exif"] = exif_json;
            }
            images.push_back(image);
        }

        auto sub_images = load_sub_images(tif, reverse_endian, dir, opts);
        for (auto sub_image : sub_images)
        {
            sub_image->filename = filename;
            sub_image->xmp_data = xmp_blob;

            // Use pre-parsed EXIF data
            if (exif.valid())
            {
                sub_image->exif             = exif;
                sub_image->metadata["exif"] = exif_json;
            }
            images.push_back(sub_image);
        }

    } while (TIFFReadDirectory(tif));

    if (images.size() == 1)
        // no need for partname
        images.front()->partname = "";

    return images;
}

void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename, const TIFFSaveOptions *opts)
{
    if (!opts)
        throw invalid_argument("TIFFSaveOptions is required.");

    ScopedMDC mdc{"IO", "TIFF"};
    Timer     timer;

    // Set custom error/warning handlers
    TIFFSetErrorHandler(error_handler);
    TIFFSetWarningHandler(warning_handler);

    TiffOutput write_data(&os);
    TIFF      *tif =
        TIFFClientOpen(string(filename).c_str(), "wm", reinterpret_cast<thandle_t>(&write_data), TiffOutput::read,
                       TiffOutput::write, TiffOutput::seek, TiffOutput::close, TiffOutput::get_size, nullptr, nullptr);

    if (!tif)
        throw runtime_error("Failed to create TIFF file for writing.");

    auto tif_guard = ScopeGuard{[tif] { TIFFClose(tif); }};

    int w = img.size().x;
    int h = img.size().y;
    int n = img.groups[img.selected_group].num_channels;

    // Set basic tags
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, n);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

    // Set photometric interpretation
    if (n == 1)
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    else
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);

    // Set compression
    uint16_t compression = COMPRESSION_NONE;
    switch (opts->compression)
    {
    case 0: compression = COMPRESSION_NONE; break;
    case 1: compression = COMPRESSION_LZW; break;
    case 2: compression = COMPRESSION_DEFLATE; break;
    case 3: compression = COMPRESSION_PACKBITS; break;
    }
    TIFFSetField(tif, TIFFTAG_COMPRESSION, compression);

    // Set data type and write pixels
    if (opts->data_type_index == 2) // Float
    {
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);

        int  w_out, h_out, n_out;
        auto pixels = img.as_interleaved<float>(&w_out, &h_out, &n_out, opts->gain, opts->tf, false, false, false);

        for (int y = 0; y < h; ++y)
        {
            if (TIFFWriteScanline(tif, pixels.get() + y * w * n, y, 0) < 0)
                throw runtime_error("Failed to write TIFF scanline.");
        }
    }
    else if (opts->data_type_index == 1) // 16-bit
    {
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

        int  w_out, h_out, n_out;
        auto pixels = img.as_interleaved<uint16_t>(&w_out, &h_out, &n_out, opts->gain, opts->tf, true, false, false);

        for (int y = 0; y < h; ++y)
        {
            if (TIFFWriteScanline(tif, pixels.get() + y * w * n, y, 0) < 0)
                throw runtime_error("Failed to write TIFF scanline.");
        }
    }
    else // 8-bit
    {
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

        int  w_out, h_out, n_out;
        auto pixels = img.as_interleaved<uint8_t>(&w_out, &h_out, &n_out, opts->gain, opts->tf, true, false, false);

        for (int y = 0; y < h; ++y)
        {
            if (TIFFWriteScanline(tif, pixels.get() + y * w * n, y, 0) < 0)
                throw runtime_error("Failed to write TIFF scanline.");
        }
    }

    // Write metadata
    TIFFSetField(tif, TIFFTAG_SOFTWARE, "HDRView");
    if (!img.metadata.empty() && img.metadata.contains("description"))
    {
        string desc = img.metadata["description"].get<string>();
        TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, desc.c_str());
    }

    spdlog::debug("Saved TIFF image ({}x{}, {} channels, {}-bit) in {:.2f}ms", w, h, n,
                  opts->data_type_index == 2 ? 32 : (opts->data_type_index == 1 ? 16 : 8), timer.elapsed());
}

void save_tiff_image(const Image &img, ostream &os, string_view filename, float gain, TransferFunction tf,
                     int compression, int data_type)
{
    TIFFSaveOptions opts;
    opts.gain            = gain;
    opts.tf              = tf;
    opts.compression     = compression;
    opts.data_type_index = data_type;
    save_tiff_image(img, os, filename, &opts);
}

TIFFSaveOptions *tiff_parameters_gui()
{
    if (ImGui::PE::Begin("TIFF Save Options", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
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
                auto changed =
                    ImGui::SliderFloat("##Gain", &s_opts.gain, 0.001f, 100.f, "%.3f", ImGuiSliderFlags_Logarithmic);
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
            "Apply this transfer function to RGB channels when encoding.");

        if (s_opts.tf.type == TransferFunction::Gamma)
            ImGui::PE::SliderFloat("Gamma", &s_opts.tf.gamma, 0.1f, 5.f, "%.3f", 0,
                                   "When using a gamma transfer function, this is the gamma value to use.");

        ImGui::PE::Entry(
            "Compression",
            [&]
            {
                const char *compression_items[] = {"None", "LZW", "ZIP (Deflate)", "PackBits"};
                return ImGui::Combo("##Compression", &s_opts.compression, compression_items,
                                    IM_ARRAYSIZE(compression_items));
            },
            "Compression method for the TIFF file.");

        ImGui::PE::Entry(
            "Data type",
            [&]
            {
                const char *data_type_items[] = {"8-bit", "16-bit", "32-bit float"};
                return ImGui::Combo("##DataType", &s_opts.data_type_index, data_type_items,
                                    IM_ARRAYSIZE(data_type_items));
            },
            "Bit depth for pixel values.");

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = TIFFSaveOptions{};

    return &s_opts;
}

#endif
