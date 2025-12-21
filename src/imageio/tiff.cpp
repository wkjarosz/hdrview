//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "tiff.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
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

#ifndef HDRVIEW_ENABLE_LIBTIFF

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

namespace
{

// Custom TIFF error and warning handlers
void tiffErrorHandler(const char *module, const char *fmt, va_list args)
{
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    spdlog::error("TIFF error ({}): {}", module ? module : "unknown", buffer);
}

void tiffWarningHandler(const char *module, const char *fmt, va_list args)
{
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    spdlog::warn("TIFF warning ({}): {}", module ? module : "unknown", buffer);
}

// Custom TIFF I/O structure for reading from memory
struct TiffData
{
    const uint8_t *data;
    toff_t         offset;
    tsize_t        size;

    TiffData(const uint8_t *data, size_t size) : data(data), offset(0), size(size) {}
};

tsize_t tiffReadProc(thandle_t handle, tdata_t data, tsize_t size)
{
    auto tiffData = reinterpret_cast<TiffData *>(handle);
    size          = min(size, tiffData->size - (tsize_t)tiffData->offset);
    memcpy(data, tiffData->data + tiffData->offset, size);
    tiffData->offset += size;
    return size;
}

tsize_t tiffWriteProc(thandle_t, tdata_t, tsize_t) { return 0; }

toff_t tiffSeekProc(thandle_t handle, toff_t offset, int whence)
{
    auto tiffData = reinterpret_cast<TiffData *>(handle);
    switch (whence)
    {
    case SEEK_SET: tiffData->offset = offset; break;
    case SEEK_CUR: tiffData->offset += offset; break;
    case SEEK_END: tiffData->offset = tiffData->size - offset; break;
    }
    return tiffData->offset;
}

int tiffCloseProc(thandle_t) { return 0; }

toff_t tiffSizeProc(thandle_t handle)
{
    auto tiffData = reinterpret_cast<TiffData *>(handle);
    return tiffData->size;
}

int tiffMapProc(thandle_t handle, tdata_t *pdata, toff_t *psize)
{
    auto tiffData = reinterpret_cast<TiffData *>(handle);
    *pdata        = (tdata_t)tiffData->data;
    *psize        = tiffData->size;
    return 1;
}

void tiffUnmapProc(thandle_t, tdata_t, toff_t) {}

// Custom TIFF I/O for writing to ostream
struct TiffWriteData
{
    ostream     *os;
    vector<char> buffer;

    explicit TiffWriteData(ostream *os) : os(os) {}
};

tsize_t tiffWriteProcOut(thandle_t handle, tdata_t data, tsize_t size)
{
    auto writeData = reinterpret_cast<TiffWriteData *>(handle);
    writeData->os->write(reinterpret_cast<const char *>(data), size);
    return writeData->os->good() ? size : 0;
}

toff_t tiffSeekProcOut(thandle_t handle, toff_t offset, int whence)
{
    auto writeData = reinterpret_cast<TiffWriteData *>(handle);
    writeData->os->seekp(offset, whence == SEEK_SET ? ios::beg : (whence == SEEK_CUR ? ios::cur : ios::end));
    return writeData->os->tellp();
}

int tiffCloseProcOut(thandle_t) { return 0; }

toff_t tiffSizeProcOut(thandle_t handle)
{
    auto writeData = reinterpret_cast<TiffWriteData *>(handle);
    auto pos       = writeData->os->tellp();
    writeData->os->seekp(0, ios::end);
    auto size = writeData->os->tellp();
    writeData->os->seekp(pos);
    return size;
}

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

} // namespace

bool is_tiff_image(istream &is) noexcept
{
    auto start = is.tellg();
    bool ret   = false;
    try
    {
        ret = check_tiff_signature(is) != 0;
    }
    catch (...)
    {
    }
    is.clear();
    is.seekg(start);
    return ret;
}

static inline void throw_if_error(int status, const string_view msg)
{
    if (status == 0)
        throw invalid_argument(fmt::format("Failed to read {}'. LibTiff error code {}'", msg, status));
}

static vector<ImagePtr> load_image(TIFF *tif, bool reverse_endian, tdir_t dir, int sub_id, int sub_chain_id,
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

        int num_channels = samples_per_pixel;

        auto image                = make_shared<Image>(int2{(int)width, (int)height}, num_channels);
        image->alpha_type         = num_channels > 3 ? AlphaType_Straight : AlphaType_None;
        image->metadata["loader"] = "libtiff";
        image->partname           = partname;

        // Extract metadata
        char       *description      = nullptr;
        char       *software         = nullptr;
        char       *datetime         = nullptr;
        const char *compression_name = "";

        if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &description))
            image->metadata["description"] = description;
        if (TIFFGetField(tif, TIFFTAG_SOFTWARE, &software))
            image->metadata["software"] = software;
        if (TIFFGetField(tif, TIFFTAG_DATETIME, &datetime))
            image->metadata["datetime"] = datetime;

        switch (compression_type)
        {
        case COMPRESSION_NONE: compression_name = "None"; break;
        case COMPRESSION_LZW: compression_name = "LZW"; break;
        case COMPRESSION_DEFLATE: compression_name = "Deflate/ZIP"; break;
        case COMPRESSION_PACKBITS: compression_name = "PackBits"; break;
        case COMPRESSION_JPEG: compression_name = "JPEG"; break;
        default: compression_name = "Other";
        }
        image->metadata["compression"] = compression_name;

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

        // Handle ICC profile
        uint32_t icc_profile_size = 0;
        void    *icc_profile_data = nullptr;
        if (TIFFGetField(tif, TIFFTAG_ICCPROFILE, &icc_profile_size, &icc_profile_data) && icc_profile_size > 0)
        {
            image->icc_data.resize(icc_profile_size);
            memcpy(image->icc_data.data(), icc_profile_data, icc_profile_size);
        }

        // Note: TIFF EXIF handling could be added here if needed

        // Read pixel data using RGBA interface (easiest and most compatible)
        vector<uint32_t> rgba_data(width * height);
        throw_if_error(TIFFReadRGBAImageOriented(tif, width, height, rgba_data.data(), ORIENTATION_TOPLEFT, 0),
                       "TIFF pixel data.");

        // Store pixels in a flat array first
        int3          size{(int)width, (int)height, num_channels};
        vector<float> float_pixels(product(size));

        // Convert RGBA data to float channels
        for (size_t y = 0; y < height; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                size_t   idx  = y * width + x;
                uint32_t rgba = rgba_data[idx];

                // TIFF stores as ABGR in memory
                uint8_t r = TIFFGetR(rgba);
                uint8_t g = TIFFGetG(rgba);
                uint8_t b = TIFFGetB(rgba);
                uint8_t a = TIFFGetA(rgba);

                int pixel_idx = idx * num_channels;

                if (num_channels >= 1)
                    float_pixels[pixel_idx + 0] = r / 255.f;
                if (num_channels >= 2)
                    float_pixels[pixel_idx + 1] = g / 255.f;
                if (num_channels >= 3)
                    float_pixels[pixel_idx + 2] = b / 255.f;
                if (num_channels >= 4)
                    float_pixels[pixel_idx + 3] = a / 255.f;
            }
        }

        // Apply color space conversions if requested
        string         profile_desc;
        Chromaticities c;

        if (!image->icc_data.empty() && !opts.override_profile)
        {
            if (ICCProfile(image->icc_data)
                    .linearize_pixels(float_pixels.data(), size, opts.keep_primaries, &profile_desc, &c))
            {
                spdlog::info("Linearizing colors using ICC profile.");
                image->chromaticities = c;
            }
        }
        else
        {
            // Default transfer function based on sample format
            TransferFunction tf =
                sample_format == SAMPLEFORMAT_IEEEFP ? TransferFunction::Linear : TransferFunction::sRGB;

            if (opts.override_profile)
                tf = opts.tf_override;

            if (tf.type != TransferFunction::Linear)
            {
                // spdlog::info("Linearizing colors using TIFF transfer function: {}", tf);
                if (linearize_pixels(float_pixels.data(), size, Chromaticities(), tf, opts.keep_primaries,
                                     &profile_desc, &c))
                    image->chromaticities = c;
            }
            else
            {
                profile_desc = "Linear";
                // spdlog::info("Image is already in linear color space.");
            }
        }

        image->metadata["color profile"] = profile_desc;

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

static vector<ImagePtr> load_sub_images(TIFF *tif, bool reverse_endian, tdir_t dir, const ImageLoadOptions &opts)
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
    }

    // Go back to main-IFD chain and re-read that main-IFD directory
    if (!TIFFSetDirectory(tif, dir))
        spdlog::warn("Failed to re-read the main IFD directory.");

    return images;
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
    TIFFSetErrorHandler(tiffErrorHandler);
    TIFFSetWarningHandler(tiffWarningHandler);

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

    TiffData tiff_data(data.data(), file_size);
    TIFF *tif = TIFFClientOpen(string(filename).c_str(), "rMc", reinterpret_cast<thandle_t>(&tiff_data), tiffReadProc,
                               tiffWriteProc, tiffSeekProc, tiffCloseProc, tiffSizeProc, tiffMapProc, tiffUnmapProc);

    if (!tif)
        throw runtime_error("Failed to open TIFF file.");

    auto tif_guard = ScopeGuard{[tif] { TIFFClose(tif); }};

    vector<ImagePtr> images;

    // TIFF files can contain multiple directories (sub-images)
    do {

        tdir_t dir = TIFFCurrentDirectory(tif);

        auto added_images = load_image(tif, reverse_endian, dir, -1, -1, opts);
        for (auto image : added_images)
        {
            image->filename = filename;
            images.push_back(image);
        }

        auto sub_images = load_sub_images(tif, reverse_endian, dir, opts);
        for (auto sub_image : sub_images)
        {
            sub_image->filename = filename;
            images.push_back(sub_image);
        }

    } while (TIFFReadDirectory(tif));

    return images;
}

void save_tiff_image(const Image &img, std::ostream &os, std::string_view filename, const TIFFSaveOptions *opts)
{
    if (!opts)
        throw invalid_argument("TIFFSaveOptions is required.");

    ScopedMDC mdc{"IO", "TIFF"};
    Timer     timer;

    // Set custom error/warning handlers
    TIFFSetErrorHandler(tiffErrorHandler);
    TIFFSetWarningHandler(tiffWarningHandler);

    TiffWriteData write_data(&os);
    TIFF *tif = TIFFClientOpen(string(filename).c_str(), "wm", reinterpret_cast<thandle_t>(&write_data), tiffReadProc,
                               tiffWriteProcOut, tiffSeekProcOut, tiffCloseProcOut, tiffSizeProcOut, nullptr, nullptr);

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
