#include "png.h"
#include "colorspace.h"
#include "icc.h"
#include "image.h"
#include "texture.h"
#include "timer.h"

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

struct PngReadStream
{
    istream    *is;
    static void read(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead)
    {
        auto *stream = static_cast<PngReadStream *>(png_get_io_ptr(png_ptr));
        stream->is->read(reinterpret_cast<char *>(outBytes), byteCountToRead);
        if (stream->is->gcount() != (streamsize)byteCountToRead)
            png_error(png_ptr, "Read error in PNG stream");
    }
};

struct PngWriteStream
{
    ostream    *os;
    static void write(png_structp png_ptr, png_bytep data, png_size_t length)
    {
        auto *stream = static_cast<PngWriteStream *>(png_get_io_ptr(png_ptr));
        stream->os->write(reinterpret_cast<char *>(data), length);
        if (!stream->os->good())
            png_error(png_ptr, "Write error in PNG stream");
    }
    static void flush(png_structp png_ptr)
    {
        auto *stream = static_cast<PngWriteStream *>(png_get_io_ptr(png_ptr));
        stream->os->flush();
    }
};

/*
   Function check_endianness() returns 1, if architecture
   is little endian, 0 in case of big endian.
 */

int check_endianness()
{
    unsigned int x = 1;
    char        *c = (char *)&x;
    return (int)*c;
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

vector<ImagePtr> load_png_image(istream &is, const string &filename)
{
    if (!check_png_signature(is))
        throw runtime_error("Not a PNG file");

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr)
        throw runtime_error("Failed to create PNG read struct");
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        throw runtime_error("Failed to create PNG info struct");
    }
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        throw runtime_error("Error during PNG read");
    }

    PngReadStream stream{&is};
    png_set_read_fn(png_ptr, &stream, PngReadStream::read);

    png_read_info(png_ptr, info_ptr);

    png_uint_32 width, height;
    int         bit_depth, color_type, interlace, compression, filter;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace, &compression, &filter);

    // Convert palette to RGB, expand bit depths to 16-bit, add alpha if needed
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);
    if ((color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    if (bit_depth == 16 && check_endianness() == 1)
        png_set_swap(png_ptr);

    int channels = png_get_channels(png_ptr, info_ptr);

    int3 size{int(width), int(height), channels};
    auto image                     = make_shared<Image>(size.xy(), size.z);
    image->filename                = filename;
    image->file_has_straight_alpha = size.z == 4 || size.z == 2;
    image->metadata["loader"]      = "libpng";
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        image->metadata["bit depth"] = fmt::format("{}-bit indexed color", bit_depth);
    else
        image->metadata["bit depth"] = fmt::format("{}-bit ({} bpc)", size.z * bit_depth, bit_depth);

    //
    // Read color chunks in reverse priority order
    //

    std::vector<uint8_t> icc_profile;
    bool                 has_icc_profile  = false;
    char                *icc_name         = nullptr;
    int                  compression_type = 0;
    png_bytep            icc_ptr          = nullptr;
    png_uint_32          icc_len          = 0;

    if (png_get_iCCP(png_ptr, info_ptr, &icc_name, &compression_type, &icc_ptr, &icc_len))
    {
        icc_profile.assign(icc_ptr, icc_ptr + icc_len);
        has_icc_profile = true;
        spdlog::info("PNG: Found ICC profile: {} ({} bytes)", icc_name, icc_len);
        image->metadata["icc profile"] = icc_name;
    }

    double           gamma       = 2.2; // default gamma
    int              srgb_intent = 0;
    TransferFunction tf          = TransferFunction_sRGB; // default
    string           tf_desc;

    if (png_get_gAMA(png_ptr, info_ptr, &gamma))
    {
        spdlog::info("PNG: Found gamma chunk: {:.4f}", gamma);
        tf = TransferFunction_Gamma;
    }

    // Read chromaticities if present
    double wx, wy, rx, ry, gx, gy, bx, by;
    if (png_get_cHRM(png_ptr, info_ptr, &wx, &wy, &rx, &ry, &gx, &gy, &bx, &by))
    {
        spdlog::info(
            "PNG: Found chromaticities chunk: R({:.4f},{:.4f}) G({:.4f},{:.4f}) B({:.4f},{:.4f}) W({:.4f},{:.4f})", rx,
            ry, gx, gy, bx, by, wx, wy);
        Imf::addChromaticities(image->header,
                               {Imath::V2f(rx, ry), Imath::V2f(gx, gy), Imath::V2f(bx, by), Imath::V2f(wx, wy)});
    }

    if (png_get_sRGB(png_ptr, info_ptr, &srgb_intent))
    {
        spdlog::info("PNG: Found sRGB chunk. sRGB intent: {}", srgb_intent);
        tf = TransferFunction_sRGB;
    }

    bool has_cICP = false;
#ifdef PNG_cICP_SUPPORTED
    png_byte colour_primaries;
    png_byte transfer_function;
    png_byte matrix_coefficients;
    png_byte video_full_range_flag;

    if (png_get_cICP(png_ptr, info_ptr, &colour_primaries, &transfer_function, &matrix_coefficients,
                     &video_full_range_flag))
    {
        has_cICP = true;
        spdlog::info("PNG: Found cICP chunk: Colour Primaries: {}, Transfer Function: {}, "
                     "Matrix Coefficients: {}, Video Full Range: {}",
                     int(colour_primaries), int(transfer_function), int(matrix_coefficients),
                     int(video_full_range_flag));

        switch (colour_primaries)
        {
        case 1: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_01_gamut)); break;
        case 4: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_04_gamut)); break;
        case 5: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_05_gamut)); break;
        case 6: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_06_gamut)); break;
        case 7: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_07_gamut)); break;
        case 8: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_08_gamut)); break;
        case 9: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_09_gamut)); break;
        case 10: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_10_gamut)); break;
        case 11: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_11_gamut)); break;
        case 12: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_12_gamut)); break;
        case 22: Imf::addChromaticities(image->header, gamut_chromaticities(lin_cicp_22_gamut)); break;
        default: spdlog::warn("PNG: Unknown cICP color primaries: {}", int(colour_primaries)); break;
        }

        switch (transfer_function)
        {
        case 1: [[fallthrough]];
        case 6: [[fallthrough]];
        case 12: [[fallthrough]];
        case 14: [[fallthrough]];
        case 15: tf = TransferFunction_ITU; break;
        case 4:
            tf    = TransferFunction_Gamma;
            gamma = 2.2f;
            break;
        case 5:
            tf    = TransferFunction_Gamma;
            gamma = 2.8f;
            break;
        case 7: tf = TransferFunction_ST240; break;
        case 8: tf = TransferFunction_Linear; break;
        case 11: tf = TransferFunction_IEC61966_2_4; break;
        case 13: tf = TransferFunction_sRGB; break;
        case 16: tf = TransferFunction_BT2100_PQ; break;
        case 17: tf = TransferFunction_DCI_P3; break;
        case 18: tf = TransferFunction_BT2100_HLG; break;
        default:
            spdlog::warn("PNG: cICP transfer function ({}) is not supported, assuming sRGB", transfer_function);
            tf = TransferFunction_sRGB;
            break;
        }
    }
#endif

    switch (tf)
    {
    case TransferFunction_Linear: tf_desc = linear_tf; break;
    case TransferFunction_Gamma: tf_desc = fmt::format("{} ({})", gamma_tf, float(1.0 / gamma)); break;
    case TransferFunction_sRGB: tf_desc = srgb_tf; break;
    case TransferFunction_ITU: tf_desc = itu_tf; break;
    case TransferFunction_BT2100_PQ: tf_desc = pq_tf; break;
    case TransferFunction_BT2100_HLG: tf_desc = hlg_tf; break;
    case TransferFunction_ST240: tf_desc = st240_tf; break;
    case TransferFunction_IEC61966_2_4: tf_desc = iec61966_2_4_tf; break;
    case TransferFunction_DCI_P3: tf_desc = dci_p3_tf; break;
    default:
        tf_desc = fmt::format("{} (assumed)", srgb_tf);
        spdlog::warn("PNG: Transfer function {} is not supported, assuming sRGB", int(tf));
        tf = TransferFunction_sRGB;
        break;
    }
    image->metadata["transfer function"] = tf_desc;

    // Done reading color chunks
    //

    Timer timer;
    // Interlaced: read entire image at once
    vector<png_bytep> row_pointers(height);

    vector<float> float_pixels(width * height * channels);

    if (bit_depth == 16)
    {
        vector<uint16_t> imagedata(width * height * channels);
        for (size_t y = 0; y < height; ++y)
            row_pointers[y] = reinterpret_cast<png_bytep>(&imagedata[y * width * channels]);
        png_read_image(png_ptr, row_pointers.data());

        for (size_t i = 0; i < imagedata.size(); ++i) float_pixels[i] = imagedata[i] / 65535.f;
    }
    else if (bit_depth <= 8)
    {
        vector<uint8_t> imagedata(width * height * channels);
        for (size_t y = 0; y < height; ++y)
            row_pointers[y] = reinterpret_cast<png_bytep>(&imagedata[y * width * channels]);
        png_read_image(png_ptr, row_pointers.data());

        for (size_t i = 0; i < imagedata.size(); ++i) float_pixels[i] = imagedata[i] / 255.f;
    }
    else
        throw runtime_error("Unsupported PNG bit depth");

    // linearize colors
    if (has_icc_profile && !has_cICP)
    {
        float2 red, green, blue, white;
        if (icc::linearize_colors(float_pixels.data(), size, icc_profile, &tf_desc, &red, &green, &blue, &white))
        {
            spdlog::info("PNG: Linearizing colors using ICC profile.");
            Imf::addChromaticities(image->header, {Imath::V2f(red.x, red.y), Imath::V2f(green.x, green.y),
                                                   Imath::V2f(blue.x, blue.y), Imath::V2f(white.x, white.y)});
            image->metadata["transfer function"] = tf_desc;
        }
    }
    else if (tf != TransferFunction_Linear)
    {
        to_linear(float_pixels.data(), size, tf, gamma);
    }

    // copy the interleaved float pixels into the channels
    for (int c = 0; c < size.z; ++c)
        image->channels[c].copy_from_interleaved(float_pixels.data(), size.x, size.y, size.z, c,
                                                 [](float v) { return v; });

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    return {image};
}

void save_png_image(const Image &img, ostream &os, const string &filename, float gain, bool sRGB, bool dither)
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
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, nullptr);
        throw runtime_error("Failed to create PNG info struct");
    }
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        throw runtime_error("Error during PNG write");
    }

    PngWriteStream stream{&os};
    png_set_write_fn(png_ptr, &stream, PngWriteStream::write, PngWriteStream::flush);

    int color_type = (n == 1)   ? PNG_COLOR_TYPE_GRAY
                     : (n == 2) ? PNG_COLOR_TYPE_GRAY_ALPHA
                     : (n == 3) ? PNG_COLOR_TYPE_RGB
                                : PNG_COLOR_TYPE_RGB_ALPHA;

    png_set_IHDR(png_ptr, info_ptr, w, h, 8, color_type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    // Write image data row by row
    size_t row_bytes = w * n;
    for (int y = 0; y < h; ++y) png_write_row(png_ptr, pixels.get() + y * row_bytes);

    png_write_end(png_ptr, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
}