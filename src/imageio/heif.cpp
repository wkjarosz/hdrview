//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

#include "exif.h"
#include "imgui.h"

using namespace std;

struct HEIFSaveOptions
{
    float            gain         = 1.f;
    int              quality      = 95;
    bool             lossless     = false;
    bool             use_alpha    = true;
    int              format_index = 0;
    TransferFunction tf           = TransferFunction_sRGB;
    float            gamma        = 2.2f;
};

static HEIFSaveOptions s_heif_params;

#ifndef HDRVIEW_ENABLE_HEIF

bool is_heif_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_heif_image(istream &is, string_view filename, string_view channel_selector)
{
    throw runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, float, int, bool, int, bool, bool,
                     TransferFunction, float)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, HEIFSaveOptions *)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

HEIFSaveOptions *heif_parameters_gui() { return &s_heif_params; }

#else

#include "colorspace.h"
#include "heif.h"
#include "icc.h"
#include "timer.h"
#include <libheif/heif.h>
#include <libheif/heif_cxx.h>

struct HeifFormatInfo
{
    heif_compression_format format;
    std::string             display_name;
    std::string             id_name;
};

static std::vector<HeifFormatInfo> s_heif_supported_formats;
static bool                        s_heif_formats_initialized = false;

static void init_heif_supported_formats()
{
    if (s_heif_formats_initialized)
        return;
    int num_encoders = heif_get_encoder_descriptors(heif_compression_undefined, nullptr, nullptr, 0);
    std::vector<const heif_encoder_descriptor *> encoders(num_encoders);
    heif_get_encoder_descriptors(heif_compression_undefined, nullptr, encoders.data(), num_encoders);

    std::set<heif_compression_format> seen;
    for (int i = 0; i < num_encoders; ++i)
    {
        const heif_encoder_descriptor *desc = encoders[i];
        heif_compression_format        fmt  = heif_encoder_descriptor_get_compression_format(desc);
        if (seen.count(fmt))
            continue;
        seen.insert(fmt);
        s_heif_supported_formats.push_back(
            {fmt, heif_encoder_descriptor_get_name(desc), heif_encoder_descriptor_get_id_name(desc)});
    }
    s_heif_formats_initialized = true;
}

static heif_transfer_characteristics transfer_function_to_heif(TransferFunction tf)
{
    switch (tf)
    {
    case TransferFunction_Linear: return heif_transfer_characteristic_linear;
    case TransferFunction_sRGB: return heif_transfer_characteristic_IEC_61966_2_1;
    case TransferFunction_ITU: return heif_transfer_characteristic_ITU_R_BT_709_5;
    case TransferFunction_BT2100_PQ: return heif_transfer_characteristic_ITU_R_BT_2100_0_PQ;
    case TransferFunction_BT2100_HLG: return heif_transfer_characteristic_ITU_R_BT_2100_0_HLG;
    case TransferFunction_ST240: return heif_transfer_characteristic_SMPTE_240M;
    case TransferFunction_Log100: return heif_transfer_characteristic_logarithmic_100;
    case TransferFunction_Log100_Sqrt10: return heif_transfer_characteristic_logarithmic_100_sqrt10;
    case TransferFunction_IEC61966_2_4: return heif_transfer_characteristic_IEC_61966_2_4;
    case TransferFunction_DCI_P3: return heif_transfer_characteristic_SMPTE_ST_428_1;
    default: return heif_transfer_characteristic_IEC_61966_2_1; // fallback to sRGB
    }
}

static bool is_heif_transfer_supported(TransferFunction tf)
{
    switch (transfer_function_to_heif(tf))
    {
    case heif_transfer_characteristic_linear:
    case heif_transfer_characteristic_IEC_61966_2_1:
    case heif_transfer_characteristic_ITU_R_BT_709_5:
    case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
    case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
    case heif_transfer_characteristic_SMPTE_240M:
    case heif_transfer_characteristic_logarithmic_100:
    case heif_transfer_characteristic_logarithmic_100_sqrt10:
    case heif_transfer_characteristic_IEC_61966_2_4:
    case heif_transfer_characteristic_SMPTE_ST_428_1: return true;
    default: return false;
    }
}

static bool linearize_colors(float *pixels, int3 size, const heif::ColorProfile_nclx *nclx,
                             string *tf_description = nullptr, Chromaticities *c = nullptr)
{
    if (!nclx)
        return false;

    if (c)
        *c = chromaticities_from_cicp((int)nclx->get_color_primaries());

    float            gamma = 1.f;
    TransferFunction tf    = transfer_function_from_cicp((int)nclx->get_transfer_characteristics(), &gamma);

    if (tf == TransferFunction_Unknown)
        spdlog::warn("HEIF: cICP transfer function ({}) is not recognized, assuming sRGB",
                     (int)nclx->get_transfer_characteristics());

    if (tf_description)
        *tf_description = transfer_function_name(tf);

    to_linear(pixels, size, tf, gamma);

    return true;
}

// Add preferred colorspace to header
static auto colorspace_name(heif_colorspace cs)
{
    switch (cs)
    {
    case heif_colorspace_YCbCr: return "YCbCr";
    case heif_colorspace_RGB: return "RGB";
    case heif_colorspace_monochrome: return "Monochrome";
    case heif_colorspace_nonvisual: return "Nonvisual";
    case heif_colorspace_undefined: return "Undefined";
    default: return "Unknown";
    }
};

// Add preferred chroma to header
static auto chroma_name(heif_chroma ch)
{
    switch (ch)
    {
    case heif_chroma_monochrome: return "Monochrome";
    case heif_chroma_420: return "4:2:0";
    case heif_chroma_422: return "4:2:2";
    case heif_chroma_444: return "4:4:4";
    case heif_chroma_interleaved_RGB: return "Interleaved RGB";
    case heif_chroma_interleaved_RGBA: return "Interleaved RGBA";
    case heif_chroma_interleaved_RRGGBB_BE: return "Interleaved RRGGBB (BE)";
    case heif_chroma_interleaved_RRGGBBAA_BE: return "Interleaved RRGGBBAA (BE)";
    case heif_chroma_interleaved_RRGGBB_LE: return "Interleaved RRGGBB (LE)";
    case heif_chroma_interleaved_RRGGBBAA_LE: return "Interleaved RRGGBBAA (LE)";
    case heif_chroma_undefined: return "Undefined";
    default: return "Unknown";
    }
};

vector<ImagePtr> load_heif_image(istream &is, string_view filename, string_view channel_selector)
{
    ScopedMDC mdc{"IO", "HEIF"};
    // calculate size of stream
    is.clear();
    is.seekg(0, is.end);
    size_t raw_size = is.tellg();
    is.seekg(0, is.beg);

    // read in the whole stream
    vector<char> raw_data(raw_size);
    is.read(raw_data.data(), raw_size);
    if ((size_t)is.gcount() != raw_size)
        throw invalid_argument{
            fmt::format("Failed to read : {} bytes, read : {} bytes", raw_size, (size_t)is.gcount())};

    vector<ImagePtr> images;
    try
    {
        heif::Context ctx;

        ctx.read_from_memory_without_copy(reinterpret_cast<void *>(raw_data.data()), raw_size);

        auto primary_id = ctx.get_primary_image_ID();            // id of primary image
        auto item_ids   = ctx.get_list_of_top_level_image_IDs(); // ids of all other images

        // remove the primary item from the list of all items
        for (size_t i = 0; i < item_ids.size(); ++i)
            if (item_ids[i] == primary_id)
            {
                item_ids.erase(item_ids.begin() + i);
                break;
            }

        int num_subimages = 1 + int(item_ids.size());

        spdlog::info("Found {} subimages", num_subimages);

        ImGuiTextFilter filter{string(channel_selector).c_str()};
        filter.Build();

        // just get the primary image for now
        for (int subimage = 0; subimage < num_subimages; ++subimage)
        {
            spdlog::info("Loading subimage {}...", subimage);
            auto id = (subimage == 0) ? primary_id : item_ids[subimage - 1];

            if (auto name = fmt::format("{:d}.R,G,B", id); !filter.PassFilter(name.c_str()))
            {
                spdlog::debug("Color channels '{}' filtered out by channel selector '{}'", name, channel_selector);
                continue;
            }

            auto ihandle     = ctx.get_image_handle(id);
            auto raw_ihandle = ihandle.get_raw_image_handle();

            if (ihandle.empty() || !raw_ihandle)
                continue;

            heif_error err;

            heif_colorspace preferred_colorspace;
            heif_chroma     preferred_chroma;
            err = heif_image_handle_get_preferred_decoding_colorspace(raw_ihandle, &preferred_colorspace,
                                                                      &preferred_chroma);
            spdlog::info("Preferred decoding colorspace: {}, chroma: {}", (int)preferred_colorspace,
                         (int)preferred_chroma);

            int3            size{(int)ihandle.get_width(), (int)ihandle.get_height(), 0};
            bool            has_alpha = ihandle.has_alpha_channel();
            heif_chroma     out_chroma;
            heif_colorspace out_colorspace;
            heif_channel    out_planes[2] = {heif_channel_Y, heif_channel_Alpha};
            int             cpp; // channels per plane
            int             num_planes = 1;
            switch (preferred_chroma)
            {
            case heif_chroma_monochrome:
                out_chroma     = heif_chroma_monochrome;
                out_colorspace = heif_colorspace_monochrome;
                out_planes[0]  = heif_channel_Y;
                size.z         = has_alpha ? 2 : 1;
                cpp            = 1;
                num_planes     = size.z;
                break;
            default:
                out_chroma     = has_alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE;
                out_colorspace = heif_colorspace_RGB;
                out_planes[0]  = heif_channel_interleaved;
                size.z         = has_alpha ? 4 : 3;
                cpp            = size.z;
                num_planes     = 1;
                break;
            }
            spdlog::info("Image size: {}", size);

            auto image                                        = make_shared<Image>(size.xy(), size.z);
            image->filename                                   = filename;
            image->partname                                   = fmt::format("{:d}", id);
            image->file_has_straight_alpha                    = has_alpha && !ihandle.is_premultiplied_alpha();
            image->metadata["loader"]                         = "libheif";
            image->metadata["header"]["preferred colorspace"] = {
                {"value", int(preferred_colorspace)},
                {"string", fmt::format("{} ({})", colorspace_name(preferred_colorspace), int(preferred_colorspace))},
                {"type", "int"}};
            image->metadata["header"]["preferred chroma"] = {
                {"value", int(preferred_chroma)},
                {"string", fmt::format("{} ({})", chroma_name(preferred_chroma), int(preferred_chroma))},
                {"type", "int"}};

            // try to get exif metadata using C++ API
            auto metadataIDs = ihandle.get_list_of_metadata_block_IDs("Exif");
            if (!metadataIDs.empty())
            {
                spdlog::info("Found {} EXIF metadata block(s). Attempting to decode...", metadataIDs.size());
                for (auto metadata_id : metadataIDs)
                {
                    try
                    {
                        auto exif_data = ihandle.get_metadata(metadata_id); // throws on error
                        if (exif_data.size() <= 4)
                        {
                            spdlog::warn("Failed to get size of EXIF data.");
                            continue;
                        }
                        // exclude the first four bytes, which are the length
                        auto j                  = exif_to_json(exif_data.data() + 4, exif_data.size() - 4);
                        image->metadata["exif"] = j;
                        spdlog::debug("EXIF metadata successfully parsed: {}", image->metadata.dump(2));
                    }
                    catch (const heif::Error &err)
                    {
                        spdlog::warn("Failed to read EXIF data: {}", err.get_message());
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
                    }
                }
            }

            spdlog::info("Decoding heif image...");
            auto himage = ihandle.decode_image(out_colorspace, out_chroma);

            if (himage.get_width(out_planes[0]) != size.x || himage.get_height(out_planes[0]) != size.y)
            {
                spdlog::warn("Image size mismatch: {}x{} vs {}x{}", himage.get_width(out_planes[0]),
                             himage.get_height(out_planes[0]), size.x, size.y);
                size.x = himage.get_width(out_planes[0]);
                size.y = himage.get_height(out_planes[0]);
            }

            image->metadata["header"]["decoded colorspace"] = {
                {"value", int(himage.get_colorspace())},
                {"string",
                 fmt::format("{} ({})", colorspace_name(himage.get_colorspace()), int(himage.get_colorspace()))},
                {"type", "int"}};

            image->metadata["header"]["decoded chroma"] = {
                {"value", int(himage.get_chroma_format())},
                {"string",
                 fmt::format("{} ({})", chroma_name(himage.get_chroma_format()), int(himage.get_chroma_format()))},
                {"type", "int"}};

            // Prefer the image-level NCLX profile via the C++ API (throws heif::Error if missing),
            // and fall back to the handle-level C API if necessary.
            std::unique_ptr<heif::ColorProfile_nclx> nclx;
            try
            {
                auto tmp = himage.get_nclx_color_profile(); // may throw if none

                // allocate our own instance and copy fields (avoid copying internals of nclx_tmp)
                // due to memory safety bug in libheif
                nclx = std::make_unique<heif::ColorProfile_nclx>();
                nclx->set_color_primaries(tmp.get_color_primaries());
                nclx->set_transfer_characteristics(tmp.get_transfer_characteristics());
                nclx->set_matrix_coefficients(tmp.get_matrix_coefficients());
                nclx->set_full_range_flag(tmp.is_full_range());

                image->metadata["header"]["nclx profile"] =
                    json{{"value", 2}, {"string", "present at image level"}, {"type", "enum"}};
            }
            catch (const heif::Error &e)
            {
                heif_color_profile_nclx *nclx_raw = nullptr;
                spdlog::warn("No image-level nclx color profile found: {}. Trying at handle level", e.get_message());
                if (err = heif_image_handle_get_nclx_color_profile(raw_ihandle, &nclx_raw); err.code == heif_error_Ok)
                {
                    // create our owned C++ object and copy fields from raw C struct
                    nclx = std::make_unique<heif::ColorProfile_nclx>();
                    nclx->set_color_primaries(nclx_raw->color_primaries);
                    nclx->set_transfer_characteristics(nclx_raw->transfer_characteristics);
                    nclx->set_matrix_coefficients(nclx_raw->matrix_coefficients);
                    nclx->set_full_range_flag(nclx_raw->full_range_flag);

                    // free the raw C struct immediately (we copied fields)
                    heif_nclx_color_profile_free(nclx_raw);

                    image->metadata["header"]["nclx profile"] =
                        json{{"value", 1}, {"string", "present at handle level"}, {"type", "enum"}};
                }
                else
                {
                    spdlog::warn("No handle-level nclx color profile found either: {}", err.message);
                    image->metadata["header"]["nclx profile"] =
                        json{{"value", 0}, {"string", "missing"}, {"type", "enum"}};
                }
            }

            // get the icc profile, first try the image level, then the handle level
            vector<uint8_t> icc_profile; // handle-level icc profile
            try
            {
                icc_profile = himage.get_raw_color_profile();
                spdlog::info("File contains an image-level ICC profile.");
                image->metadata["header"]["icc profile"] =
                    json{{"value", 2}, {"string", "present at image level"}, {"type", "enum"}};
            }
            catch (const heif::Error &err)
            {
                spdlog::info("No image-level ICC profile found: {}. Trying at handle level", err.get_message());
                icc_profile.clear();
                if (size_t icc_size = heif_image_handle_get_raw_color_profile_size(raw_ihandle); icc_size != 0)
                {
                    spdlog::info("File contains a handle-level ICC profile.");
                    icc_profile.resize(icc_size);
                    if (auto err2 = heif_image_handle_get_raw_color_profile(
                            raw_ihandle, reinterpret_cast<void *>(icc_profile.data()));
                        err2.code != heif_error_Ok)
                    {
                        spdlog::warn("Could not read handle-level ICC profile either: {}", err2.message);
                        icc_profile.clear();
                    }
                }
            }

            spdlog::info("Copying image channels...");
            Timer timer;
            // the code below works for both interleaved (RGBA) and planar (YA) channel layouts
            for (int p = 0; p < num_planes; ++p)
            {
                size_t         bytes_per_line = 0;
                const uint8_t *pixels         = himage.get_plane(out_planes[p], &bytes_per_line);
                int            bpp_storage    = himage.get_bits_per_pixel(out_planes[p]);
                int            bpc            = himage.get_bits_per_pixel_range(out_planes[p]);
                spdlog::debug(
                    "Bits per pixel: {}; Bits per pixel storage: {}; Channels per pixel: {}; Bytes per line: {}", bpc,
                    bpp_storage, cpp, bytes_per_line);
                if (bpp_storage != cpp * 16 && bpp_storage != cpp * 8)
                    throw runtime_error(fmt::format("Unsupported bits per pixel: {}", bpp_storage));
                if (p == 0)
                    image->metadata["pixel format"] = fmt::format("{}-bit ({} bpc)", size.z * bpc, bpc);
                float bpc_div = 1.f / ((1 << bpc) - 1);

                // copy pixels into a contiguous float buffer and normalize values to [0,1]
                spdlog::debug("Copying to contiguous float buffer");
                vector<float> float_pixels(size.x * size.y * cpp);
                bool          is_16bit = bpp_storage == cpp * 16;
                for (int y = 0; y < size.y; ++y)
                {
                    auto row8  = reinterpret_cast<const uint8_t *>(pixels + y * bytes_per_line);
                    auto row16 = reinterpret_cast<const uint16_t *>(pixels + y * bytes_per_line);
                    for (int x = 0; x < size.x; ++x)
                        for (int c = 0; c < cpp; ++c)
                            float_pixels[(y * size.x + x) * cpp + c] =
                                bpc_div * (is_16bit ? row16[cpp * x + c] : row8[cpp * x + c]);
                }
                spdlog::debug("done copying to continuguous float buffer");

                // only prefer the nclx if it exists and it specifies an HDR transfer function
                bool prefer_icc = // false;
                    !nclx ||
                    (nclx->get_transfer_characteristics() != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG &&
                     nclx->get_transfer_characteristics() != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ);

                string         tf_description;
                Chromaticities chr;
                // for SDR profiles, try to transform the interleaved data using the icc profile.
                // Then try the nclx profile
                if ((prefer_icc && icc::linearize_colors(float_pixels.data(), int3{size.xy(), cpp}, icc_profile,
                                                         &tf_description, &chr)) ||
                    linearize_colors(float_pixels.data(), int3{size.xy(), cpp}, nclx.get(), &tf_description, &chr))
                {
                    image->chromaticities                = chr;
                    image->metadata["transfer function"] = tf_description;
                }
                else
                    image->metadata["transfer function"] = transfer_function_name(TransferFunction_Unknown);

                // copy the interleaved float pixels into the channels
                for (int c = 0; c < cpp; ++c)
                    image->channels[p * cpp + c].copy_from_interleaved(float_pixels.data(), size.x, size.y, cpp, c,
                                                                       [](float v) { return v; });
            }

            spdlog::info("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));
            images.emplace_back(image);
        }
    }
    catch (const heif::Error &err)
    {
        std::string e = err.get_message();
        throw invalid_argument{e.empty() ? "unknown exception" : e};
    }
    catch (const exception &err)
    {
        std::string e = err.what();
        throw invalid_argument{e.empty() ? "unknown exception" : e};
    }
    catch (...)
    {
        throw invalid_argument{"unknown exception"};
    }

    return images;
}

bool is_heif_image(istream &is) noexcept
{
    bool ret = false;
    try
    {
        uint8_t magic[12];
        is.read(reinterpret_cast<char *>(magic), sizeof(magic));

        heif_filetype_result filetype_check =
            heif_check_filetype(magic, std::min((int)sizeof(magic), (int)is.gcount()));
        if (filetype_check == heif_filetype_no)
            throw invalid_argument{"Not a HEIF/AVIF file"};
        ret = true;
        if (filetype_check == heif_filetype_yes_unsupported)
            spdlog::warn("This is an unsupported HEIF/AVIF file. Reading will probably fail.");
    }
    catch (const exception &e)
    {
        spdlog::debug("Cannot load image with libheif: {}", e.what());
        ret = false;
    }

    is.clear();
    is.seekg(0);
    return ret;
}

void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, float gain, int quality,
                     bool lossless, bool use_alpha, int format_index, TransferFunction tf, float gamma)
{
    try
    {
        Timer timer;

        int                        w = 0, h = 0, n = 0;
        std::unique_ptr<uint8_t[]> pixels8;
        void                      *pixels = nullptr;
        pixels8                           = img.as_interleaved<uint8_t>(&w, &h, &n, gain, tf, gamma, true, true, true);
        if (!use_alpha && n == 4)
        {
            size_t                     num_pixels = w * h;
            std::unique_ptr<uint8_t[]> rgb_pixels(new uint8_t[num_pixels * 3]);
            for (size_t i = 0, j = 0; i < num_pixels; ++i, j += 4)
            {
                rgb_pixels[i * 3 + 0] = pixels8[j + 0];
                rgb_pixels[i * 3 + 1] = pixels8[j + 1];
                rgb_pixels[i * 3 + 2] = pixels8[j + 2];
            }
            pixels8.swap(rgb_pixels);
            n = 3;
        }
        pixels = pixels8.get();

        if (!pixels || w <= 0 || h <= 0)
            throw std::runtime_error("HEIF: empty image or invalid image dimensions");

        if (n != 3 && n != 4)
            throw std::invalid_argument("HEIF/AVIF output only supports 3 or 4 channels (RGB or RGBA)");

        heif::Context   ctx;
        heif::Image     heif_img;
        heif_colorspace colorspace = heif_colorspace_RGB;
        heif_chroma     chroma     = (n == 4) ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;

        heif_img.create(w, h, colorspace, chroma);
        heif_img.add_plane(heif_channel_interleaved, w, h, 8);

        size_t   stride;
        uint8_t *plane = heif_img.get_plane(heif_channel_interleaved, &stride);

        // Copy pixel data
        size_t row_bytes = w * n * sizeof(uint8_t);
        for (int y = 0; y < h; ++y) { memcpy(plane + y * stride, pixels8.get() + y * w * n, row_bytes); }

        // Set color profile (nclx)
        heif::ColorProfile_nclx nclx;
        nclx.set_color_primaries(heif_color_primaries_ITU_R_BT_709_5); // TODO map from img.chromaticities
        heif_transfer_characteristics heif_tf = transfer_function_to_heif(tf);
        if (!is_heif_transfer_supported(tf))
        {
            spdlog::warn("HEIF: Transfer function '{}' not supported, falling back to sRGB.",
                         transfer_function_name(tf, gamma));
            heif_tf = heif_transfer_characteristic_IEC_61966_2_1;
        }
        nclx.set_transfer_characteristics(heif_tf);

        nclx.set_matrix_coefficients(heif_matrix_coefficients_ITU_R_BT_709_5);
        nclx.set_full_range_flag(true);
        heif_img.set_nclx_color_profile(nclx);

        // Encoder selection
        if (format_index < 0 || format_index >= int(s_heif_supported_formats.size()))
            throw std::runtime_error("Invalid HEIF/AVIF format selected");

        auto encoder = heif::Encoder(s_heif_supported_formats[format_index].format);
        encoder.set_lossy_quality(quality);
        encoder.set_lossless(lossless);

        // Encode and write
        auto handle = ctx.encode_image(heif_img, encoder);
        ctx.set_primary_image(handle);
        struct StreamWriter : public heif::Context::Writer
        {
            std::ostream &os;
            StreamWriter(std::ostream &out) : os(out) {}
            heif_error write(const void *data, size_t size) override
            {
                heif_error herr{heif_error_Ok, heif_suberror_Unspecified, ""};
                os.write(reinterpret_cast<const char *>(data), size);
                if (!os)
                {
                    herr.code    = heif_error_Encoding_error;
                    herr.message = "Failed to write to output stream";
                }
                return herr;
            }
        } writer(os);
        ctx.write(writer);
        spdlog::info("Saved image to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
    }
    catch (const heif::Error &err)
    {
        throw std::runtime_error(
            fmt::format("HEIF error ({}.{}): {}", int(err.get_code()), int(err.get_subcode()), err.get_message()));
    }
}

// Opaque pointer version
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, HEIFSaveOptions *params)
{
    if (!params)
        throw std::invalid_argument("HEIFSaveOptions pointer is null");
    save_heif_image(img, os, filename, params->gain, params->quality, params->lossless, params->use_alpha,
                    params->format_index, params->tf, params->gamma);
}

// GUI parameter function
HEIFSaveOptions *heif_parameters_gui()
{
    init_heif_supported_formats();

    ImGui::SliderFloat("Gain", &s_heif_params.gain, 0.1f, 10.0f);
    ImGui::SliderInt("Quality", &s_heif_params.quality, 1, 100);
    ImGui::Checkbox("Lossless", &s_heif_params.lossless);
    ImGui::Checkbox("Include alpha", &s_heif_params.use_alpha);

    // Format combo
    static int selected_format = 0;
    if (selected_format >= int(s_heif_supported_formats.size()))
        selected_format = 0;
    if (ImGui::BeginCombo("Format", s_heif_supported_formats[selected_format].display_name.c_str()))
    {
        for (size_t i = 0; i < s_heif_supported_formats.size(); ++i)
        {
            bool selected = (selected_format == int(i));
            if (ImGui::Selectable(s_heif_supported_formats[i].display_name.c_str(), selected))
                selected_format = int(i);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    s_heif_params.format_index = selected_format; // repurpose as index

    if (ImGui::BeginCombo("Transfer function",
                          transfer_function_name(s_heif_params.tf, 1.f / s_heif_params.gamma).c_str()))
    {
        for (int i = TransferFunction_Linear; i < TransferFunction_Count; ++i)
        {
            if (!is_heif_transfer_supported((TransferFunction)i))
                continue;
            bool selected = (s_heif_params.tf == (TransferFunction)i);
            if (ImGui::Selectable(transfer_function_name((TransferFunction)i, 1.f / s_heif_params.gamma).c_str(),
                                  selected))
                s_heif_params.tf = (TransferFunction)i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SliderFloat("Gamma", &s_heif_params.gamma, 0.1f, 5.0f);
    if (ImGui::Button("Reset options to defaults"))
        s_heif_params = HEIFSaveOptions{};
    return &s_heif_params;
}

#endif
