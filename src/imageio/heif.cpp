//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

#include "app.h"
#include "imgui.h"

using namespace std;

#ifndef HDRVIEW_ENABLE_LIBHEIF

bool is_heif_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_heif_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, float, int, bool, int, bool, bool,
                     TransferFunction, float)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, const struct HEIFSaveOptions *)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

HEIFSaveOptions *heif_parameters_gui() { return nullptr; }

#else

#include "colorspace.h"
#include "exif.h"
#include "fonts.h"
#include "heif.h"
#include "icc.h"
#include "imgui_ext.h"
#include "json.h"
#include "timer.h"
#include <cstdint>
#include <cstdio>

#include <libheif/heif.h>
#include <libheif/heif_cxx.h>

struct HEIFSaveOptions
{
    float             gain         = 1.f;
    int               quality      = 95;
    bool              lossless     = false;
    bool              use_alpha    = true;
    int               format_index = 0;
    TransferFunction_ tf           = TransferFunction_sRGB;
    float             gamma        = 2.2f;
    // encoder parameters stored as JSON so we can keep typed values
    json encoder_parameters = json::object();
};

static HEIFSaveOptions s_opts;

static std::vector<heif::EncoderDescriptor> s_heif_supported_formats;
static bool                                 s_heif_formats_initialized = false;

static void init_heif_supported_formats()
{
    if (s_heif_formats_initialized)
        return;

    s_heif_supported_formats   = heif::EncoderDescriptor::get_encoder_descriptors(heif_compression_undefined, nullptr);
    s_heif_formats_initialized = true;
}

static heif_transfer_characteristics transfer_function_to_heif(TransferFunction_ tf)
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

static bool is_heif_transfer_supported(TransferFunction_ tf)
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

    float             gamma = 1.f;
    TransferFunction_ tf    = transfer_function_from_cicp((int)nclx->get_transfer_characteristics(), &gamma);

    if (tf == TransferFunction_Unspecified)
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

vector<ImagePtr> load_heif_image(istream &is, string_view filename, const ImageLoadOptions &opts)
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

        ImGuiTextFilter filter{opts.channel_selector.c_str()};
        filter.Build();

        // just get the primary image for now
        for (int subimage = 0; subimage < num_subimages; ++subimage)
        {
            spdlog::info("Loading subimage {}...", subimage);
            auto id = (subimage == 0) ? primary_id : item_ids[subimage - 1];

            if (auto name = fmt::format("{:d}.R,G,B", id); !filter.PassFilter(name.c_str()))
            {
                spdlog::debug("Color channels '{}' filtered out by channel selector '{}'", name, opts.channel_selector);
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
            vector<uint8_t> icc_profile;
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
                int            bytes_per_line = 0;
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
                    auto row8  = reinterpret_cast<const uint8_t *>(pixels + y * (size_t)bytes_per_line);
                    auto row16 = reinterpret_cast<const uint16_t *>(pixels + y * (size_t)bytes_per_line);
                    for (int x = 0; x < size.x; ++x)
                        for (int c = 0; c < cpp; ++c)
                            float_pixels[(y * size.x + x) * cpp + c] =
                                bpc_div * (is_16bit ? row16[cpp * x + c] : row8[cpp * x + c]);
                }
                spdlog::debug("done copying to continuguous float buffer");

                if (opts.tf != TransferFunction_Unspecified)
                {
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
                        image->metadata["transfer function"] = transfer_function_name(TransferFunction_Unspecified);
                }
                else
                {
                    // FIXME: probably still want to use the icc/nclx profile to get chromaticities
                    // linearize using the user-specified transfer function
                    to_linear(float_pixels.data(), int3{size.xy(), cpp}, opts.tf, opts.gamma);
                    image->metadata["transfer function"] = transfer_function_name(opts.tf, opts.gamma);
                }

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

// Opaque pointer version
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, const HEIFSaveOptions *params)
{
    if (!params)
        throw std::invalid_argument("HEIFSaveOptions pointer is null");
    try
    {
        Timer timer;

        int                        w = 0, h = 0, n = 0;
        std::unique_ptr<uint8_t[]> pixels8;
        void                      *pixels = nullptr;
        pixels8 = img.as_interleaved<uint8_t>(&w, &h, &n, params->gain, params->tf, params->gamma, true, true, true);
        if (!params->use_alpha && n == 4)
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

        int      stride;
        uint8_t *plane = heif_img.get_plane(heif_channel_interleaved, &stride);

        // Copy pixel data
        size_t row_bytes = w * n * sizeof(uint8_t);
        for (int y = 0; y < h; ++y) { memcpy(plane + y * (size_t)stride, pixels8.get() + y * w * n, row_bytes); }

        // Set color profile (nclx)
        heif::ColorProfile_nclx nclx;
        nclx.set_color_primaries(heif_color_primaries_ITU_R_BT_709_5); // TODO map from img.chromaticities
        heif_transfer_characteristics heif_tf = transfer_function_to_heif(params->tf);
        if (!is_heif_transfer_supported(params->tf))
        {
            spdlog::warn("HEIF: Transfer function '{}' not supported, falling back to sRGB.",
                         transfer_function_name(params->tf, params->gamma));
            heif_tf = heif_transfer_characteristic_IEC_61966_2_1;
        }
        nclx.set_transfer_characteristics(heif_tf);

        nclx.set_matrix_coefficients(heif_matrix_coefficients_ITU_R_BT_709_5);
        nclx.set_full_range_flag(true);
        heif_img.set_nclx_color_profile(nclx);

        // Encoder selection
        if (params->format_index < 0 || params->format_index >= int(s_heif_supported_formats.size()))
            throw std::runtime_error("Invalid HEIF/AVIF format selected");

        auto encoder = s_heif_supported_formats[params->format_index].get_encoder();
        encoder.set_lossy_quality(params->quality);
        encoder.set_lossless(params->lossless);
        // Apply any encoder-specific parameters from the GUI (typed)
        for (auto &item : params->encoder_parameters.items())
        {
            const std::string name = item.key();

            // we handle the common "Lossless" and "Quality" parameters separately
            if (name == "lossless" || name == "quality")
                continue;

            const json &val = item.value();
            try
            {
                if (val.is_number_integer())
                    encoder.set_integer_parameter(name, val.get<int>());
                else if (val.is_boolean())
                    encoder.set_boolean_parameter(name, val.get<bool>());
                else if (val.is_string())
                    encoder.set_string_parameter(name, val.get<std::string>());
                else
                    // Fallback: set as string representation
                    encoder.set_parameter(name, val.dump());
            }
            catch (...)
            {
                spdlog::warn("HEIF: failed to set encoder parameter {} = {}", name, val.dump());
            }
        }

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

void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, float gain, int quality,
                     bool lossless, bool use_alpha, int format_index, TransferFunction_ tf, float gamma)
{
    HEIFSaveOptions params;
    params.gain         = gain;
    params.quality      = quality;
    params.lossless     = lossless;
    params.use_alpha    = use_alpha;
    params.format_index = format_index;
    params.tf           = tf;
    params.gamma        = gamma;

    save_heif_image(img, os, filename, &params);
}

// GUI parameter function
HEIFSaveOptions *heif_parameters_gui()
{
    init_heif_supported_formats();

    static int selected_format = 0;
    if (selected_format >= int(s_heif_supported_formats.size()))
        selected_format = 0;

    auto &selected_encoder = s_heif_supported_formats[selected_format];

    if (ImGui::PE::Begin("HEIF/AVIF Save Options", ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

        // Gain (custom widget with button)
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

        // Transfer function
        ImGui::PE::Entry(
            "Transfer function",
            [&]
            {
                if (ImGui::BeginCombo("##Transfer function",
                                      transfer_function_name(s_opts.tf, 1.f / s_opts.gamma).c_str()))
                {
                    for (int i = TransferFunction_Linear; i < TransferFunction_Count; ++i)
                    {
                        if (!is_heif_transfer_supported((TransferFunction_)i))
                            continue;
                        bool selected = (s_opts.tf == (TransferFunction_)i);
                        if (ImGui::Selectable(transfer_function_name((TransferFunction_)i, 1.f / s_opts.gamma).c_str(),
                                              selected))
                            s_opts.tf = (TransferFunction_)i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Tooltip("Encode the pixel values using this transfer function.");
                if (s_opts.tf == TransferFunction_Gamma)
                {
                    ImGui::Indent();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Gamma");
                    ImGui::SameLine(HelloImGui::EmSize(9.f));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::SliderFloat("##Gamma", &s_opts.gamma, 0.1f, 5.f);
                    ImGui::Unindent();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.");

        // Include alpha
        ImGui::PE::Checkbox("Include alpha", &s_opts.use_alpha);

        auto enc_open =
            ImGui::PE::TreeNode("Encoder", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        {
            if (ImGui::BeginCombo("##Encoder", selected_encoder.get_name().c_str()))
            {
                for (size_t i = 0; i < s_heif_supported_formats.size(); ++i)
                {
                    bool selected = (selected_format == int(i));
                    if (ImGui::Selectable(s_heif_supported_formats[i].get_id_name().c_str(), selected))
                        selected_format = int(i);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            s_opts.format_index = selected_format; // repurpose as index
        }

        if (enc_open)
        {
            // // Encoder + parameters (complex UI inside Entry)
            // ImGui::PE::Entry(
            //     "Encoder",
            //     [&]
            //     {
            //         if (ImGui::BeginCombo("##Encoder", selected_encoder.get_name().c_str()))
            //         {
            //             for (size_t i = 0; i < s_heif_supported_formats.size(); ++i)
            //             {
            //                 bool selected = (selected_format == int(i));
            //                 if (ImGui::Selectable(s_heif_supported_formats[i].get_id_name().c_str(), selected))
            //                     selected_format = int(i);
            //                 if (selected)
            //                     ImGui::SetItemDefaultFocus();
            //             }
            //             ImGui::EndCombo();
            //         }
            //         s_opts.format_index = selected_format; // repurpose as index

            //         return true;
            //     },
            //     "Select encoder.");

            // Lossless
            if (selected_encoder.supports_lossless_compression())
            {
                ImGui::BeginDisabled(!selected_encoder.supports_lossy_compression());
                bool lossless = s_opts.lossless || !selected_encoder.supports_lossy_compression();
                if (ImGui::PE::Checkbox("Lossless", &lossless,
                                        "If enabled, the encoder will use lossless compression if supported."))
                    s_opts.lossless = lossless;
                ImGui::EndDisabled();
            }

            // Quality
            if (selected_encoder.supports_lossy_compression())
            {
                ImGui::BeginDisabled(s_opts.lossless);
                ImGui::PE::SliderInt("Quality", &s_opts.quality, 1, 100, "%d", 0,
                                     "Controls the quality of the encoded image (1 = worst, 100 = best).");
                ImGui::EndDisabled();
            }

            if (ImGui::PE::TreeNode("Advanced", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DrawLinesFull))
            {
                // Show encoder-specific parameters
                heif_encoder *encoder;
                auto          err = heif::Error(
                    heif_context_get_encoder_for_format(nullptr, selected_encoder.get_compression_format(), &encoder));
                if (err)
                {
                    throw err;
                }

                auto enc = std::shared_ptr<heif_encoder>(encoder, [](heif_encoder *e) { heif_encoder_release(e); });

                const struct heif_encoder_parameter *const *params = heif_encoder_list_parameters(enc.get());
                for (int i = 0; params[i]; ++i)
                {
                    const char *name = heif_encoder_parameter_get_name(params[i]);
                    std::string uppercase_name(name);
                    if (!uppercase_name.empty())
                        uppercase_name[0] =
                            static_cast<char>(std::toupper(static_cast<unsigned char>(uppercase_name[0])));
                    auto        type = heif_encoder_parameter_get_type(params[i]);
                    std::string key(name);

                    // we handle the common "Lossless" and "Quality" parameters separately
                    if (key == "lossless" || key == "quality")
                        continue;

                    ImGui::PushID(i);

                    // Initialize default value if not present
                    if (!s_opts.encoder_parameters.contains(key))
                    {
                        if (heif_encoder_has_default(enc.get(), name))
                        {
                            if (type == heif_encoder_parameter_type_integer)
                            {
                                int value = 0;
                                (void)heif_encoder_get_parameter_integer(enc.get(), name, &value);
                                s_opts.encoder_parameters[key] = value;
                            }
                            else if (type == heif_encoder_parameter_type_boolean)
                            {
                                int value = 0;
                                (void)heif_encoder_get_parameter_boolean(enc.get(), name, &value);
                                s_opts.encoder_parameters[key] = (value ? true : false);
                            }
                            else if (type == heif_encoder_parameter_type_string)
                            {
                                const int bufsize = 256;
                                char      buf[bufsize];
                                (void)heif_encoder_get_parameter_string(enc.get(), name, buf, bufsize);
                                s_opts.encoder_parameters[key] = std::string(buf);
                            }
                        }
                        else
                        {
                            // No default - pick a reasonable fallback
                            if (type == heif_encoder_parameter_type_integer)
                            {
                                int have_minimum = 0, have_maximum = 0, minimum = 0, maximum = 0, num_valid_values = 0;
                                const int *valid_values = nullptr;
                                (void)heif_encoder_parameter_integer_valid_values(enc.get(), name, &have_minimum,
                                                                                  &have_maximum, &minimum, &maximum,
                                                                                  &num_valid_values, &valid_values);
                                if (num_valid_values > 0)
                                    s_opts.encoder_parameters[key] = valid_values[0];
                                else if (have_minimum)
                                    s_opts.encoder_parameters[key] = minimum;
                                else
                                    s_opts.encoder_parameters[key] = 0;
                            }
                            else if (type == heif_encoder_parameter_type_boolean)
                            {
                                s_opts.encoder_parameters[key] = false;
                            }
                            else
                            {
                                s_opts.encoder_parameters[key] = std::string("");
                            }
                        }
                    }

                    if (type == heif_encoder_parameter_type_integer)
                    {
                        int        have_minimum = 0, have_maximum = 0, minimum = 0, maximum = 0, num_valid_values = 0;
                        const int *valid_values = nullptr;
                        (void)heif_encoder_parameter_integer_valid_values(enc.get(), name, &have_minimum, &have_maximum,
                                                                          &minimum, &maximum, &num_valid_values,
                                                                          &valid_values);

                        if (num_valid_values > 0)
                            ImGui::PE::Entry(
                                uppercase_name,
                                [&]
                                {
                                    int         cur     = s_opts.encoder_parameters.value(key, 0);
                                    std::string preview = std::to_string(cur);
                                    if (ImGui::BeginCombo(("##" + key).c_str(), preview.c_str()))
                                    {
                                        for (int k = 0; k < num_valid_values; ++k)
                                        {
                                            bool selected = (cur == valid_values[k]);
                                            if (ImGui::Selectable(std::to_string(valid_values[k]).c_str(), selected))
                                                s_opts.encoder_parameters[key] = valid_values[k];
                                            if (selected)
                                                ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                    return false;
                                });
                        else
                        {
                            int val = s_opts.encoder_parameters.value(key, 0);
                            if (have_minimum && have_maximum)
                            {
                                if (ImGui::PE::SliderInt(uppercase_name, &val, minimum, maximum))
                                    s_opts.encoder_parameters[key] = val;
                            }
                            else
                            {
                                if (ImGui::PE::DragInt(uppercase_name, &val))
                                    s_opts.encoder_parameters[key] = val;
                            }
                        }
                    }
                    else if (type == heif_encoder_parameter_type_boolean)
                    {
                        bool b = s_opts.encoder_parameters.value(key, false);
                        if (ImGui::PE::Checkbox(uppercase_name, &b))
                            s_opts.encoder_parameters[key] = b;
                    }
                    else if (type == heif_encoder_parameter_type_string)
                    {
                        const char *const *valid_options = nullptr;
                        (void)heif_encoder_parameter_string_valid_values(enc.get(), name, &valid_options);

                        ImGui::PE::Entry(uppercase_name,
                                         [&]
                                         {
                                             if (valid_options)
                                             {
                                                 std::string preview = s_opts.encoder_parameters.value(key, "");
                                                 if (ImGui::BeginCombo(("##" + key).c_str(), preview.c_str()))
                                                 {
                                                     for (int k = 0; valid_options[k]; ++k)
                                                     {
                                                         bool selected = (preview == valid_options[k]);
                                                         if (ImGui::Selectable(valid_options[k], selected))
                                                             s_opts.encoder_parameters[key] =
                                                                 std::string(valid_options[k]);
                                                         if (selected)
                                                             ImGui::SetItemDefaultFocus();
                                                     }
                                                     ImGui::EndCombo();
                                                 }
                                             }
                                             else
                                             {
                                                 char        buf[256];
                                                 std::string cur = "";
                                                 if (s_opts.encoder_parameters[key].is_string())
                                                     cur = s_opts.encoder_parameters[key].get<std::string>();
                                                 strncpy(buf, cur.c_str(), sizeof(buf));
                                                 buf[sizeof(buf) - 1] = '\0';
                                                 if (ImGui::InputText(("##" + key).c_str(), buf, sizeof(buf)))
                                                     s_opts.encoder_parameters[key] = std::string(buf);
                                             }
                                             return false;
                                         });
                    }
                    ImGui::PopID();
                }
                ImGui::PE::TreePop();
            }
            ImGui::PE::TreePop();
        }

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = HEIFSaveOptions{};

    return &s_opts;
}

#endif
